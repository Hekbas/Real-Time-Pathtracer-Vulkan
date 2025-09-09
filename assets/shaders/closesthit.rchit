#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "common.glsl"

layout(binding = 2, set = 0) buffer Vertices{float vertices[];};
layout(binding = 3, set = 0) buffer Indices{uint indices[];};
layout(binding = 4, set = 0) buffer Faces{float faces[];};
layout(binding = 5, set = 0) uniform sampler2D textures[];

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
};

struct Face {
    vec3 albedo;
    vec3 emission;
    int diffuseTextureID;
    int material_type;
    float roughness;
    float ior;
    float metallic;
    float alpha;
    float area;
    int metallicRoughnessTextureID;
    int normalTextureID;
    float pad0;
};

Vertex unpackVertex(uint index)
{
    uint stride = 11u; // position(3) + normal(3) + texcoord(2) + tangent(3)
    uint offset = index * stride;
    Vertex v;
    v.position = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
    v.normal   = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
    v.texCoord = vec2(vertices[offset + 6], vertices[offset + 7]);
    v.tangent  = vec3(vertices[offset + 8], vertices[offset + 9], vertices[offset + 10]);
    return v;
}

Face unpackFace(uint index)
{
    uint stride = 16u; // as defined in C++ Face struct
    uint offset = index * stride;
    Face f;
    f.albedo    = vec3(faces[offset + 0], faces[offset + 1], faces[offset + 2]);
    f.emission  = vec3(faces[offset + 3], faces[offset + 4], faces[offset + 5]);
    f.diffuseTextureID  = floatBitsToInt(faces[offset + 6]);
    f.material_type     = floatBitsToInt(faces[offset + 7]);
    f.roughness         = faces[offset + 8];
    f.ior               = faces[offset + 9];
    f.metallic          = faces[offset + 10];
    f.alpha             = faces[offset + 11];
    f.area              = faces[offset + 12];
    f.metallicRoughnessTextureID = floatBitsToInt(faces[offset + 13u]);
    f.normalTextureID = floatBitsToInt(faces[offset + 14u]);
    f.pad0 = faces[offset + 15u];
    return f;
}

void main() {
    const Vertex v0 = unpackVertex(indices[3 * gl_PrimitiveID + 0]);
    const Vertex v1 = unpackVertex(indices[3 * gl_PrimitiveID + 1]);
    const Vertex v2 = unpackVertex(indices[3 * gl_PrimitiveID + 2]);

    const vec3 barycentricCoords = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec3 position = v0.position * barycentricCoords.x + v1.position * barycentricCoords.y + v2.position * barycentricCoords.z;
    vec3 normal   = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
    const vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
    // interpolate tangent (not using bitangent sign)
    vec3 tangent = normalize(v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z);

    const Face face = unpackFace(gl_PrimitiveID);
    
    vec3 albedoColor = face.albedo;
    if (face.diffuseTextureID != -1) {
        // albedo textures are sRGB in glTF — if your texture image was uploaded as UNORM,
        // convert to linear. If you used VK_FORMAT_R8G8B8A8_SRGB for the image, DO NOT pow().
        vec3 tex = texture(nonuniformEXT(textures[nonuniformEXT(face.diffuseTextureID)]), texCoord).rgb;
        // If your textures are in UNORM, linearize:
        albedoColor = pow(tex, vec3(2.2)); // remove if texture was created as SRGB format
    }

    float metallic = face.metallic;
    float roughness = face.roughness;

    if (face.metallicRoughnessTextureID != -1) {
        // glTF: R = occlusion, G = roughness, B = metallic (all linear)
        vec4 mr = texture(nonuniformEXT(textures[nonuniformEXT(face.metallicRoughnessTextureID)]), texCoord);
        // multiply/scalar-combine or override (choose workflow). We multiply to keep factor scales:
        roughness *= mr.g;
        metallic  *= mr.b;
    }

    // Normal mapping
    vec3 finalNormal = normal;
    if (face.normalTextureID != -1) {
        vec3 nmap = texture(nonuniformEXT(textures[nonuniformEXT(face.normalTextureID)]), texCoord).rgb;
        nmap = nmap * 2.0 - 1.0; // unpack to [-1,1]
        // build orthonormal TBN (assume tangent is roughly orthogonal)
        vec3 T = normalize(tangent - normal * dot(normal, tangent));
        vec3 B = cross(normal, T);
        mat3 TBN = mat3(T, B, normal);
        finalNormal = normalize(TBN * nmap);
    }

    // write payload
    payload.albedo        = albedoColor;
    payload.emission      = face.emission;
    payload.position      = position;
    payload.normal        = finalNormal;
    payload.roughness     = roughness;
    payload.ior           = face.ior;
    payload.metallic      = metallic;
    payload.alpha         = face.alpha;
    payload.material_type = face.material_type;
    payload.done          = false;
}
