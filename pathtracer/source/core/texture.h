#pragma once
#include "image.h"
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

struct Context;

struct Texture {
    Image image;
    vk::UniqueSampler sampler;
};

Texture createTexture(const Context& context, const std::string& path);