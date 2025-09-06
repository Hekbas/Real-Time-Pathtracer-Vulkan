#pragma once
#include "core/image.h"

struct GBuffer {
    Image position;
    Image normal;
    Image albedo;
    Image motion;
};

GBuffer createGBuffer(const Context& context, vk::Extent2D extent);