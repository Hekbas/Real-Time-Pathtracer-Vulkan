#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

struct Context;

struct Image {
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

    vk::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorImageInfo descImageInfo;
};