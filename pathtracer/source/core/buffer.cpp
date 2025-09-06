#include "buffer.h"
#include <cstring>

Buffer::Buffer(const Context& context, Type type, vk::DeviceSize size, const void* data) {
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags memoryProps;

    if (type == Type::AccelInput) {
        usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }
    else if (type == Type::Scratch) {
        usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryProps = vk::MemoryPropertyFlagBits::eDeviceLocal;
    }
    else if (type == Type::AccelStorage) {
        usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryProps = vk::MemoryPropertyFlagBits::eDeviceLocal;
    }
    else if (type == Type::ShaderBindingTable) {
        usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }
    else if (type == Type::TransferSrc) {
        usage = vk::BufferUsageFlagBits::eTransferSrc;
        memoryProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }
    else if (type == Type::Storage) {
        usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        memoryProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }
    else if (type == Type::Uniform) {
        usage = vk::BufferUsageFlagBits::eUniformBuffer;
        memoryProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }

    vk::BufferCreateInfo bufferInfo;
    bufferInfo.setSize(size);
    bufferInfo.setUsage(usage);
    bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
    buffer = context.device->createBufferUnique(bufferInfo);

    // Allocate memory
    vk::MemoryRequirements requirements = context.device->getBufferMemoryRequirements(*buffer);
    uint32_t memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, memoryProps);

    vk::MemoryAllocateFlagsInfo flagsInfo{};
    if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        flagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
    }

    vk::MemoryAllocateInfo memoryInfo;
    memoryInfo.setAllocationSize(requirements.size);
    memoryInfo.setMemoryTypeIndex(memoryTypeIndex);
    if (flagsInfo.flags) {
        memoryInfo.setPNext(&flagsInfo);
    }
    memory = context.device->allocateMemoryUnique(memoryInfo);

    context.device->bindBufferMemory(*buffer, *memory, 0);

    // Get device address if needed
    if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        vk::BufferDeviceAddressInfoKHR bufferDeviceAI{ *buffer };
        deviceAddress = context.device->getBufferAddressKHR(bufferDeviceAI);
    }
    else {
        deviceAddress = 0;
    }

    descBufferInfo.setBuffer(*buffer);
    descBufferInfo.setOffset(0);
    descBufferInfo.setRange(size);

    if (data) {
        void* mapped = context.device->mapMemory(*memory, 0, size);
        memcpy(mapped, data, size);
        context.device->unmapMemory(*memory);
    }
}