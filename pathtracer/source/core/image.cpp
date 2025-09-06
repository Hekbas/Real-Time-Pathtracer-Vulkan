#include "image.h"
#include "common.h"
#include "context.h"
#include <stdexcept>

Image::Image(const Context& context,
    vk::Extent2D extent,
    vk::Format format,
    vk::ImageUsageFlags usage,
    vk::ImageLayout finalLayout) {

    // Create image
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent({ extent.width, extent.height, 1 });
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
    imageInfo.setFormat(format);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(usage);
    imageInfo.setSharingMode(vk::SharingMode::eExclusive);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);

    image = context.device->createImageUnique(imageInfo);

    // Allocate memory
    vk::MemoryRequirements requirements = context.device->getImageMemoryRequirements(*image);
    uint32_t memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::MemoryAllocateInfo memoryInfo;
    memoryInfo.setAllocationSize(requirements.size);
    memoryInfo.setMemoryTypeIndex(memoryTypeIndex);
    memory = context.device->allocateMemoryUnique(memoryInfo);

    // Bind memory and image
    context.device->bindImageMemory(*image, *memory, 0);

    // Create image view
    vk::ImageViewCreateInfo imageViewInfo;
    imageViewInfo.setImage(*image);
    imageViewInfo.setViewType(vk::ImageViewType::e2D);
    imageViewInfo.setFormat(format);
    imageViewInfo.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
    view = context.device->createImageViewUnique(imageViewInfo);

    // Set image info
    descImageInfo.setImageView(*view);
    descImageInfo.setImageLayout(finalLayout);

    // Only transition the layout if a specific final layout is requested
    if (finalLayout != vk::ImageLayout::eUndefined) {
        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            setImageLayout(commandBuffer, *image, vk::ImageLayout::eUndefined, finalLayout);
            });
    }
}

vk::AccessFlags Image::toAccessFlags(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::AccessFlagBits::eTransferWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eGeneral:
        return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    default:
        return {};
    }
}

void Image::setImageLayout(
    vk::CommandBuffer cmdbuffer,
    vk::Image image,
    vk::ImageLayout oldImageLayout,
    vk::ImageLayout newImageLayout,
    vk::PipelineStageFlags srcStageMask,
    vk::PipelineStageFlags dstStageMask)
{
    vk::ImageMemoryBarrier imageMemoryBarrier;
    imageMemoryBarrier.setOldLayout(oldImageLayout);
    imageMemoryBarrier.setNewLayout(newImageLayout);
    imageMemoryBarrier.setImage(image);
    imageMemoryBarrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    // Source layouts
    switch (oldImageLayout) {
    case vk::ImageLayout::eUndefined:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlags();
        break;
    case vk::ImageLayout::ePreinitialized:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
        break;
    case vk::ImageLayout::eGeneral:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    default:
        break;
    }

    // Destination layouts
    switch (newImageLayout) {
    case vk::ImageLayout::eTransferDstOptimal:
        imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::eGeneral:
        imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    case vk::ImageLayout::ePresentSrcKHR:
        imageMemoryBarrier.dstAccessMask = vk::AccessFlags();
        break;
    default:
        break;
    }

    cmdbuffer.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);
}

void Image::copyImage(vk::CommandBuffer commandBuffer, vk::Image srcImage, vk::Image dstImage) {
    vk::ImageCopy copyRegion;
    copyRegion.setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 });
    copyRegion.setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 });
    copyRegion.setExtent({ WIDTH, HEIGHT, 1 });
    commandBuffer.copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal,
        dstImage, vk::ImageLayout::eTransferDstOptimal, copyRegion);
}