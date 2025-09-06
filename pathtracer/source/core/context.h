#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <functional>

struct Context {
    Context();
    bool checkDeviceExtensionSupport(const std::vector<const char*>& requiredExtensions) const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    void oneTimeSubmit(const std::function<void(vk::CommandBuffer)>& func) const;
    vk::UniqueDescriptorSet allocateDescSet(vk::DescriptorSetLayout descSetLayout);
    void createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
        vk::UniqueDescriptorSetLayout& layout);
    void createComputePipeline(const std::string& shaderPath,
        vk::UniquePipelineLayout& pipelineLayout,
        vk::UniquePipeline& pipeline);

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessengerCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
        vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
        void* pUserData);

    GLFWwindow* window;
    vk::detail::DynamicLoader dl;
    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT messenger;
    vk::UniqueSurfaceKHR surface;
    vk::UniqueDevice device;
    vk::PhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex;
    vk::Queue queue;
    vk::UniqueCommandPool commandPool;
    vk::UniqueDescriptorPool descPool;
};