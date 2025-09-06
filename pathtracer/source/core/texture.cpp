#include "texture.h"
#include "context.h"
#include "buffer.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stdexcept>

Texture createTexture(const Context& context, const std::string& path) {
    // 1. Load image pixels from file using stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    // 2. Use Buffer struct to create a staging buffer
    Buffer stagingBuffer(context, Buffer::Type::TransferSrc, imageSize, pixels);
    stbi_image_free(pixels); // Free the CPU-side pixels

    // 3. Create the destination GPU image
    vk::Extent2D extent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight) };
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

    // We pass eUndefined to prevent the automatic layout transition
    Image gpuImage(context, extent, vk::Format::eR8G8B8A8Srgb, usage, vk::ImageLayout::eUndefined);

    // 4. Copy data from staging buffer to GPU image and set the correct layout for shaders
    context.oneTimeSubmit([&](vk::CommandBuffer cmd) {
        // Transition layout to be ready for copying
        Image::setImageLayout(cmd, *gpuImage.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

        // Copy the data
        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

        cmd.copyBufferToImage(*stagingBuffer.buffer, *gpuImage.image,
            vk::ImageLayout::eTransferDstOptimal, region);

        // Transition layout to be ready for shader reading
        Image::setImageLayout(cmd, *gpuImage.image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);
        });

    // 5. Create a texture sampler
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    vk::UniqueSampler sampler = context.device->createSamplerUnique(samplerInfo);

    // 6. Return the completed texture object
    return { std::move(gpuImage), std::move(sampler) };
}