#pragma once

#include "math/vec3.h"
#include "math/vec4.h"

#include <vector>
#include <string>

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec3 tangent;
    float uv[2];
};

struct Material {
    Vec4 baseColorFactor;   // (r,g,b,a)
    int baseColorTex;       // texture index or -1
    int metallicRoughnessTex;
    int emissiveTex;
    int normalTex;
    int occlusionTex;
    int doubleSided;        // 0 or 1
    int alphaMode;          // 0 opaque, 1 mask, 2 blend
    float metallicFactor;
    float roughnessFactor;
    // pad to 16 bytes (std430)
    float _pad0;
    float _pad1;
    Vec4 emissiveFactor;    // (r,g,b,unused)
};

void loadFromFile(
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Material>& outMaterials,
    std::vector<std::string>& outTextureFiles,
    std::vector<uint32_t>& outMaterialIndexPerTriangle,
    const std::string& filename);

std::vector<char> readFile(const std::string& filename);