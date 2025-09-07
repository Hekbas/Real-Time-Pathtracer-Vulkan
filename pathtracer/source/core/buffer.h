#pragma once

#include "context.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

class Buffer {
public:
    enum class Type {
        Scratch,
        AccelInput,
        AccelStorage,
        ShaderBindingTable,
        TransferSrc,
        TransferDst,
        Storage,
        Uniform,
    };

    Buffer() = default;
    Buffer(const Context& context, Type type, vk::DeviceSize size, const void* data = nullptr);

    void* map(const Context& context);
    void unmap(const Context& context);
    void upload(const Context& context, const void* data, size_t size, size_t offset = 0);

    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorBufferInfo descBufferInfo;
    uint64_t deviceAddress = 0;
};