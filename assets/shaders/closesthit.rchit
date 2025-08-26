#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "common.glsl"

layout(binding = 2, set = 0) buffer Vertices{float vertices[];};
layout(binding = 3, set = 0) buffer Indices{uint indices[];};
layout(binding = 4, set = 0) buffer Faces{float faces[];};
layout(binding = 5, set = 0) uniform sampler2D diffuseTextures[];

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec2 texCoord;
};

struct Face
{
    vec3 diffuse;
    vec3 emission;
    int  diffuseTextureID;
};

Vertex unpackVertex(uint index)
{
    uint stride = 8;
    uint offset = index * stride;
    Vertex v;
    v.position = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
    v.normal   = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
    v.texCoord = vec2(vertices[offset + 6], vertices[offset + 7]);
    return v;
}

Face unpackFace(uint index)
{
    uint stride = 7;
    uint offset = index * stride;
    Face f;
    f.diffuse  = vec3(faces[offset + 0], faces[offset + 1], faces[offset + 2]);
    f.emission = vec3(faces[offset + 3], faces[offset + 4], faces[offset + 5]);
    f.diffuseTextureID = floatBitsToInt(faces[offset + 6]);
    return f;
}

void main() {
    const Vertex v0 = unpackVertex(indices[3 * gl_PrimitiveID + 0]);
    const Vertex v1 = unpackVertex(indices[3 * gl_PrimitiveID + 1]);
    const Vertex v2 = unpackVertex(indices[3 * gl_PrimitiveID + 2]);

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    const vec3 position = v0.position * barycentricCoords.x + v1.position * barycentricCoords.y + v2.position * barycentricCoords.z;
    const vec3 normal   = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
    
    // Interpolate texture coordinates
    const vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    const Face face = unpackFace(gl_PrimitiveID);
    
    vec3 diffuseColor = face.diffuse;
    // If the face has a valid texture ID, sample the texture
    if (face.diffuseTextureID != -1) {
        // non uniformity check helps the compiler with dynamic array indexing
        diffuseColor = texture(nonuniformEXT(diffuseTextures[nonuniformEXT(face.diffuseTextureID)]), texCoord).rgb;
    }

    // Set payload data
    payload.brdf     = diffuseColor / M_PI;
    payload.emission = face.emission;
    payload.position = position;
    payload.normal   = normal;
    payload.done     = false;
}