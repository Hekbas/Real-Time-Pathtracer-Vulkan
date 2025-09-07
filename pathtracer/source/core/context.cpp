#include "context.h"
#include "common.h"
#include "render/model_loader.h"

#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <set>

// Validation layers
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// Required device extensions
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
};

Context::Context() {
    // Initialize Vulkan dynamic loader
    dl = vk::detail::DynamicLoader();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));

    // Create Vulkan instance
    vk::ApplicationInfo appInfo("Vulkan Path Tracer", 1, "No Engine", 1, VK_API_VERSION_1_2);

    auto extensions = getRequiredInstanceExtensions();

    vk::InstanceCreateInfo instanceInfo({}, &appInfo);
    instanceInfo.setPEnabledExtensionNames(extensions);

    if (enableValidationLayers) {
        instanceInfo.setPEnabledLayerNames(validationLayers);
    }

    instance = vk::createInstanceUnique(instanceInfo);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    // Setup debug messenger (if validation layers are enabled)
    if (enableValidationLayers) {
        vk::DebugUtilsMessengerCreateInfoEXT debugInfo;
        debugInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        debugInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debugInfo.pfnUserCallback = debugUtilsMessengerCallback;

        messenger = instance->createDebugUtilsMessengerEXTUnique(debugInfo);
    }

    // Note: Surface creation is removed from here - it will be handled separately after window creation
}

std::vector<const char*> Context::getRequiredInstanceExtensions() {
    // This will be called by GLFW to get required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Add other required extensions
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    return extensions;
}

// Add a new method to initialize device with a surface
void Context::initDevice(GLFWwindow* window) {
    // Create surface
    VkSurfaceKHR surfaceRaw;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &surfaceRaw) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
    surface = vk::UniqueSurfaceKHR(vk::SurfaceKHR(surfaceRaw), *instance);

    // Select physical device
    auto physicalDevices = instance->enumeratePhysicalDevices();
    if (physicalDevices.empty()) {
        throw std::runtime_error("No Vulkan-capable devices found");
    }

    // Select the first suitable device
    for (const auto& device : physicalDevices) {
        if (checkDeviceExtensionSupport(device, deviceExtensions)) {
            physicalDevice = device;
            break;
        }
    }

    if (!physicalDevice) {
        throw std::runtime_error("No suitable physical device found");
    }

    // Find queue family
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            // Check if this queue family supports presentation
            if (physicalDevice.getSurfaceSupportKHR(i, *surface)) {
                queueFamilyIndex = i;
                break;
            }
        }
    }

    if (queueFamilyIndex == -1) {
        throw std::runtime_error("No suitable queue family found");
    }

    // Create logical device
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, queueFamilyIndex, 1, &queuePriority);

    // Enable required features
    vk::PhysicalDeviceFeatures2 deviceFeatures2;
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures(true);
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures(true);
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures(true);

    // Chain the feature structures
    deviceFeatures2.pNext = &bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.pNext = &accelerationStructureFeatures;
    accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;

    vk::DeviceCreateInfo deviceInfo({}, queueInfo);
    deviceInfo.setPEnabledExtensionNames(deviceExtensions);
    deviceInfo.pNext = &deviceFeatures2;

    if (enableValidationLayers) {
        deviceInfo.setPEnabledLayerNames(validationLayers);
    }

    device = physicalDevice.createDeviceUnique(deviceInfo);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);

    // Get queue
    queue = device->getQueue(queueFamilyIndex, 0);

    // Create command pool
    vk::CommandPoolCreateInfo cmdPoolInfo({}, queueFamilyIndex);
    commandPool = device->createCommandPoolUnique(cmdPoolInfo);

    // Create descriptor pool
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eAccelerationStructureKHR, 10},
        {vk::DescriptorType::eStorageImage, 10},
        {vk::DescriptorType::eUniformBuffer, 10},
        {vk::DescriptorType::eStorageBuffer, 10},
        {vk::DescriptorType::eCombinedImageSampler, 10}
    };

    vk::DescriptorPoolCreateInfo descPoolInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        100, // max sets
        poolSizes
    );

    descPool = device->createDescriptorPoolUnique(descPoolInfo);
}

bool Context::checkDeviceExtensionSupport(const vk::PhysicalDevice& device,
    const std::vector<const char*>& requiredExtensions) const {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();

    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());

    for (const auto& extension : availableExtensions) {
        required.erase(extension.extensionName);
    }

    return required.empty();
}

uint32_t Context::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void Context::oneTimeSubmit(const std::function<void(vk::CommandBuffer)>& func) const {
    // Allocate command buffer
    vk::CommandBufferAllocateInfo allocInfo(*commandPool, vk::CommandBufferLevel::ePrimary, 1);
    auto commandBuffers = device->allocateCommandBuffersUnique(allocInfo);

    // Begin command buffer
    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffers[0]->begin(beginInfo);
    func(*commandBuffers[0]);
    commandBuffers[0]->end();

    // Submit to queue
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffers[0]);
    vk::FenceCreateInfo fenceInfo;
    auto fence = device->createFenceUnique(fenceInfo);
    queue.submit(submitInfo, *fence);

    // Wait for completion
    device->waitForFences(*fence, VK_TRUE, UINT64_MAX);
}

vk::UniqueDescriptorSet Context::allocateDescSet(vk::DescriptorSetLayout descSetLayout) {
    vk::DescriptorSetAllocateInfo allocInfo(*descPool, 1, &descSetLayout);
    return std::move(device->allocateDescriptorSetsUnique(allocInfo)[0]);
}

void Context::createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
    vk::UniqueDescriptorSetLayout& layout) {
    vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
    layout = device->createDescriptorSetLayoutUnique(layoutInfo);
}

void Context::createComputePipeline(const std::string& shaderPath,
    vk::UniquePipelineLayout& pipelineLayout,
    vk::UniquePipeline& pipeline) {
    // Read shader code
    std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + shaderPath);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> shaderCode(fileSize);

    file.seekg(0);
    file.read(shaderCode.data(), fileSize);
    file.close();

    // Create shader module
    vk::ShaderModuleCreateInfo shaderModuleInfo({}, shaderCode.size(),
        reinterpret_cast<const uint32_t*>(shaderCode.data()));
    auto shaderModule = device->createShaderModuleUnique(shaderModuleInfo);

    // Create pipeline layout (if not provided)
    if (!pipelineLayout) {
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutInfo);
    }

    // Create compute pipeline
    vk::PipelineShaderStageCreateInfo shaderStageInfo(
        {},
        vk::ShaderStageFlagBits::eCompute,
        *shaderModule,
        "main"
    );

    vk::ComputePipelineCreateInfo pipelineInfo({}, shaderStageInfo, *pipelineLayout);
    pipeline = device->createComputePipelineUnique(nullptr, pipelineInfo).value;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL Context::debugUtilsMessengerCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
    vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData) {

    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}