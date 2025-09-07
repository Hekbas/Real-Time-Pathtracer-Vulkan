#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

class Context;
class Buffer;

class Image {
public:
    Image() = default;
    Image(const Context& context,
        vk::Extent2D extent,
        vk::Format format,
        vk::ImageUsageFlags usage,
        vk::ImageLayout finalLayout = vk::ImageLayout::eGeneral);

    static vk::AccessFlags toAccessFlags(vk::ImageLayout layout);
    static void setImageLayout(
        vk::CommandBuffer cmdbuffer,
        vk::Image image,
        vk::ImageLayout oldImageLayout,
        vk::ImageLayout newImageLayout,
        vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eAllCommands,
        vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eAllCommands);
    static void copyImage(vk::CommandBuffer commandBuffer, vk::Image srcImage, vk::Image dstImage);

    void transitionLayout(const Context& context, vk::ImageLayout newLayout);
    void copyFromBuffer(const Context& context, const Buffer& buffer, vk::Extent2D extent);
    void copyToBuffer(const Context& context, Buffer& buffer, vk::Extent2D extent) const;

    vk::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorImageInfo descImageInfo;
};