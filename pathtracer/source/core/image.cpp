#include "image.h"
#include "common.h"
#include "buffer.h"
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
    /*if (finalLayout != vk::ImageLayout::eUndefined) {
        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
            setImageLayout(commandBuffer, *image, vk::ImageLayout::eUndefined, finalLayout);
            });
    }*/
}

vk::AccessFlags Image::toAccessFlags(vk::ImageLayout layout) {
    switch (layout) {
    case vk::ImageLayout::eUndefined:
        return vk::AccessFlags();
    case vk::ImageLayout::eGeneral:
        return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    case vk::ImageLayout::eColorAttachmentOptimal:
        return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        return vk::AccessFlagBits::eDepthStencilAttachmentRead;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::AccessFlagBits::eTransferWrite;
    case vk::ImageLayout::ePreinitialized:
        return vk::AccessFlagBits::eHostWrite;
    case vk::ImageLayout::ePresentSrcKHR:
        return vk::AccessFlagBits::eMemoryRead;
    default:
        return vk::AccessFlags();
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

void Image::transitionLayout(const Context& context, vk::ImageLayout newLayout) {
    context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
        setImageLayout(cmdBuffer, *image, descImageInfo.imageLayout, newLayout);
        });
    descImageInfo.imageLayout = newLayout;
}

void Image::copyFromBuffer(const Context& context, const Buffer& buffer, vk::Extent2D extent) {
    context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
        // Transition image to transfer destination layout
        setImageLayout(cmdBuffer, *image, descImageInfo.imageLayout,
            vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);

        // Copy buffer to image
        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, 0, 1
        );
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

        cmdBuffer.copyBufferToImage(
            *buffer.buffer, *image,
            vk::ImageLayout::eTransferDstOptimal,
            1, &region
        );

        // Transition image back to the original layout
        setImageLayout(cmdBuffer, *image, vk::ImageLayout::eTransferDstOptimal,
            descImageInfo.imageLayout,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader);
        });
}

void Image::copyToBuffer(const Context& context, Buffer& buffer, vk::Extent2D extent) const {
    context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
        // Transition image to transfer source layout
        setImageLayout(cmdBuffer, *image, descImageInfo.imageLayout,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);

        // Copy image to buffer
        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, 0, 1
        );
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

        cmdBuffer.copyImageToBuffer(
            *image, vk::ImageLayout::eTransferSrcOptimal,
            *buffer.buffer,
            1, &region
        );

        // Transition image back to the original layout
        setImageLayout(cmdBuffer, *image, vk::ImageLayout::eTransferSrcOptimal,
            descImageInfo.imageLayout,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader);
        });
}