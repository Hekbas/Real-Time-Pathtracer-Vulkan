#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "core/buffer.h"
#include "core/context.h"

struct Accel {
    Accel() = default;
    Accel(const Context& context, vk::AccelerationStructureGeometryKHR geometry,
        uint32_t primitiveCount, vk::AccelerationStructureTypeKHR type);

    Buffer buffer;
    vk::UniqueAccelerationStructureKHR accel;
    vk::WriteDescriptorSetAccelerationStructureKHR descAccelInfo;
};