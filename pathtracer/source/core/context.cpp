#include "context.h"
#include "common.h"
#include "rendering/model_loader.h"
#include <stdexcept>
#include <iostream>

Context::Context() {
    // Create window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Pathtracer", nullptr, nullptr);

    // Prepare extensions and layers
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers{ "VK_LAYER_KHRONOS_validation" };

    auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    // Create instance
    vk::ApplicationInfo appInfo;
    appInfo.setApiVersion(VK_API_VERSION_1_4);

    vk::InstanceCreateInfo instanceInfo;
    instanceInfo.setPApplicationInfo(&appInfo);
    instanceInfo.setPEnabledLayerNames(layers);
    instanceInfo.setPEnabledExtensionNames(extensions);
    instance = vk::createInstanceUnique(instanceInfo);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    // Pick first gpu
    physicalDevice = instance->enumeratePhysicalDevices().front();

    // Create debug messenger
    vk::DebugUtilsMessengerCreateInfoEXT messengerInfo;
    messengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    messengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    messengerInfo.setPfnUserCallback(&debugUtilsMessengerCallback);
    messenger = instance->createDebugUtilsMessengerEXTUnique(messengerInfo);

    // Create surface
    VkSurfaceKHR _surface;
    VkResult res = glfwCreateWindowSurface(VkInstance(*instance), window, nullptr, &_surface);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::UniqueSurfaceKHR(vk::SurfaceKHR(_surface), { *instance });

    // Find queue family
    std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (int i = 0; i < queueFamilies.size(); i++) {
        auto supportCompute = queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute;
        auto supportPresent = physicalDevice.getSurfaceSupportKHR(i, *surface);
        if (supportCompute && supportPresent) {
            queueFamilyIndex = i;
        }
    }

    // Create device
    const float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.setQueueFamilyIndex(queueFamilyIndex);
    queueCreateInfo.setQueueCount(1);
    queueCreateInfo.setPQueuePriorities(&queuePriority);

    std::vector<const char*> deviceExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
    };

    if (!checkDeviceExtensionSupport(deviceExtensions)) {
        throw std::runtime_error("Some required extensions are not supported");
    }

    vk::DeviceCreateInfo deviceInfo;
    deviceInfo.setQueueCreateInfos(queueCreateInfo);
    deviceInfo.setPEnabledExtensionNames(deviceExtensions);

    // Existing feature structs
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{ true };
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{ true };
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{ true };

    // Vulkan 1.2 core features required by SPIR-V capabilities
    vk::PhysicalDeviceVulkan12Features features12{};
    features12.setRuntimeDescriptorArray(VK_TRUE);
    features12.setShaderSampledImageArrayNonUniformIndexing(VK_TRUE);

    // Chain them: deviceInfo.pNext -> bufferDeviceAddressFeatures -> rayTracing -> accel -> features12
    deviceInfo.setPNext(&bufferDeviceAddressFeatures);
    bufferDeviceAddressFeatures.setPNext(&rayTracingPipelineFeatures);
    rayTracingPipelineFeatures.setPNext(&accelerationStructureFeatures);
    accelerationStructureFeatures.setPNext(&features12);

    // Create device (recreate device when changing this chain)
    device = physicalDevice.createDeviceUnique(deviceInfo);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);

    queue = device->getQueue(queueFamilyIndex, 0);

    // Create command pool
    vk::CommandPoolCreateInfo commandPoolInfo;
    commandPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    commandPoolInfo.setQueueFamilyIndex(queueFamilyIndex);
    commandPool = device->createCommandPoolUnique(commandPoolInfo);

    // Create descriptor pool
    std::vector<vk::DescriptorPoolSize> poolSizes{
        {vk::DescriptorType::eAccelerationStructureKHR, 1},
        {vk::DescriptorType::eStorageImage, 20},
        {vk::DescriptorType::eStorageBuffer, 3},
        {vk::DescriptorType::eCombinedImageSampler, 100},
        {vk::DescriptorType::eUniformBuffer, 2},
    };

    vk::DescriptorPoolCreateInfo descPoolInfo;
    descPoolInfo.setPoolSizes(poolSizes);
    descPoolInfo.setMaxSets(10);
    descPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    descPool = device->createDescriptorPoolUnique(descPoolInfo);
}

bool Context::checkDeviceExtensionSupport(const std::vector<const char*>& requiredExtensions) const {
    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    for (const auto& required : requiredExtensions) {
        bool found = false;
        for (const auto& available : availableExtensions) {
            if (strcmp(required, available.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

uint32_t Context::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    auto memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

void Context::oneTimeSubmit(const std::function<void(vk::CommandBuffer)>& func) const {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(*commandPool);
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandBufferCount(1);

    auto commandBuffers = device->allocateCommandBuffersUnique(allocInfo);
    auto& commandBuffer = commandBuffers[0];

    commandBuffer->begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    func(*commandBuffer);
    commandBuffer->end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);
    queue.submit(submitInfo);
    queue.waitIdle();
}

vk::UniqueDescriptorSet Context::allocateDescSet(vk::DescriptorSetLayout descSetLayout) {
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*descPool);
    allocInfo.setSetLayouts(descSetLayout);
    return std::move(device->allocateDescriptorSetsUnique(allocInfo)[0]);
}

void Context::createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
    vk::UniqueDescriptorSetLayout& layout) {
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    layout = device->createDescriptorSetLayoutUnique(layoutInfo);
}

void Context::createComputePipeline(const std::string& shaderPath,
    vk::UniquePipelineLayout& pipelineLayout,
    vk::UniquePipeline& pipeline) {
    auto shaderCode = readFile(shaderPath);
    vk::ShaderModuleCreateInfo shaderModuleInfo;
    shaderModuleInfo.setCodeSize(shaderCode.size());
    shaderModuleInfo.setPCode(reinterpret_cast<const uint32_t*>(shaderCode.data()));
    vk::UniqueShaderModule shaderModule = device->createShaderModuleUnique(shaderModuleInfo);

    vk::PipelineShaderStageCreateInfo stageInfo;
    stageInfo.setStage(vk::ShaderStageFlagBits::eCompute);
    stageInfo.setModule(*shaderModule);
    stageInfo.setPName("main");

    vk::ComputePipelineCreateInfo pipelineInfo;
    pipelineInfo.setStage(stageInfo);
    pipelineInfo.setLayout(*pipelineLayout);

    pipeline = device->createComputePipelineUnique(nullptr, pipelineInfo).value;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL Context::debugUtilsMessengerCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
    vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData) {
    std::cerr << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}