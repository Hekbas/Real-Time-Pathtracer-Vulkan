#pragma once

#include "math/vec3.h"

#include <vector>
#include <string>

const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;

struct Vertex {
    Vec3 position;
    Vec3 normal;
    float texCoord[2];
    Vec3 tangent;

    bool operator==(const Vertex& other) const {
        return position == other.position &&
            normal == other.normal &&
            texCoord == other.texCoord && 
			tangent == other.tangent;
    }
};

struct Material {
    Vec3 albedo;
    Vec3 emission;
    int  diffuseTextureID;
    int  material_type;
    float roughness;
    float ior;
    float metallic;
    float alpha;
    int  metalRoughTextureID;
    int  normalTextureID;
    float pad0;
    float pad1;
};

void loadFromFile(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Material>& materials,
    std::vector<uint32_t>& faceMaterialIndices,
    std::vector<std::string>& textureFiles,
    const std::string& modelPath
);

std::vector<char> readFile(const std::string& filename);

// Hash support
namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(const Vertex& v) const noexcept {
            size_t h1 = hash<float>()(v.position.x) ^ (hash<float>()(v.position.y) << 1) ^ (hash<float>()(v.position.z) << 2);
            size_t h2 = hash<float>()(v.normal.x) ^ (hash<float>()(v.normal.y) << 1) ^ (hash<float>()(v.normal.z) << 2);
            size_t h3 = hash<float>()(v.texCoord[0]) ^ (hash<float>()(v.texCoord[1]) << 1);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}