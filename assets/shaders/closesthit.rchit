#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#include "common.glsl"

layout(binding = 2, set = 0) buffer Vertices{float vertices[];};
layout(binding = 3, set = 0) buffer Indices{uint indices[];};
layout(binding = 4, set = 0) buffer Faces{float faces[];};

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

struct Vertex
{
    vec3 position;
    vec3 normal;
};

struct Face
{
    vec3 diffuse;
    vec3 emission;
};

Vertex unpackVertex(uint index)
{
    uint stride = 6;
    uint offset = index * stride;
    Vertex v;
    v.position = vec3(vertices[offset +  0], vertices[offset +  1], vertices[offset + 2]);
    v.normal   = vec3(vertices[offset +  3], vertices[offset +  4], vertices[offset + 5]);
    return v;
}

Face unpackFace(uint index)
{
    uint stride = 6;
    uint offset = index * stride;
    Face f;
    f.diffuse  = vec3(faces[offset +  0], faces[offset +  1], faces[offset + 2]);
    f.emission = vec3(faces[offset +  3], faces[offset +  4], faces[offset + 5]);
    return f;
}

void main()
{
    // Get the three vertices of the triangle that was hit
    const Vertex v0 = unpackVertex(indices[3 * gl_PrimitiveID + 0]);
    const Vertex v1 = unpackVertex(indices[3 * gl_PrimitiveID + 1]);
    const Vertex v2 = unpackVertex(indices[3 * gl_PrimitiveID + 2]);

    // Barycentric coordinates of the hit point
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    // Interpolate position and normal to get the exact values at the hit point
    const vec3 position = v0.position * barycentricCoords.x + v1.position * barycentricCoords.y + v2.position * barycentricCoords.z;
    const vec3 normal   = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);

    // Get the material for this face
    const Face face = unpackFace(gl_PrimitiveID);
    
    // Set the payload data to be returned to the raygen shader
    payload.brdf     = face.diffuse / M_PI; // Lambertian BRDF
    payload.emission = face.emission;
    payload.position = position;
    payload.normal   = normal;
    payload.done     = false;
}
