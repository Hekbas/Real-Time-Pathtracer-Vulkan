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

    // 2. Staging buffer
    Buffer stagingBuffer(context, Buffer::Type::TransferSrc, imageSize, pixels);
    stbi_image_free(pixels); // We can now free the CPU-side pixels

    // 3. Create the destination GPU image
    vk::Extent2D extent = { (uint32_t)texWidth, (uint32_t)texHeight };
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
        cmd.copyBufferToImage(*stagingBuffer.buffer, *gpuImage.image, vk::ImageLayout::eTransferDstOptimal, region);

        // Transition layout to be ready for shader reading
        Image::setImageLayout(cmd, *gpuImage.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
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
    samplerInfo.anisotropyEnable = VK_FALSE;  // requires device feature enabled
    samplerInfo.maxAnisotropy = 1; //context.physicalDevice.getProperties().limits.maxSamplerAnisotropy;
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

Texture createTextureFromMemory(const Context& context, const unsigned char* data, size_t size) {
    // Load image from memory using STB image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size),
        &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load texture from memory");
    }

    // Create a temporary path for the createTexture function
    // We'll use a different approach here
    Texture texture;
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    try {
        Buffer stagingBuffer(context, Buffer::Type::TransferSrc, imageSize, pixels);

        texture.image = Image(
            context,
            vk::Extent2D(static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight)),
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eShaderReadOnlyOptimal
        );

        // Copy from staging buffer to image (same as in createTexture)
        context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
            Image::setImageLayout(
                cmdBuffer,
                *texture.image.image,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer
            );

            vk::BufferImageCopy region;
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor, 0, 0, 1
            );
            region.imageOffset = vk::Offset3D(0, 0, 0);
            region.imageExtent = vk::Extent3D(
                static_cast<uint32_t>(texWidth),
                static_cast<uint32_t>(texHeight),
                1
            );

            cmdBuffer.copyBufferToImage(
                *stagingBuffer.buffer,
                *texture.image.image,
                vk::ImageLayout::eTransferDstOptimal,
                1, &region
            );

            Image::setImageLayout(
                cmdBuffer,
                *texture.image.image,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader
            );
            });

        // Create sampler (same as in createTexture)
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        texture.sampler = context.device->createSamplerUnique(samplerInfo);
        texture.image.descImageInfo.sampler = *texture.sampler;

    }
    catch (const std::exception& e) {
        stbi_image_free(pixels);
        throw e;
    }

    stbi_image_free(pixels);
    return texture;
}

Texture createTextureFromData(const Context& context, uint32_t width, uint32_t height,
    vk::Format format, const void* data) {
    Texture texture;
    vk::DeviceSize imageSize = width * height * 4; // Assuming 4 bytes per pixel

    try {
        Buffer stagingBuffer(context, Buffer::Type::TransferSrc, imageSize, data);

        texture.image = Image(
            context,
            vk::Extent2D(width, height),
            format,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eShaderReadOnlyOptimal
        );

        // Copy from staging buffer to image (same as before)
        context.oneTimeSubmit([&](vk::CommandBuffer cmdBuffer) {
            Image::setImageLayout(
                cmdBuffer,
                *texture.image.image,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer
            );

            vk::BufferImageCopy region;
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor, 0, 0, 1
            );
            region.imageOffset = vk::Offset3D(0, 0, 0);
            region.imageExtent = vk::Extent3D(width, height, 1);

            cmdBuffer.copyBufferToImage(
                *stagingBuffer.buffer,
                *texture.image.image,
                vk::ImageLayout::eTransferDstOptimal,
                1, &region
            );

            Image::setImageLayout(
                cmdBuffer,
                *texture.image.image,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader
            );
            });

        // Create sampler (same as before)
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        texture.sampler = context.device->createSamplerUnique(samplerInfo);
        texture.image.descImageInfo.sampler = *texture.sampler;

    }
    catch (const std::exception& e) {
        throw e;
    }

    return texture;
}

Texture createEmptyTexture(const Context& context, uint32_t width, uint32_t height,
    vk::Format format, vk::ImageUsageFlags usage) {
    Texture texture;

    try {
        texture.image = Image(
            context,
            vk::Extent2D(width, height),
            format,
            usage,
            vk::ImageLayout::eShaderReadOnlyOptimal
        );

        // Create sampler
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        texture.sampler = context.device->createSamplerUnique(samplerInfo);
        texture.image.descImageInfo.sampler = *texture.sampler;

    }
    catch (const std::exception& e) {
        throw e;
    }

    return texture;
}