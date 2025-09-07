#pragma once

#include "core/buffer.h"
#include "core/context.h"
#include "math/mat4.h"
#include "render/model.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>

struct Accel {
    Accel() = default;
    Accel(const Context& context, vk::AccelerationStructureGeometryKHR geometry, uint32_t primitiveCount, vk::AccelerationStructureTypeKHR type) {
        vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo;
        buildGeometryInfo.setType(type);
        buildGeometryInfo.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
        buildGeometryInfo.setGeometries(geometry);

        // Create buffer
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo = context.device->getAccelerationStructureBuildSizesKHR(  //
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometryInfo, primitiveCount);
        vk::DeviceSize size = buildSizesInfo.accelerationStructureSize;
        buffer = Buffer{ context, Buffer::Type::AccelStorage, size };

        // Create accel
        vk::AccelerationStructureCreateInfoKHR accelInfo;
        accelInfo.setBuffer(*buffer.buffer);
        accelInfo.setSize(size);
        accelInfo.setType(type);
        accel = context.device->createAccelerationStructureKHRUnique(accelInfo);

        // Build
        Buffer scratchBuffer{ context, Buffer::Type::Scratch, buildSizesInfo.buildScratchSize };
        buildGeometryInfo.setScratchData(scratchBuffer.deviceAddress);
        buildGeometryInfo.setDstAccelerationStructure(*accel);

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {  //
            vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo;
            buildRangeInfo.setPrimitiveCount(primitiveCount);
            buildRangeInfo.setFirstVertex(0);
            buildRangeInfo.setPrimitiveOffset(0);
            buildRangeInfo.setTransformOffset(0);
            commandBuffer.buildAccelerationStructuresKHR(buildGeometryInfo, &buildRangeInfo);
            });

        descAccelInfo.setAccelerationStructures(*accel);
    }

    Buffer buffer;
    vk::UniqueAccelerationStructureKHR accel;
    vk::WriteDescriptorSetAccelerationStructureKHR descAccelInfo;
};