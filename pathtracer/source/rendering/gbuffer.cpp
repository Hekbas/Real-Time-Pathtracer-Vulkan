#include "gbuffer.h"
#include "core/context.h"

GBuffer createGBuffer(const Context& context, vk::Extent2D extent) {
    GBuffer gbuffer;

    gbuffer.position = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc };

    gbuffer.normal = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc };

    gbuffer.albedo = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc };

    gbuffer.motion = Image{ context, extent, vk::Format::eR32G32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc };

    return gbuffer;
}