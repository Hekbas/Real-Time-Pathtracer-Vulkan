#include "accel.h"
#include <stdexcept>

Accel::Accel(const Context& context, vk::AccelerationStructureGeometryKHR geometry,
    uint32_t primitiveCount, vk::AccelerationStructureTypeKHR type) {

    vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo;
    buildGeometryInfo.setType(type);
    buildGeometryInfo.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
    buildGeometryInfo.setGeometryCount(1);
    buildGeometryInfo.setPGeometries(&geometry);
    buildGeometryInfo.setMode(vk::BuildAccelerationStructureModeKHR::eBuild);

    // Get build sizes
    vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
        context.device->getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            buildGeometryInfo,
            primitiveCount);

    vk::DeviceSize size = buildSizesInfo.accelerationStructureSize;
    buffer = Buffer{ context, Buffer::Type::AccelStorage, size };

    // Create acceleration structure
    vk::AccelerationStructureCreateInfoKHR accelInfo;
    accelInfo.setBuffer(*buffer.buffer);
    accelInfo.setSize(size);
    accelInfo.setType(type);
    accel = context.device->createAccelerationStructureKHRUnique(accelInfo);

    // Build acceleration structure
    Buffer scratchBuffer{ context, Buffer::Type::Scratch, buildSizesInfo.buildScratchSize };
    buildGeometryInfo.setDstAccelerationStructure(*accel);
    buildGeometryInfo.setScratchData(scratchBuffer.deviceAddress);

    context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
        vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo;
        buildRangeInfo.setPrimitiveCount(primitiveCount);
        buildRangeInfo.setPrimitiveOffset(0);
        buildRangeInfo.setFirstVertex(0);
        buildRangeInfo.setTransformOffset(0);

        const vk::AccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
        commandBuffer.buildAccelerationStructuresKHR(1, &buildGeometryInfo, &pBuildRangeInfo);
        });

    descAccelInfo.setAccelerationStructureCount(1);
    descAccelInfo.setPAccelerationStructures(&(*accel));
}