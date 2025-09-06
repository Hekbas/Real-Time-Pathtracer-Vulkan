#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "context.h"

struct Buffer {
    enum class Type {
        Scratch,
        AccelInput,
        AccelStorage,
        ShaderBindingTable,
        TransferSrc,
        Storage,
        Uniform,
    };

    Buffer() = default;
    Buffer(const Context& context, Type type, vk::DeviceSize size, const void* data = nullptr);

    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorBufferInfo descBufferInfo;
    uint64_t deviceAddress = 0;
};