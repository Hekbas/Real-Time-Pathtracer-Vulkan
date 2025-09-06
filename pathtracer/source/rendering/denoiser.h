#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "core/image.h"
#include "core/buffer.h"

struct DenoiserSettings {
    int temporalAccumulation = 1;
    int atrousIterations = 5;
    float phiColor = 10.0f;
    float phiNormal = 128.0f;
    float phiDepth = 128.0f;
};

struct Denoiser {
    Image historyColor[2];
    Image historyMoments[2];
    Image historyNormal[2];
    Image historyDepth[2];

    Image intensity;
    Image variance;
    Image filtered;

    Buffer settingsBuffer;

    int frameCount = 0;
    int historyIndex = 0;

    vk::UniquePipeline pipeline;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniqueDescriptorSetLayout descSetLayout;
    vk::UniqueDescriptorSet descSet;
};

Denoiser createDenoiser(Context& context, vk::Extent2D extent);