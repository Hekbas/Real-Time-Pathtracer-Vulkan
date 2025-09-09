#pragma once

#include "math/vec3.h"

#include <vector>
#include <string>

const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;

struct Vertex {
    Vec3 position;      // 0..2
    Vec3 normal;        // 3..5
    float texCoord[2];  // 6..7
    Vec3 tangent;       // 8..10  (x,y,z) - w omitted, we don't need sign for now

    bool operator==(const Vertex& other) const {
        return position == other.position &&
            normal == other.normal &&
            texCoord == other.texCoord && 
			tangent == other.tangent;
    }
};

struct Face {
    Vec3 albedo;                // 0..2
    Vec3 emission;              // 3..5
    uint32_t diffuseTextureID;  // 6
    uint32_t material_type;     // 7
    float roughness;            // 8
    float ior;                  // 9
    float metallic;             // 10
    float alpha;                // 11
    float area;                 // 12
    uint32_t metalRoughTextureID;   // 13
    uint32_t normalTextureID;       // 14
    float pad0;             // 15 (pad to 16 floats total)
};

void loadFromFile(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Face>& faces,
    std::vector<std::string>& textureFiles,
    const std::string& modelPath);

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