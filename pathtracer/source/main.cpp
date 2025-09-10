#include "common.h"
#include "core/context.h"
#include "core/texture.h"
#include "core/accel.h"
#include "math/math_utils.h"
#include "math/mat4.h"
#include "render/camera.h"
#include "render/model_loader.h"

#include <map>
#include <cmath>
#include <string>
#include <fstream>
#include <iostream>
#include <functional>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

///////////// MODEL LOADER /////////////

struct SceneObject {
    std::string modelPath;
    Mat4 transform;
};

std::vector<SceneObject> MODELS_TO_LOAD = {
    //{"chess/ABeautifulGame.gltf", Mat4::identity()},
    //{"porsche/scene.gltf", Mat4::identity()},
    //{"FLY/FlightHelmet.gltf", Mat4::identity()},
    //{"lantern/Lantern.gltf", Mat4::scale(Vec3(0.1))},
    //{"env/EnvironmentTest.gltf", Mat4::scale(Vec3(0.8))},
    //{"sponza_lights/scene.gltf", Mat4::identity()},
    {"bath/scene.gltf", Mat4::identity()},
    //{"sponza_lights/NewSponza_4_Combined_glTF.gltf", Mat4::identity()},
    //{"sponza_main/NewSponza_Main_glTF_003.gltf", Mat4::identity()},
    //{"helmet/DamagedHelmet.gltf", Mat4::identity()},
};

////////////////////////////////////////


// Global variables
Camera camera;
bool firstMouse = true;
double lastX = WIDTH / 2.0;
double lastY = HEIGHT / 2.0;

// Function declarations
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void processInput(GLFWwindow* window, float deltaTime);

// Push constants and matrix buffer structures
struct PushConstants {
    int frame;
    float pad1, pad2, pad3;
    Vec3 cameraPos;
    float pad4;
    Vec3 cameraFront;
    float pad5;
    Vec3 cameraUp;
    float pad6;
    Vec3 cameraRight;
};

struct EmissiveTriGPU {
    alignas(16) float v0[4];       // xyz, pad
    alignas(16) float v1[4];
    alignas(16) float v2[4];
    alignas(16) float normal[4];   // xyz, pad
    alignas(16) float emission[4]; // rgb radiance (linear), pad
    alignas(16) float area[4];     // area in .x, rest pad
};

