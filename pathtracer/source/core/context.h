#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <functional>

class Context {
public:
    Context();

    // Instance and device setup
    void initDevice(GLFWwindow* window);
    std::vector<const char*> getRequiredInstanceExtensions();
    bool checkDeviceExtensionSupport(const vk::PhysicalDevice& device,
        const std::vector<const char*>& requiredExtensions) const;

    // Memory and command utilities
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    void oneTimeSubmit(const std::function<void(vk::CommandBuffer)>& func) const;

    // Descriptor and pipeline helpers
    vk::UniqueDescriptorSet allocateDescSet(vk::DescriptorSetLayout descSetLayout);
    void createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
        vk::UniqueDescriptorSetLayout& layout);
    void createComputePipeline(const std::string& shaderPath,
        vk::UniquePipelineLayout& pipelineLayout,
        vk::UniquePipeline& pipeline);

    // Debug callback
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessengerCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
        vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
        void* pUserData);

    // Vulkan handles
    vk::detail::DynamicLoader dl;
    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT messenger;
    vk::UniqueSurfaceKHR surface;

    vk::PhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex = -1;

    vk::UniqueDevice device;
    vk::Queue queue;
    vk::UniqueCommandPool commandPool;
    vk::UniqueDescriptorPool descPool;
};