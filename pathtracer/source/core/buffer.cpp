#include "buffer.h"
#include <cstring>

Buffer::Buffer(const Context& context, Type type, vk::DeviceSize size, const void* data) {
    vk::BufferUsageFlags usageFlags;
    vk::MemoryPropertyFlags memoryFlags;

    // Determine buffer usage and memory properties based on type
    switch (type) {
    case Type::Scratch:
        usageFlags = vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case Type::AccelInput:
        usageFlags = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        break;

    case Type::AccelStorage:
        usageFlags = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case Type::ShaderBindingTable:
        usageFlags = vk::BufferUsageFlagBits::eShaderBindingTableKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        break;

    case Type::TransferSrc:
        usageFlags = vk::BufferUsageFlagBits::eTransferSrc;
        memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        break;

    case Type::TransferDst:
        usageFlags = vk::BufferUsageFlagBits::eTransferDst;
        memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case Type::Storage:
        usageFlags = vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case Type::Uniform:
        usageFlags = vk::BufferUsageFlagBits::eUniformBuffer;
        memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        break;
    }

    // Create buffer
    vk::BufferCreateInfo bufferInfo({}, size, usageFlags, vk::SharingMode::eExclusive);
    buffer = context.device->createBufferUnique(bufferInfo);

    // Get memory requirements
    vk::MemoryRequirements memRequirements = context.device->getBufferMemoryRequirements(*buffer);

    // Allocate memory
    vk::MemoryAllocateInfo allocInfo(memRequirements.size,
        context.findMemoryType(memRequirements.memoryTypeBits, memoryFlags));

    // For buffers that need device address, add the flag
    if (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        vk::MemoryAllocateFlagsInfo flagsInfo(vk::MemoryAllocateFlagBits::eDeviceAddress);
        allocInfo.pNext = &flagsInfo;
    }

    memory = context.device->allocateMemoryUnique(allocInfo);

    // Bind memory to buffer
    context.device->bindBufferMemory(*buffer, *memory, 0);

    // Set up descriptor buffer info
    descBufferInfo.buffer = *buffer;
    descBufferInfo.offset = 0;
    descBufferInfo.range = size;

    // Get device address for buffers that need it
    if (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        vk::BufferDeviceAddressInfo addressInfo(*buffer);
        deviceAddress = context.device->getBufferAddressKHR(addressInfo);
    }

    // If data is provided, copy it to the buffer
    if (data != nullptr) {
        // For device local buffers, we need to use a staging buffer
        if (memoryFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) {
            // Create a staging buffer
            Buffer staging(context, Type::TransferSrc, size, data);

            // Copy from staging to device local buffer
            context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
                vk::BufferCopy copyRegion(0, 0, size);
                cmdBuffer.copyBuffer(*staging.buffer, *buffer, copyRegion);
                });
        }
        else {
            // For host visible buffers, map memory and copy directly
            void* mappedData = context.device->mapMemory(*memory, 0, size);
            memcpy(mappedData, data, size);
            context.device->unmapMemory(*memory);
        }
    }
}

void* Buffer::map(const Context& context) {
    return context.device->mapMemory(*memory, 0, descBufferInfo.range);
}

void Buffer::unmap(const Context& context) {
    context.device->unmapMemory(*memory);
}

void Buffer::upload(const Context& context, const void* data, size_t size, size_t offset) {
    void* mappedData = context.device->mapMemory(*memory, offset, size);
    memcpy(mappedData, data, size);
    context.device->unmapMemory(*memory);
}