int main() {
    // 1. Initialize GLFW and create window
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, APP_NAME, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 2. Initialize Vulkan context
    Context context;
    context.initDevice(window);

    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.setSurface(*context.surface);
    swapchainInfo.setMinImageCount(3);
    swapchainInfo.setImageFormat(vk::Format::eB8G8R8A8Unorm);
    swapchainInfo.setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear);
    swapchainInfo.setImageExtent({ WIDTH, HEIGHT });
    swapchainInfo.setImageArrayLayers(1);
    swapchainInfo.setImageUsage(vk::ImageUsageFlagBits::eTransferDst);
    swapchainInfo.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity);
    swapchainInfo.setPresentMode(vk::PresentModeKHR::eFifo);
    swapchainInfo.setClipped(true);
    swapchainInfo.setQueueFamilyIndices(context.queueFamilyIndex);
    vk::UniqueSwapchainKHR swapchain = context.device->createSwapchainKHRUnique(swapchainInfo);

    std::vector<vk::Image> swapchainImages = context.device->getSwapchainImagesKHR(*swapchain);

    vk::CommandBufferAllocateInfo commandBufferInfo;
    commandBufferInfo.setCommandPool(*context.commandPool);
    commandBufferInfo.setCommandBufferCount(static_cast<uint32_t>(swapchainImages.size()));
    std::vector<vk::UniqueCommandBuffer> commandBuffers = context.device->allocateCommandBuffersUnique(commandBufferInfo);

    Image outputImage{
        context,
        {WIDTH, HEIGHT},
        vk::Format::eB8G8R8A8Unorm,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
    };

    Image accumImage{
        context,
        {WIDTH, HEIGHT},
        vk::Format::eB8G8R8A8Unorm,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
    };

    std::vector<Vertex> sceneVertices;
    std::vector<uint32_t> sceneIndices;
    std::vector<Material> sceneMaterials;
    std::vector<uint32_t> sceneFaceMaterialIndices;
    std::vector<std::string> sceneTextureFiles;
    std::unordered_map<std::string, int> textureIndexMap; // To de-duplicate textures across files

    // Keep track of offsets as we load each model
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0; // Not strictly needed for appending, but good for clarity
    uint32_t materialOffset = 0;

    std::cout << "Loading scene..." << std::endl;

    // 3. Loop through and load each object
    for (const auto& object : MODELS_TO_LOAD) {
        // Use temporary vectors for each model load
        std::vector<Vertex> tempVertices;
        std::vector<uint32_t> tempIndices;
        std::vector<Material> tempMaterials;
        std::vector<uint32_t> tempFaceMaterialIndices;
        std::vector<std::string> tempTextureFiles;

        std::string fullPath = "../assets/models/" + object.modelPath;
        loadFromFile(tempVertices, tempIndices, tempMaterials, tempFaceMaterialIndices, tempTextureFiles, fullPath);

        // --- Apply Transformation ---
        Mat4 transform = object.transform;
        Mat3 normalTransform = transform.toMat3().inverse().transpose(); // For normals
        for (auto& vertex : tempVertices) {
            vertex.position = transform.transformPoint(vertex.position);
            vertex.normal = normalTransform * vertex.normal;
            // Tangent should also be transformed if used for lighting
            if (vertex.tangent != Vec3(0.0f, 0.0f, 0.0f)) {
                vertex.tangent = normalTransform * vertex.tangent;
            }
        }

        // --- Append and Offset Data ---
        for (const auto& index : tempIndices) {
            sceneIndices.push_back(vertexOffset + index);
        }
        for (const auto& matIndex : tempFaceMaterialIndices) {
            sceneFaceMaterialIndices.push_back(materialOffset + matIndex);
        }

        // De-duplicate and append textures
        for (auto& material : tempMaterials) {
            if (material.diffuseTextureID != -1) {
                const std::string& path = tempTextureFiles[material.diffuseTextureID];
                if (textureIndexMap.find(path) == textureIndexMap.end()) {
                    textureIndexMap[path] = sceneTextureFiles.size();
                    sceneTextureFiles.push_back(path);
                }
                material.diffuseTextureID = textureIndexMap[path];
            }
            // Repeat for metalRoughTextureID and normalTextureID
            if (material.metalRoughTextureID != -1) {
                const std::string& path = tempTextureFiles[material.metalRoughTextureID];
                if (textureIndexMap.find(path) == textureIndexMap.end()) {
                    textureIndexMap[path] = sceneTextureFiles.size();
                    sceneTextureFiles.push_back(path);
                }
                material.metalRoughTextureID = textureIndexMap[path];
            }
            if (material.normalTextureID != -1) {
                const std::string& path = tempTextureFiles[material.normalTextureID];
                if (textureIndexMap.find(path) == textureIndexMap.end()) {
                    textureIndexMap[path] = sceneTextureFiles.size();
                    sceneTextureFiles.push_back(path);
                }
                material.normalTextureID = textureIndexMap[path];
            }
            /*if (material.emissiveTextureID != -1) {
                const std::string& path = tempTextureFiles[material.emissiveTextureID];
                if (textureIndexMap.find(path) == textureIndexMap.end()) {
                    textureIndexMap[path] = sceneTextureFiles.size();
                    sceneTextureFiles.push_back(path);
                }
                material.emissiveTextureID = textureIndexMap[path];
            }
            if (material.occlusionTextureID != -1) {
                const std::string& path = tempTextureFiles[material.occlusionTextureID];
                if (textureIndexMap.find(path) == textureIndexMap.end()) {
                    textureIndexMap[path] = sceneTextureFiles.size();
                    sceneTextureFiles.push_back(path);
                }
                material.occlusionTextureID = textureIndexMap[path];
            }*/
        }

        sceneVertices.insert(sceneVertices.end(), tempVertices.begin(), tempVertices.end());
        sceneMaterials.insert(sceneMaterials.end(), tempMaterials.begin(), tempMaterials.end());

        // Update offsets for the next model
        vertexOffset += static_cast<uint32_t>(tempVertices.size());
        indexOffset += static_cast<uint32_t>(tempIndices.size());
        materialOffset += static_cast<uint32_t>(tempMaterials.size());

        std::cout << " - Loaded " << object.modelPath << std::endl;
    }

    // 4. Create GPU buffers from the final, concatenated scene data
    // (Replace old vectors with the 'scene' vectors)
    if (sceneVertices.empty() || sceneIndices.empty()) {
        throw std::runtime_error("No vertices or indices loaded for the scene");
    }

    std::cout
        << sceneVertices.size() << " vertices" << std::endl
        << sceneIndices.size() << " indices" << std::endl
        << sceneMaterials.size() << " unique materials, " << std::endl
        << sceneTextureFiles.size() << " textures" << std::endl;

    // Load textures
    std::vector<Texture> textures;
    textures.reserve(sceneTextureFiles.size());
    for (const auto& filePath : sceneTextureFiles) {
        textures.push_back(createTexture(context, filePath));
    }

    Buffer vertexBuffer{ context, Buffer::Type::AccelInput, sizeof(Vertex) * sceneVertices.size(), sceneVertices.data() };
    Buffer indexBuffer{ context, Buffer::Type::AccelInput, sizeof(uint32_t) * sceneIndices.size(), sceneIndices.data() };
    Buffer materialBuffer{ context, Buffer::Type::AccelInput, sizeof(Material) * sceneMaterials.size(), sceneMaterials.data() };
    Buffer faceMaterialIndexBuffer{ context, Buffer::Type::AccelInput, sizeof(uint32_t) * sceneFaceMaterialIndices.size(), sceneFaceMaterialIndices.data() };

    // 5. Build emissive triangle list and CDF
    std::vector<EmissiveTriGPU> emissiveTris;
    emissiveTris.reserve(256);

    const uint32_t primitiveCount = static_cast<uint32_t>(sceneIndices.size() / 3);
    for (uint32_t prim = 0; prim < primitiveCount; ++prim) {
        uint32_t i0 = sceneIndices[3 * prim + 0];
        uint32_t i1 = sceneIndices[3 * prim + 1];
        uint32_t i2 = sceneIndices[3 * prim + 2];

        Vec3 p0 = sceneVertices[i0].position;
        Vec3 p1 = sceneVertices[i1].position;
        Vec3 p2 = sceneVertices[i2].position;

        // face->material index
        uint32_t matIdx = sceneFaceMaterialIndices[prim];
        if (matIdx >= sceneMaterials.size()) continue;
        Material mat = sceneMaterials[matIdx];

        // Compute per-triangle emission. We use material.emission * material.albedo (component-wise)
        // You can change this to include emissive textures averaging later.
        Vec3 triEmission = { mat.emission.x * mat.albedo.x,
                             mat.emission.y * mat.albedo.y,
                             mat.emission.z * mat.albedo.z };

        // luminance check
        float lum = 0.2126f * triEmission.x + 0.7152f * triEmission.y + 0.0722f * triEmission.z;
        if (lum <= 1e-6f) continue;

        // area and normal
        Vec3 e1 = p1 - p0;
        Vec3 e2 = p2 - p0;
        Vec3 n = cross(e1, e2);
        float area = 0.5f * n.length();
        if (area <= 1e-9f) continue;
        n = normalize(n);

        EmissiveTriGPU et{};
        et.v0[0] = p0.x; et.v0[1] = p0.y; et.v0[2] = p0.z; et.v0[3] = 0.0f;
        et.v1[0] = p1.x; et.v1[1] = p1.y; et.v1[2] = p1.z; et.v1[3] = 0.0f;
        et.v2[0] = p2.x; et.v2[1] = p2.y; et.v2[2] = p2.z; et.v2[3] = 0.0f;
        et.normal[0] = n.x; et.normal[1] = n.y; et.normal[2] = n.z; et.normal[3] = 0.0f;
        et.emission[0] = triEmission.x; et.emission[1] = triEmission.y; et.emission[2] = triEmission.z; et.emission[3] = 0.0f;
        et.area[0] = area; et.area[1] = et.area[2] = et.area[3] = 0.0f;

        emissiveTris.push_back(et);
    }

    // Build CDF (weight = area * luminance)
    std::vector<float> emissiveCdf;
    emissiveCdf.reserve(emissiveTris.size());
    float accum = 0.0f;
    for (size_t i = 0; i < emissiveTris.size(); ++i) {
        const EmissiveTriGPU& et = emissiveTris[i];
        float lum = 0.2126f * et.emission[0] + 0.7152f * et.emission[1] + 0.0722f * et.emission[2];
        float w = std::max(1e-6f, lum) * std::max(1e-9f, et.area[0]);
        accum += w;
        emissiveCdf.push_back(accum);
    }

    if (accum > 0.0f) {
        // normalize CDF to [0,1]
        for (auto& v : emissiveCdf) v /= accum;
    }

    // Create GPU buffers (safe even if there are no emissives)
    size_t emissiveTriCount = emissiveTris.empty() ? 1 : emissiveTris.size();
    size_t emissiveCdfCount = emissiveCdf.empty() ? 1 : emissiveCdf.size();

    // Dummy data for empty case
    EmissiveTriGPU dummyTri{};
    float dummyCdf = 1.0f;

    Buffer emissiveBuffer{
        context,
        Buffer::Type::AccelInput,
        sizeof(EmissiveTriGPU) * emissiveTriCount,
        emissiveTris.empty() ? &dummyTri : emissiveTris.data()
    };

    Buffer emissiveCdfBuffer{
        context,
        Buffer::Type::AccelInput,
        sizeof(float) * emissiveCdfCount,
        emissiveCdf.empty() ? &dummyCdf : emissiveCdf.data()
    };

    // light count uniform
    int lightCountInt = static_cast<int>(emissiveTris.size());
    Buffer lightCountBuffer{
        context,
        Buffer::Type::AccelInput,
        sizeof(int),
        &lightCountInt
    };

    // debug output
    std::cout << "Emissive triangles: " << emissiveTris.size() << ", CDF size: " << emissiveCdf.size() << std::endl;

    // 6. Build a single BLAS for the whole scene
    vk::AccelerationStructureGeometryTrianglesDataKHR triangleData;
    triangleData.setVertexFormat(vk::Format::eR32G32B32Sfloat);
    triangleData.setVertexData(vertexBuffer.deviceAddress);
    triangleData.setVertexStride(sizeof(Vertex));
    triangleData.setMaxVertex(static_cast<uint32_t>(sceneVertices.size()));
    triangleData.setIndexType(vk::IndexType::eUint32);
    triangleData.setIndexData(indexBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR triangleGeometry;
    triangleGeometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
    triangleGeometry.setGeometry({ triangleData });
    triangleGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    Accel bottomAccel{ context, triangleGeometry, primitiveCount, vk::AccelerationStructureTypeKHR::eBottomLevel };

    // 7. Build a TLAS with one instance pointing to the scene BLAS
    // (The transform is identity because we already applied it to the vertices)
    vk::TransformMatrixKHR transformMatrix = std::array{
        std::array{1.0f, 0.0f, 0.0f, 0.0f},
        std::array{0.0f, 1.0f, 0.0f, 0.0f},
        std::array{0.0f, 0.0f, 1.0f, 0.0f},
    };

    vk::AccelerationStructureInstanceKHR accelInstance;
    accelInstance.setTransform(transformMatrix);
    accelInstance.setMask(0xFF);
    accelInstance.setAccelerationStructureReference(bottomAccel.buffer.deviceAddress);
    accelInstance.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);

    Buffer instancesBuffer{ context, Buffer::Type::AccelInput, sizeof(vk::AccelerationStructureInstanceKHR), &accelInstance };

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData;
    instancesData.setArrayOfPointers(false);
    instancesData.setData(instancesBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR instanceGeometry;
    instanceGeometry.setGeometryType(vk::GeometryTypeKHR::eInstances);
    instanceGeometry.setGeometry({ instancesData });
    instanceGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    Accel topAccel{ context, instanceGeometry, 1, vk::AccelerationStructureTypeKHR::eTopLevel };

    if (topAccel.buffer.deviceAddress == 0) {
        throw std::runtime_error("TLAS device address is zero");
    }

    // Load shaders
    const std::vector<char> raygenCode = readFile("../assets/shaders/raygen.rgen.spv");
    const std::vector<char> missCode = readFile("../assets/shaders/miss.rmiss.spv");
    const std::vector<char> chitCode = readFile("../assets/shaders/closesthit.rchit.spv");

    // Shader validation
    if (raygenCode.empty() || missCode.empty() || chitCode.empty()) {
        throw std::runtime_error("Failed to load shader code");
    }

    std::cout << "Raygen shader size: " << raygenCode.size() << " bytes" << std::endl;
    std::cout << "Miss shader size: " << missCode.size() << " bytes" << std::endl;
    std::cout << "Closest hit shader size: " << chitCode.size() << " bytes" << std::endl;

    std::vector<vk::UniqueShaderModule> shaderModules(3);
    shaderModules[0] = context.device->createShaderModuleUnique({ {}, raygenCode.size(), reinterpret_cast<const uint32_t*>(raygenCode.data()) });
    shaderModules[1] = context.device->createShaderModuleUnique({ {}, missCode.size(), reinterpret_cast<const uint32_t*>(missCode.data()) });
    shaderModules[2] = context.device->createShaderModuleUnique({ {}, chitCode.size(), reinterpret_cast<const uint32_t*>(chitCode.data()) });

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages(3);
    shaderStages[0] = { {}, vk::ShaderStageFlagBits::eRaygenKHR, *shaderModules[0], "main" };
    shaderStages[1] = { {}, vk::ShaderStageFlagBits::eMissKHR, *shaderModules[1], "main" };
    shaderStages[2] = { {}, vk::ShaderStageFlagBits::eClosestHitKHR, *shaderModules[2], "main" };

    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
    shaderGroups[0] = { vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR };
    shaderGroups[1] = { vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR };
    shaderGroups[2] = { vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, 2, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR };

    // Note: Ensure your device supports enough samplers. Sponza has ~50 textures.
    // A size of 0 is invalid, so handle the no-texture case.
    const uint32_t textureCount = textures.empty() ? 1u : static_cast<uint32_t>(textures.size());

    // create ray tracing pipeline
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        {0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR},             // 0 = TLAS
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR},                         // 1 = accumImage (rgba32f)
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR},                         // 2 = outputImage (rgba8)
        {3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},                    // 3 = Vertices
        {4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},                    // 4 = Indices
        {5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},                    // 5 = Materials
        {6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},                    // 6 = Face Material Indices
        {7, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eClosestHitKHR},  // 7 = Textures
        {8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR},                        // 8 = EmissiveTris SSBO
        {9, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR},                        // 9 = Emissive CDF SSBO (float[])
        {10, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR},                       // 10 = Light count UBO
    };

    // Create desc set layout
    vk::DescriptorSetLayoutCreateInfo descSetLayoutInfo;
    descSetLayoutInfo.setBindings(bindings);
    vk::UniqueDescriptorSetLayout descSetLayout = context.device->createDescriptorSetLayoutUnique(descSetLayoutInfo);

    // Create pipeline layout
    vk::PushConstantRange pushRange;
    pushRange.setOffset(0);
    pushRange.setSize(sizeof(PushConstants));
    pushRange.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(*descSetLayout);
    pipelineLayoutInfo.setPushConstantRanges(pushRange);
    vk::UniquePipelineLayout pipelineLayout = context.device->createPipelineLayoutUnique(pipelineLayoutInfo);

    // Create pipeline
    vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
    rtPipelineInfo.setStages(shaderStages);
    rtPipelineInfo.setGroups(shaderGroups);
    rtPipelineInfo.setMaxPipelineRayRecursionDepth(4);
    rtPipelineInfo.setLayout(*pipelineLayout);

    auto result = context.device->createRayTracingPipelineKHRUnique(nullptr, nullptr, rtPipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create ray tracing pipeline.");
    }

    vk::UniquePipeline pipeline = std::move(result.value);

    // Get ray tracing properties
    auto properties = context.physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    auto rtProperties = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    // Calculate shader binding table (SBT) size
    uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    uint32_t handleSizeAligned = rtProperties.shaderGroupHandleAlignment;
    uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
    uint32_t sbtSize = groupCount * handleSizeAligned;

    // Get shader group handles
    std::vector<uint8_t> handleStorage(sbtSize);
    if (context.device->getRayTracingShaderGroupHandlesKHR(*pipeline, 0, groupCount, sbtSize, handleStorage.data()) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to get ray tracing shader group handles.");
    }

    // Create SBT
    Buffer raygenSBT{ context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 0 * handleSizeAligned };
    Buffer missSBT{ context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 1 * handleSizeAligned };
    Buffer hitSBT{ context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 2 * handleSizeAligned };

    // SBT validation
    if (raygenSBT.deviceAddress == 0 || missSBT.deviceAddress == 0 || hitSBT.deviceAddress == 0) {
        throw std::runtime_error("SBT device address is zero");
    }

    std::cout << "Shader group handle size: " << handleSize << std::endl;
    std::cout << "Shader group handle alignment: " << handleSizeAligned << std::endl;
    std::cout << "Raygen SBT address: " << raygenSBT.deviceAddress << std::endl;
    std::cout << "Miss SBT address: " << missSBT.deviceAddress << std::endl;
    std::cout << "Hit SBT address: " << hitSBT.deviceAddress << std::endl;

    uint32_t stride = rtProperties.shaderGroupHandleAlignment;
    uint32_t size = rtProperties.shaderGroupHandleAlignment;

    vk::StridedDeviceAddressRegionKHR raygenRegion{ raygenSBT.deviceAddress, stride, size };
    vk::StridedDeviceAddressRegionKHR missRegion{ missSBT.deviceAddress, stride, size };
    vk::StridedDeviceAddressRegionKHR hitRegion{ hitSBT.deviceAddress, stride, size };

    // Create desc set
    vk::UniqueDescriptorSet descSet = context.allocateDescSet(*descSetLayout);

    // Create dummy resources that persist until the end of the program
    vk::UniqueSampler dummySampler;
    vk::UniqueImageView dummyImageView;

    // Prepare the texture info for the descriptor write
    std::vector<vk::DescriptorImageInfo> imageInfos;
    if (textures.empty()) {
        // Create a dummy texture/sampler for the case when there are no textures
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        dummySampler = context.device->createSamplerUnique(samplerInfo);

        // Create a dummy 1x1 black texture
        Image dummyImage{
            context,
            {1, 1},
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eShaderReadOnlyOptimal
        };

        dummyImageView = std::move(dummyImage.view);

        // Add the dummy descriptor
        imageInfos.push_back({ *dummySampler, *dummyImageView, vk::ImageLayout::eShaderReadOnlyOptimal });
    }
    else {
        for (const auto& texture : textures) {
            imageInfos.push_back({ *texture.sampler, *texture.image.view, vk::ImageLayout::eShaderReadOnlyOptimal });
        }
    }

    // Create the descriptor writes
    std::vector<vk::WriteDescriptorSet> writes;
    writes.resize(11);

    // 0: TLAS
    writes[0].setDstSet(*descSet);
    writes[0].setDstBinding(0);
    writes[0].setDescriptorCount(1);
    writes[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    writes[0].setPNext(&topAccel.descAccelInfo);

    // 1: accumImage (32-bit float)
    writes[1].setDstSet(*descSet);
    writes[1].setDstBinding(1);
    writes[1].setDescriptorCount(1);
    writes[1].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[1].setImageInfo(accumImage.descImageInfo);

    // 2: outputImage (8-bit unorm)
    writes[2].setDstSet(*descSet);
    writes[2].setDstBinding(2);
    writes[2].setDescriptorCount(1);
    writes[2].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[2].setImageInfo(outputImage.descImageInfo);

    // 3: vertices buffer
    writes[3].setDstSet(*descSet);
    writes[3].setDstBinding(3);
    writes[3].setDescriptorCount(1);
    writes[3].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[3].setBufferInfo(vertexBuffer.descBufferInfo);

    // 4: indices buffer
    writes[4].setDstSet(*descSet);
    writes[4].setDstBinding(4);
    writes[4].setDescriptorCount(1);
    writes[4].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[4].setBufferInfo(indexBuffer.descBufferInfo);

    // 5: materials buffer
    writes[5].setDstSet(*descSet);
    writes[5].setDstBinding(5);
    writes[5].setDescriptorCount(1);
    writes[5].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[5].setBufferInfo(materialBuffer.descBufferInfo);

    // 6: face material indices buffer
    writes[6].setDstSet(*descSet);
    writes[6].setDstBinding(6);
    writes[6].setDescriptorCount(1);
    writes[6].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[6].setBufferInfo(faceMaterialIndexBuffer.descBufferInfo);

    // 7: textures array
    writes[7].setDstSet(*descSet);
    writes[7].setDstBinding(7);
    writes[7].setDescriptorCount(textureCount);
    writes[7].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
    writes[7].setImageInfo(imageInfos);

    // 8: emissive triangles SSBO
    writes[8].setDstSet(*descSet);
    writes[8].setDstBinding(8);
    writes[8].setDescriptorCount(1);
    writes[8].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[8].setBufferInfo(emissiveBuffer.descBufferInfo);

    // 9: emissive CDF SSBO (float[])
    writes[9].setDstSet(*descSet);
    writes[9].setDstBinding(9);
    writes[9].setDescriptorCount(1);
    writes[9].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[9].setBufferInfo(emissiveCdfBuffer.descBufferInfo);

    // 10: lightCount uniform buffer
    writes[10].setDstSet(*descSet);
    writes[10].setDstBinding(10);
    writes[10].setDescriptorCount(1);
    writes[10].setDescriptorType(vk::DescriptorType::eUniformBuffer);
    writes[10].setBufferInfo(lightCountBuffer.descBufferInfo);

    // Descriptor set validation
    for (auto& write : writes) {
        if (write.dstSet == VK_NULL_HANDLE) {
            throw std::runtime_error("Descriptor set is null");
        }
    }

    context.device->updateDescriptorSets(writes, nullptr);

    // Main loop
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    uint32_t imageIndex = 0;
    int frame = 0;
    vk::UniqueSemaphore imageAcquiredSemaphore = context.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());

    std::cout << "Starting main loop..." << std::endl;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Store previous camera position to detect movement
        Vec3 oldPos = camera.position;
        float oldYaw = camera.yaw;
        float oldPitch = camera.pitch;

        // This handles mouse movement via the callback
        glfwPollEvents();
        // This handles keyboard movement
        processInput(window, deltaTime);

        // If the camera moved, reset the frame accumulator
        if (oldPos.x != camera.position.x || oldPos.y != camera.position.y || oldPos.z != camera.position.z || oldYaw != camera.yaw ||
            oldPitch != camera.pitch) {
            frame = 0;
        }

        // Acquire next image
        auto acquireResult = context.device->acquireNextImageKHR(*swapchain, UINT64_MAX, *imageAcquiredSemaphore);
        if (acquireResult.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to acquire next image");
        }
        imageIndex = acquireResult.value;

        // Populate and push constants
        PushConstants pc;
        pc.frame = frame;
        pc.cameraPos = camera.position;
        pc.cameraFront = camera.front;
        pc.cameraUp = camera.up;
        pc.cameraRight = camera.right;

        // Record commands
        vk::CommandBuffer commandBuffer = *commandBuffers[imageIndex];
        commandBuffer.begin(vk::CommandBufferBeginInfo());
        Image::setImageLayout(commandBuffer, *outputImage.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, *descSet, nullptr);
        commandBuffer.pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pc);
        commandBuffer.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, WIDTH, HEIGHT, 1);

        vk::Image srcImage = *outputImage.image;
        vk::Image dstImage = swapchainImages[imageIndex];
        Image::setImageLayout(commandBuffer, srcImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
        Image::setImageLayout(commandBuffer, dstImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        Image::copyImage(commandBuffer, srcImage, dstImage);
        Image::setImageLayout(commandBuffer, srcImage, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        Image::setImageLayout(commandBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);

        commandBuffer.end();

        // Submit
        context.queue.submit(vk::SubmitInfo().setCommandBuffers(commandBuffer));

        // Present image
        vk::PresentInfoKHR presentInfo;
        presentInfo.setSwapchains(*swapchain);
        presentInfo.setImageIndices(imageIndex);
        presentInfo.setWaitSemaphores(*imageAcquiredSemaphore);
        auto result = context.queue.presentKHR(presentInfo);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to present.");
        }
        context.queue.waitIdle();
        frame++;
    }

    context.device->waitIdle();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;  // Reversed since y-coordinates go from top to bottom
    lastX = xpos;
    lastY = ypos;

    camera.processMouse(xoffset, yoffset);
}

void processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.processKeyboard("SHIFT_DOWN", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE)
        camera.processKeyboard("SHIFT_UP", deltaTime);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard("FORWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard("BACKWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard("LEFT", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard("RIGHT", deltaTime);
}