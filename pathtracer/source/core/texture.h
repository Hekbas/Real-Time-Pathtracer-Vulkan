#pragma once

#include "image.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

class Context;

struct Texture {
    Image image;
    vk::UniqueSampler sampler;
};

Texture createTexture(const Context& context, const std::string& path);

Texture createTextureFromMemory(const Context& context, const unsigned char* data, size_t size);
Texture createTextureFromData(const Context& context, uint32_t width, uint32_t height,
    vk::Format format, const void* data);
Texture createEmptyTexture(const Context& context, uint32_t width, uint32_t height,
    vk::Format format, vk::ImageUsageFlags usage);