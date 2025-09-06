
#include "common.h"
#include "core/context.h"
#include "rendering/camera.h"
#include "rendering/model_loader.h"
#include "rendering/accel.h"
#include "rendering/denoiser.h"
#include "rendering/gbuffer.h"
#include "core/texture.h"
#include "math/math_utils.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

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
    Vec3 cameraPos;
    float pad;
};

struct MatrixBuffer {
    Mat4 view;
    Mat4 proj;
    Mat4 prevView;
    Mat4 prevProj;
};

int main() {
    Context context;

    glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(context.window, mouse_callback);

    // Create swapchain with additional usage flags
    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.setSurface(*context.surface);
    swapchainInfo.setMinImageCount(3);
    swapchainInfo.setImageFormat(vk::Format::eB8G8R8A8Unorm);
    swapchainInfo.setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear);
    swapchainInfo.setImageExtent({ WIDTH, HEIGHT });
    swapchainInfo.setImageArrayLayers(1);
    // need TRANSFER_DST for blit/copy into the swapchain image
    swapchainInfo.setImageUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage);
    swapchainInfo.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity);
    swapchainInfo.setPresentMode(vk::PresentModeKHR::eFifo);
    swapchainInfo.setClipped(true);
    // Use exclusive sharing mode when only one queue family is used
    swapchainInfo.setImageSharingMode(vk::SharingMode::eExclusive);

    vk::UniqueSwapchainKHR swapchain = context.device->createSwapchainKHRUnique(swapchainInfo);

    std::vector<vk::Image> swapchainImages = context.device->getSwapchainImagesKHR(*swapchain);

    // Create command buffers
    vk::CommandBufferAllocateInfo commandBufferInfo;
    commandBufferInfo.setCommandPool(*context.commandPool);
    commandBufferInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    commandBufferInfo.setCommandBufferCount(static_cast<uint32_t>(swapchainImages.size()));
    std::vector<vk::UniqueCommandBuffer> commandBuffers = context.device->allocateCommandBuffersUnique(commandBufferInfo);

    // Create output image
    Image outputImage{ context, {WIDTH, HEIGHT}, vk::Format::eB8G8R8A8Unorm,
                        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

    // Load mesh
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Face> faces;
    std::vector<std::string> textureFiles;
    loadFromFile(vertices, indices, faces, textureFiles, "../assets/models/sponza.obj");

    // Load textures
    std::vector<Texture> textures;
    textures.reserve(textureFiles.size());
    for (const auto& filePath : textureFiles) {
        textures.push_back(createTexture(context, filePath));
    }

    // Create buffers
    Buffer vertexBuffer{ context, Buffer::Type::AccelInput, sizeof(Vertex) * vertices.size(), vertices.data() };
    Buffer indexBuffer{ context, Buffer::Type::AccelInput, sizeof(uint32_t) * indices.size(), indices.data() };
    Buffer faceBuffer{ context, Buffer::Type::AccelInput, sizeof(Face) * faces.size(), faces.data() };
    Buffer matrixBuffer{ context, Buffer::Type::Uniform, sizeof(MatrixBuffer) };

    // Create BLAS
    vk::AccelerationStructureGeometryTrianglesDataKHR triangleData;
    triangleData.setVertexFormat(vk::Format::eR32G32B32Sfloat);
    triangleData.setVertexData(vertexBuffer.deviceAddress);
    triangleData.setVertexStride(sizeof(Vertex));
    triangleData.setMaxVertex(static_cast<uint32_t>(vertices.size()));
    triangleData.setIndexType(vk::IndexType::eUint32);
    triangleData.setIndexData(indexBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR triangleGeometry;
    triangleGeometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
    triangleGeometry.setGeometry({ triangleData });
    triangleGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    const auto primitiveCount = static_cast<uint32_t>(indices.size() / 3);
    Accel bottomAccel{ context, triangleGeometry, primitiveCount, vk::AccelerationStructureTypeKHR::eBottomLevel };

    // Create TLAS
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

    // Load shaders
    const std::vector<char> raygenCode = readFile("source/shaders/raygen.rgen.spv");
    const std::vector<char> missCode = readFile("source/shaders/miss.rmiss.spv");
    const std::vector<char> chitCode = readFile("source/shaders/closesthit.rchit.spv");

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

    // Create GBuffer and Denoiser
    GBuffer gbuffer = createGBuffer(context, { WIDTH, HEIGHT });
    Denoiser denoiser = createDenoiser(context, { WIDTH, HEIGHT });

    // Create descriptor set layout for ray tracing
    const uint32_t textureCount = textures.empty() ? 1 : static_cast<uint32_t>(textures.size());
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        { 0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR },   // TLAS
        { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },               // Storage image
        { 2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },          // Vertices
        { 3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },          // Indices
        { 4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },          // Faces
        { 5, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eClosestHitKHR}, // Textures
        { 6, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },       // GBuffer position
        { 7, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },       // GBuffer normal
        { 8, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },       // GBuffer albedo
        { 9, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },       // GBuffer motion
        { 10, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR },     // Matrix buffer
    };

    vk::DescriptorSetLayoutCreateInfo descSetLayoutInfo;
    descSetLayoutInfo.setBindings(bindings);
    vk::UniqueDescriptorSetLayout descSetLayout = context.device->createDescriptorSetLayoutUnique(descSetLayoutInfo);

    // Create pipeline layout
    vk::PushConstantRange pushRange;
    pushRange.setOffset(0);
	pushRange.setSize(std::max<uint32_t>(sizeof(PushConstants), 32)); // ensure at least 32 bytes
    pushRange.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    // use vectors to avoid ambiguous overloads
    std::vector<vk::DescriptorSetLayout> pSetLayouts = { *descSetLayout };
    std::vector<vk::PushConstantRange> pPushRanges = { pushRange };
    pipelineLayoutInfo.setSetLayouts(pSetLayouts);
    pipelineLayoutInfo.setPushConstantRanges(pPushRanges);
    vk::UniquePipelineLayout pipelineLayout = context.device->createPipelineLayoutUnique(pipelineLayoutInfo);

    // Create ray tracing pipeline
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

    // Create shader binding table (use padding/alignment)
    uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    uint32_t handleAlignment = rtProperties.shaderGroupHandleAlignment;
    auto padded = [&](uint32_t s) { return (s + handleAlignment - 1) & ~(handleAlignment - 1); };

    uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
    // we know groups: raygen(0), miss(1), hit(2)
    uint32_t raygenCount = 1;
    uint32_t missCount = 1;
    uint32_t hitCount = 1;

    uint32_t raygenSize = padded(handleSize) * raygenCount;
    uint32_t missSize = padded(handleSize) * missCount;
    uint32_t hitSize = padded(handleSize) * hitCount;
    uint32_t sbtSize = raygenSize + missSize + hitSize;

    std::vector<uint8_t> handleStorage(sbtSize);
    if (context.device->getRayTracingShaderGroupHandlesKHR(*pipeline, 0, groupCount, sbtSize, handleStorage.data()) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to get ray tracing shader group handles.");
    }

    // Create SBT buffers using the padded offsets
    // Create SBT buffers using the padded offsets
    uint32_t raygenOffset = 0;
    uint32_t missOffset = raygenOffset + raygenSize;
    uint32_t hitOffset = missOffset + missSize;

    Buffer raygenSBT{ context, Buffer::Type::ShaderBindingTable, raygenSize, handleStorage.data() + raygenOffset };
    Buffer missSBT{ context, Buffer::Type::ShaderBindingTable, missSize, handleStorage.data() + missOffset };
    Buffer hitSBT{ context, Buffer::Type::ShaderBindingTable, hitSize, handleStorage.data() + hitOffset };

    uint64_t stride = padded(handleSize);
    uint64_t size = padded(handleSize);

    vk::StridedDeviceAddressRegionKHR raygenRegion{ raygenSBT.deviceAddress, stride, size };
    vk::StridedDeviceAddressRegionKHR missRegion{ missSBT.deviceAddress, stride, size };
    vk::StridedDeviceAddressRegionKHR hitRegion{ hitSBT.deviceAddress, stride, size };

    // Create descriptor set
    vk::UniqueDescriptorSet descSet = context.allocateDescSet(*descSetLayout);

    // Create dummy resources for textures if needed
    vk::UniqueSampler dummySampler;
    vk::UniqueImageView dummyImageView;

    std::vector<vk::DescriptorImageInfo> imageInfos;
    if (textures.empty()) {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        dummySampler = context.device->createSamplerUnique(samplerInfo);

        Image dummyImage{ context, {1, 1}, vk::Format::eR8G8B8A8Unorm,
                        vk::ImageUsageFlagBits::eSampled, vk::ImageLayout::eShaderReadOnlyOptimal };
        dummyImageView = std::move(dummyImage.view);

        imageInfos.push_back({ *dummySampler, *dummyImageView, vk::ImageLayout::eShaderReadOnlyOptimal });
    }
    else {
        for (const auto& texture : textures) {
            imageInfos.push_back({ *texture.sampler, *texture.image.view, vk::ImageLayout::eShaderReadOnlyOptimal });
        }
    }

    // Update descriptor sets
    std::vector<vk::WriteDescriptorSet> writes(11);

    writes[0].setDstSet(*descSet);
    writes[0].setDstBinding(0);
    writes[0].setDescriptorCount(1);
    writes[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    // Keep using the Accel helper pNext if present (topAccel.descAccelInfo)
    writes[0].setPNext(&topAccel.descAccelInfo);

    writes[1].setDstSet(*descSet);
    writes[1].setDstBinding(1);
    writes[1].setDescriptorCount(1);
    writes[1].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[1].setImageInfo(outputImage.descImageInfo);

    writes[2].setDstSet(*descSet);
    writes[2].setDstBinding(2);
    writes[2].setDescriptorCount(1);
    writes[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[2].setBufferInfo(vertexBuffer.descBufferInfo);

    writes[3].setDstSet(*descSet);
    writes[3].setDstBinding(3);
    writes[3].setDescriptorCount(1);
    writes[3].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[3].setBufferInfo(indexBuffer.descBufferInfo);

    writes[4].setDstSet(*descSet);
    writes[4].setDstBinding(4);
    writes[4].setDescriptorCount(1);
    writes[4].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[4].setBufferInfo(faceBuffer.descBufferInfo);

    writes[5].setDstSet(*descSet);
    writes[5].setDstBinding(5);
    writes[5].setDescriptorCount(textureCount);
    writes[5].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
    writes[5].setImageInfo(imageInfos);

    writes[6].setDstSet(*descSet);
    writes[6].setDstBinding(6);
    writes[6].setDescriptorCount(1);
    writes[6].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[6].setImageInfo(gbuffer.position.descImageInfo);

    writes[7].setDstSet(*descSet);
    writes[7].setDstBinding(7);
    writes[7].setDescriptorCount(1);
    writes[7].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[7].setImageInfo(gbuffer.normal.descImageInfo);

    writes[8].setDstSet(*descSet);
    writes[8].setDstBinding(8);
    writes[8].setDescriptorCount(1);
    writes[8].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[8].setImageInfo(gbuffer.albedo.descImageInfo);

    writes[9].setDstSet(*descSet);
    writes[9].setDstBinding(9);
    writes[9].setDescriptorCount(1);
    writes[9].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[9].setImageInfo(gbuffer.motion.descImageInfo);

    writes[10].setDstSet(*descSet);
    writes[10].setDstBinding(10);
    writes[10].setDescriptorCount(1);
    writes[10].setDescriptorType(vk::DescriptorType::eUniformBuffer);
    writes[10].setBufferInfo(matrixBuffer.descBufferInfo);

    context.device->updateDescriptorSets(writes, nullptr);

	// Update denoiser descriptor set with actual input and gbuffer images
    std::vector<vk::WriteDescriptorSet> denWrites;

    // binding 0: input image (raytrace output)
    vk::WriteDescriptorSet w0{};
    w0.setDstSet(*denoiser.descSet);
    w0.setDstBinding(0);
    w0.setDescriptorCount(1);
    w0.setDescriptorType(vk::DescriptorType::eStorageImage);
    w0.setImageInfo(outputImage.descImageInfo);
    denWrites.push_back(w0);

    // binding 1: gbuffer.position
    vk::WriteDescriptorSet w1{};
    w1.setDstSet(*denoiser.descSet);
    w1.setDstBinding(1);
    w1.setDescriptorCount(1);
    w1.setDescriptorType(vk::DescriptorType::eStorageImage);
    w1.setImageInfo(gbuffer.position.descImageInfo);
    denWrites.push_back(w1);

    // binding 2: gbuffer.normal
    vk::WriteDescriptorSet w2{};
    w2.setDstSet(*denoiser.descSet);
    w2.setDstBinding(2);
    w2.setDescriptorCount(1);
    w2.setDescriptorType(vk::DescriptorType::eStorageImage);
    w2.setImageInfo(gbuffer.normal.descImageInfo);
    denWrites.push_back(w2);

    // binding 3: gbuffer.albedo
    vk::WriteDescriptorSet w3{};
    w3.setDstSet(*denoiser.descSet);
    w3.setDstBinding(3);
    w3.setDescriptorCount(1);
    w3.setDescriptorType(vk::DescriptorType::eStorageImage);
    w3.setImageInfo(gbuffer.albedo.descImageInfo);
    denWrites.push_back(w3);

    // binding 4: gbuffer.motion
    vk::WriteDescriptorSet w4{};
    w4.setDstSet(*denoiser.descSet);
    w4.setDstBinding(4);
    w4.setDescriptorCount(1);
    w4.setDescriptorType(vk::DescriptorType::eStorageImage);
    w4.setImageInfo(gbuffer.motion.descImageInfo);
    denWrites.push_back(w4);

    context.device->updateDescriptorSets(denWrites, nullptr);

    // Create semaphores
    vk::UniqueSemaphore imageAcquiredSemaphore = context.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
    vk::UniqueSemaphore renderFinishedSemaphore = context.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());

    // Main loop
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    uint32_t imageIndex = 0;
    int frame = 0;

    // Initialize previous matrices for motion vectors
    Mat4 prevView = Camera::lookAt(camera);
    Mat4 prevProj = Mat4::perspective(60.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 1000.0f);

    while (!glfwWindowShouldClose(context.window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // cache old camera to reset accumulation if moved
        Vec3 oldPos = camera.position;
        float oldYaw = camera.yaw;
        float oldPitch = camera.pitch;

        glfwPollEvents();
        processInput(context.window, deltaTime);

        if (oldPos.x != camera.position.x || oldPos.y != camera.position.y || oldPos.z != camera.position.z ||
            oldYaw != camera.yaw || oldPitch != camera.pitch) {
            frame = 0;
        }

        // Acquire next swapchain image (signal imageAcquiredSemaphore)
        auto acquireResult = context.device->acquireNextImageKHR(*swapchain, UINT64_MAX, *imageAcquiredSemaphore);
        if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("failed to acquire swapchain image.");
        }
        imageIndex = acquireResult.value;

        // Update matrices and upload uniform buffer
        MatrixBuffer matrices;
        matrices.view = Camera::lookAt(camera);
        matrices.proj = Mat4::perspective(60.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 1000.0f);
        matrices.prevView = prevView;
        matrices.prevProj = prevProj;
        void* mapped = context.device->mapMemory(*matrixBuffer.memory, 0, sizeof(MatrixBuffer));
        memcpy(mapped, &matrices, sizeof(MatrixBuffer));
        context.device->unmapMemory(*matrixBuffer.memory);

        // Prepare push constants
        PushConstants pc{};
        pc.frame = frame;
        pc.cameraPos = camera.position;

        // --- Update denoiser descriptor bindings that main owns (bindings 0..4) ---
        // (these must match svgf.comp bindings)
        std::vector<vk::WriteDescriptorSet> denWrites;

        vk::WriteDescriptorSet w0{};
        w0.setDstSet(*denoiser.descSet);
        w0.setDstBinding(0); // input image (raytrace output)
        w0.setDescriptorCount(1);
        w0.setDescriptorType(vk::DescriptorType::eStorageImage);
        w0.setImageInfo(outputImage.descImageInfo);
        denWrites.push_back(w0);

        vk::WriteDescriptorSet w1{};
        w1.setDstSet(*denoiser.descSet);
        w1.setDstBinding(1); // gbuffer.position
        w1.setDescriptorCount(1);
        w1.setDescriptorType(vk::DescriptorType::eStorageImage);
        w1.setImageInfo(gbuffer.position.descImageInfo);
        denWrites.push_back(w1);

        vk::WriteDescriptorSet w2{};
        w2.setDstSet(*denoiser.descSet);
        w2.setDstBinding(2); // gbuffer.normal
        w2.setDescriptorCount(1);
        w2.setDescriptorType(vk::DescriptorType::eStorageImage);
        w2.setImageInfo(gbuffer.normal.descImageInfo);
        denWrites.push_back(w2);

        vk::WriteDescriptorSet w3{};
        w3.setDstSet(*denoiser.descSet);
        w3.setDstBinding(3); // gbuffer.albedo
        w3.setDescriptorCount(1);
        w3.setDescriptorType(vk::DescriptorType::eStorageImage);
        w3.setImageInfo(gbuffer.albedo.descImageInfo);
        denWrites.push_back(w3);

        vk::WriteDescriptorSet w4{};
        w4.setDstSet(*denoiser.descSet);
        w4.setDstBinding(4); // gbuffer.motion
        w4.setDescriptorCount(1);
        w4.setDescriptorType(vk::DescriptorType::eStorageImage);
        w4.setImageInfo(gbuffer.motion.descImageInfo);
        denWrites.push_back(w4);

        if (!denWrites.empty()) {
            context.device->updateDescriptorSets(denWrites, nullptr);
        }

        // Record commands into per-image command buffer
        vk::CommandBuffer commandBuffer = *commandBuffers[imageIndex];
        commandBuffer.begin(vk::CommandBufferBeginInfo());

        // Ray tracing pass (bind pipeline + descriptor set + push constants)
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, *descSet, nullptr);
        commandBuffer.pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pc);
        commandBuffer.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, WIDTH, HEIGHT, 1);

        // Barrier: ray tracing -> compute (denoiser)
        vk::MemoryBarrier rtToComputeBarrier{
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderRead
        };
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags(),
            std::array<vk::MemoryBarrier, 1>{ rtToComputeBarrier },
            nullptr, nullptr
        );

        // Denoising pass (compute)
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *denoiser.pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *denoiser.pipelineLayout, 0, *denoiser.descSet, nullptr);
        uint32_t groupCountX = (WIDTH + DENOISER_WG_SIZE - 1) / DENOISER_WG_SIZE;
        uint32_t groupCountY = (HEIGHT + DENOISER_WG_SIZE - 1) / DENOISER_WG_SIZE;
        commandBuffer.dispatch(groupCountX, groupCountY, 1);

        // Barrier: compute -> transfer
        vk::MemoryBarrier computeToTransferBarrier{
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eTransferRead
        };
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags(),
            std::array<vk::MemoryBarrier, 1>{ computeToTransferBarrier },
            nullptr, nullptr
        );

        // --- Transfer copy from denoiser image -> swapchain image (transfer-only) ---
        vk::Image srcImage = *denoiser.historyColor[denoiser.historyIndex].image;
        vk::Image dstImage = swapchainImages[imageIndex];

        // Transition srcImage: GENERAL -> TRANSFER_SRC
        Image::setImageLayout(commandBuffer, srcImage,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer);

        // Transition dstImage: UNDEFINED (or present) -> TRANSFER_DST
        // Use TopOfPipe -> Transfer to be safe on transfer-only queues
        Image::setImageLayout(commandBuffer, dstImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);

        // copy (no blit) - works on transfer queues
        vk::ImageCopy copyRegion{};
        copyRegion.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        copyRegion.dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        copyRegion.srcOffset = vk::Offset3D{ 0,0,0 };
        copyRegion.dstOffset = vk::Offset3D{ 0,0,0 };
        copyRegion.extent = vk::Extent3D{ WIDTH, HEIGHT, 1 };

        commandBuffer.copyImage(
            srcImage, vk::ImageLayout::eTransferSrcOptimal,
            dstImage, vk::ImageLayout::eTransferDstOptimal,
            { copyRegion }
        );

        // Transition src back: TRANSFER_SRC -> GENERAL (so compute can write/read next frame)
        Image::setImageLayout(commandBuffer, srcImage,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader);

        // Transition dst -> PRESENT
        Image::setImageLayout(commandBuffer, dstImage,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe);

        commandBuffer.end();

        // Submit with a per-frame fence
        vk::UniqueFence frameFence = context.device->createFenceUnique(vk::FenceCreateInfo{});

        vk::SubmitInfo submitInfo;
        std::array<vk::Semaphore, 1> waitImageSems = { *imageAcquiredSemaphore };
        std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eTransfer }; // wait for the acquire at transfer stage
        std::array<vk::Semaphore, 1> signalSems = { *renderFinishedSemaphore };

        submitInfo.setWaitSemaphores(waitImageSems);
        submitInfo.setWaitDstStageMask(waitStages);
        // explicit single-command-buffer array
        std::array<vk::CommandBuffer, 1> cmdBufs = { commandBuffer };
        submitInfo.setCommandBuffers(cmdBufs);
        submitInfo.setSignalSemaphores(signalSems);

        context.queue.submit(submitInfo, *frameFence);

        // Present (wait on renderFinishedSemaphore)
        vk::PresentInfoKHR presentInfo;
        std::array<vk::Semaphore, 1> waitRenderSems = { *renderFinishedSemaphore };
        std::array<vk::SwapchainKHR, 1> swapchains = { *swapchain };
        std::array<uint32_t, 1> imageIndicesArr = { imageIndex };
        presentInfo.setWaitSemaphores(waitRenderSems);
        presentInfo.setSwapchains(swapchains);
        presentInfo.setImageIndices(imageIndicesArr);

        vk::Result presentResult = context.queue.presentKHR(presentInfo);
        if (presentResult != vk::Result::eSuccess && presentResult != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("failed to present.");
        }

        // Wait for frame finish to detect device lost early
        vk::Result fenceRes = context.device->waitForFences(*frameFence, VK_TRUE, UINT64_MAX);
        if (fenceRes == vk::Result::eErrorDeviceLost) {
            throw std::runtime_error("Device lost detected after submit (waitForFences).");
        }

        // Update previous camera matrices for motion vectors
        prevView = matrices.view;
        prevProj = matrices.proj;

        // advance counters
        frame++;
    }

    context.device->waitIdle();
    glfwDestroyWindow(context.window);
    glfwTerminate();

    return EXIT_SUCCESS;
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

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard("FORWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard("BACKWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard("LEFT", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard("RIGHT", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.processKeyboard("UP", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.processKeyboard("DOWN", deltaTime);
}