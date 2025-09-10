#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "common.glsl"

layout(binding = 3, set = 0) buffer Vertices { float vertices[]; };
layout(binding = 4, set = 0) buffer Indices  { uint indices[]; };
layout(binding = 5, set = 0) buffer Materials { float materials[]; };
layout(binding = 6, set = 0) buffer FaceMaterialIndices { uint materialIndices[]; };
layout(binding = 7, set = 0) uniform sampler2D textures[];

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
};

struct Material {
    vec3 albedo;
    vec3 emission;
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

Vertex unpackVertex(uint index) {
    uint stride = 11u; // pos(3) + norm(3) + uv(2) + tangent(3)
    uint offset = index * stride;
    Vertex v;
    v.position = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
    v.normal   = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
    v.texCoord = vec2(vertices[offset + 6], vertices[offset + 7]);
    v.tangent  = vec3(vertices[offset + 8], vertices[offset + 9], vertices[offset + 10]);
    return v;
}

Material unpackMaterial(uint index) {
    uint stride = 16u;
    uint offset = index * stride;
    Material m;
    m.albedo            = vec3(materials[offset + 0], materials[offset + 1], materials[offset + 2]);
    m.emission          = vec3(materials[offset + 3], materials[offset + 4], materials[offset + 5]);
    m.diffuseTextureID  = floatBitsToInt(materials[offset + 6]);
    m.material_type     = floatBitsToInt(materials[offset + 7]);
    m.roughness         = materials[offset + 8];
    m.ior               = materials[offset + 9];
    m.metallic          = materials[offset + 10];
    m.alpha             = materials[offset + 11];
    m.metalRoughTextureID = floatBitsToInt(materials[offset + 12]);
    m.normalTextureID   = floatBitsToInt(materials[offset + 13]);
    m.pad0              = materials[offset + 14];
    m.pad1              = materials[offset + 15];
    return m;
}

void main() {
    // Vertex unpacking
    const Vertex v0 = unpackVertex(indices[3 * gl_PrimitiveID + 0]);
    const Vertex v1 = unpackVertex(indices[3 * gl_PrimitiveID + 1]);
    const Vertex v2 = unpackVertex(indices[3 * gl_PrimitiveID + 2]);

    // Attrib interpolation
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec3 position = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    vec3 normal   = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    const vec2 texCoord = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
    vec3 tangent = normalize(v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z);

    // First get the material index, then unpack
    uint materialIndex = materialIndices[gl_PrimitiveID];
    const Material material = unpackMaterial(materialIndex);

    // --- Albedo (UNORM -> must linearize) ---
    vec3 albedoColor = material.albedo;
    float alpha = material.alpha;
    if (material.diffuseTextureID != -1) {
        vec4 tex = texture(nonuniformEXT(textures[nonuniformEXT(material.diffuseTextureID)]), texCoord);
        albedoColor = pow(tex.rgb, vec3(2.2));
        alpha *= tex.a;
    }

    // --- Metallic/Roughness (stay linear, no gamma) ---
    float metallic  = material.metallic;
    float roughness = material.roughness;
    if (material.metalRoughTextureID != -1) {
        vec4 mr = texture(nonuniformEXT(textures[nonuniformEXT(material.metalRoughTextureID)]), texCoord);
        roughness *= mr.g;
        metallic  *= mr.b;
    }

    // --- Normal map (stay linear, no gamma) ---
    vec3 finalNormal = normal;
    if (material.normalTextureID != -1) {
        vec3 nmap = texture(nonuniformEXT(textures[nonuniformEXT(material.normalTextureID)]), texCoord).rgb;
        nmap = nmap * 2.0 - 1.0;
        vec3 T = normalize(tangent - normal * dot(normal, tangent));
        vec3 B = cross(normal, T);
        mat3 TBN = mat3(T, B, normal);
        finalNormal = normalize(TBN * nmap);
    }

    // --- Write payload ---
    payload.albedo        = albedoColor;
    payload.emission      = material.emission;
    payload.position      = position;
    payload.normal        = finalNormal;
    payload.roughness     = clamp(roughness, 0.01, 1.0);
    payload.ior           = material.ior;
    payload.metallic      = clamp(metallic, 0.0, 1.0);
    payload.alpha         = clamp(alpha, 0.0, 1.0);
    payload.material_type = material.material_type;
    payload.done          = false;
}