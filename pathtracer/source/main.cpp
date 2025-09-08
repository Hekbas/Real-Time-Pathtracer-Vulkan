#include "common.h"
#include "core/context.h"
#include "core/texture.h"
#include "core/accel.h"
#include "math/math_utils.h"
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

std::string MODEL_TO_LOAD = "sponza/scene.gltf";

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
    swapchainInfo.setImageExtent({WIDTH, HEIGHT});
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

    // Load model
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<std::string> textureFiles;
    std::vector<uint32_t> materialIndices;
    std::string modelPath = "../assets/models/" + MODEL_TO_LOAD;

    std::cout << "Loading model: " << modelPath << std::endl;
    loadFromFile(vertices, indices, materials, textureFiles, materialIndices, modelPath);

    // Model loading validation
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("No vertices or indices loaded from model");
    }

    std::cout << "Loaded " << vertices.size() << " vertices, "
        << indices.size() << " indices, "
        << materials.size() << " materials, "
        << textureFiles.size() << " textures" << std::endl;

    // quick runtime sanity check
    if (materialIndices.size() != (indices.size() / 3)) {
        throw std::runtime_error("materialIndices size mismatch: expected indices.size()/3");
    }

	// Load textures
    std::vector<Texture> textures;
    textures.reserve(textureFiles.size());
    for (const auto& filePath : textureFiles) {
        textures.push_back(createTexture(context, filePath));
    }

    Buffer vertexBuffer{ context, Buffer::Type::AccelInput, sizeof(Vertex) * vertices.size(), vertices.data() };
    Buffer indexBuffer{ context, Buffer::Type::AccelInput, sizeof(uint32_t) * indices.size(), indices.data() };

    // material array (one Material struct per material)
    Buffer materialBuffer{ context, Buffer::Type::AccelInput, sizeof(Material) * materials.size(), materials.data() };

    // per-triangle material index buffer (one uint32 per triangle)
    Buffer materialIndexBuffer{ context, Buffer::Type::AccelInput, sizeof(uint32_t) * materialIndices.size(), materialIndices.data() };


    // Create bottom level accel struct
    vk::AccelerationStructureGeometryTrianglesDataKHR triangleData;
    triangleData.setVertexFormat(vk::Format::eR32G32B32Sfloat);
    triangleData.setVertexData(vertexBuffer.deviceAddress);
    triangleData.setVertexStride(sizeof(Vertex));
    triangleData.setMaxVertex(static_cast<uint32_t>(vertices.size()));
    triangleData.setIndexType(vk::IndexType::eUint32);
    triangleData.setIndexData(indexBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR triangleGeometry;
    triangleGeometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
    triangleGeometry.setGeometry({triangleData});
    triangleGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    const auto primitiveCount = static_cast<uint32_t>(indices.size() / 3);

    Accel bottomAccel{context, triangleGeometry, primitiveCount, vk::AccelerationStructureTypeKHR::eBottomLevel};

    // Create top level accel struct
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

    // Acceleration structure validation
    if (bottomAccel.buffer.deviceAddress == 0) {
        throw std::runtime_error("BLAS device address is zero");
    }

    if (accelInstance.accelerationStructureReference == 0) {
        throw std::runtime_error("Instance acceleration structure reference is zero");
    }

    Buffer instancesBuffer{context, Buffer::Type::AccelInput, sizeof(vk::AccelerationStructureInstanceKHR), &accelInstance};

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData;
    instancesData.setArrayOfPointers(false);
    instancesData.setData(instancesBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR instanceGeometry;
    instanceGeometry.setGeometryType(vk::GeometryTypeKHR::eInstances);
    instanceGeometry.setGeometry({instancesData});
    instanceGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    Accel topAccel{context, instanceGeometry, 1, vk::AccelerationStructureTypeKHR::eTopLevel};

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
    shaderModules[0] = context.device->createShaderModuleUnique({{}, raygenCode.size(), reinterpret_cast<const uint32_t*>(raygenCode.data())});
    shaderModules[1] = context.device->createShaderModuleUnique({{}, missCode.size(), reinterpret_cast<const uint32_t*>(missCode.data())});
    shaderModules[2] = context.device->createShaderModuleUnique({{}, chitCode.size(), reinterpret_cast<const uint32_t*>(chitCode.data())});

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages(3);
    shaderStages[0] = {{}, vk::ShaderStageFlagBits::eRaygenKHR, *shaderModules[0], "main"};
    shaderStages[1] = {{}, vk::ShaderStageFlagBits::eMissKHR, *shaderModules[1], "main"};
    shaderStages[2] = {{}, vk::ShaderStageFlagBits::eClosestHitKHR, *shaderModules[2], "main"};

    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
    shaderGroups[0] = {vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
    shaderGroups[1] = {vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
    shaderGroups[2] = {vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, 2, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};

    // Note: Ensure your device supports enough samplers. Sponza has ~50 textures.
    // A size of 0 is invalid, so handle the no-texture case.
    const uint32_t textureCount = textures.empty() ? 1u : static_cast<uint32_t>(textures.size());

    // create ray tracing pipeline
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        {0, vk::DescriptorType::eAccelerationStructureKHR, 1,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR}, // 0: TLAS
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR},             // 1: Storage image (output)
        {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},        // 2: vertices
        {3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},        // 3: indices
        {4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},        // 4: per-triangle material indices (uint per triangle)
        {5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},        // 5: material array (Material[])
        {6, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eClosestHitKHR},// 6: textures (combined image sampler array)
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
    Buffer raygenSBT{ context, Buffer::Type::ShaderBindingTable, handleSizeAligned, handleStorage.data() + 0 * handleSizeAligned };
    Buffer missSBT{ context, Buffer::Type::ShaderBindingTable, handleSizeAligned, handleStorage.data() + 1 * handleSizeAligned };
    Buffer hitSBT{ context, Buffer::Type::ShaderBindingTable, handleSizeAligned, handleStorage.data() + 2 * handleSizeAligned };

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

    vk::StridedDeviceAddressRegionKHR raygenRegion{raygenSBT.deviceAddress, stride, size};
    vk::StridedDeviceAddressRegionKHR missRegion{missSBT.deviceAddress, stride, size};
    vk::StridedDeviceAddressRegionKHR hitRegion{hitSBT.deviceAddress, stride, size};

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
        Image dummyImage {
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
    writes.resize(7);

    // 0: TLAS
    writes[0].setDstSet(*descSet);
    writes[0].setDstBinding(0);
    writes[0].setDescriptorCount(1);
    writes[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    writes[0].setPNext(&topAccel.descAccelInfo);

    // 1: storage image
    writes[1].setDstSet(*descSet);
    writes[1].setDstBinding(1);
    writes[1].setDescriptorCount(1);
    writes[1].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[1].setImageInfo(outputImage.descImageInfo);

    // 2: vertices buffer
    writes[2].setDstSet(*descSet);
    writes[2].setDstBinding(2);
    writes[2].setDescriptorCount(1);
    writes[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[2].setBufferInfo(vertexBuffer.descBufferInfo);

    // 3: indices buffer
    writes[3].setDstSet(*descSet);
    writes[3].setDstBinding(3);
    writes[3].setDescriptorCount(1);
    writes[3].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[3].setBufferInfo(indexBuffer.descBufferInfo);

    // 4: per-triangle material index buffer
    writes[4].setDstSet(*descSet);
    writes[4].setDstBinding(4);
    writes[4].setDescriptorCount(1);
    writes[4].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[4].setBufferInfo(materialIndexBuffer.descBufferInfo);

    // 5: material array buffer
    writes[5].setDstSet(*descSet);
    writes[5].setDstBinding(5);
    writes[5].setDescriptorCount(1);
    writes[5].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[5].setBufferInfo(materialBuffer.descBufferInfo);

    // 6: textures
    writes[6].setDstSet(*descSet);
    writes[6].setDstBinding(6);
    writes[6].setDescriptorCount(textureCount);
    writes[6].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
    writes[6].setImageInfo(imageInfos);

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

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard("FORWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard("BACKWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard("LEFT", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard("RIGHT", deltaTime);
}