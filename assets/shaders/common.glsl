#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

/* -------------------------
   PUSH CONSTANTS (matches C++ PushConstants)
   ------------------------- */
layout(push_constant) uniform PushConstants {
    int frame;
    float pad1, pad2, pad3;   // padding to make first 16 bytes
    vec3 cameraPos;
    float pad4;
    vec3 cameraFront;
    float pad5;
    vec3 cameraUp;
    float pad6;
    vec3 cameraRight;
} pc;

/* -------------------------
   Resource bindings (must match main.cpp descriptor layout)
   binding = 0 : TLAS
   binding = 1 : output storage image
   binding = 2 : vertex buffer (storageBuffer of Vertex)
   binding = 3 : index buffer (storageBuffer of uint)
   binding = 4 : per-triangle material indices (storageBuffer of uint)
   binding = 5 : materials[] (storageBuffer)
   binding = 6 : combined image samplers (textures[])
   ------------------------- */

// TLAS
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

// Output image
layout(set = 0, binding = 1, rgba8) uniform image2D imgOutput;

// Vertex format expected on GPU (must match your C++ Vertex packing)
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec3 tangent;
    vec2 uv;
};
// storage buffer for vertices (binding 2)
layout(std430, set = 0, binding = 2) buffer VertexBuffer {
    Vertex vertices[];
};

// storage buffer for indices (binding 3)
layout(std430, set = 0, binding = 3) buffer IndexBuffer {
    uint indices[];
};

// per-triangle material index (binding 4): materialIndex[primitiveID]
layout(std430, set = 0, binding = 4) buffer MaterialIndexBuffer {
    uint materialIndex[];
};

/* Material layout
   IMPORTANT: This GLSL layout must match the memory layout of the C++ Material struct
   If you change fields/order in C++ update this struct to match exactly :)
*/
struct Material {
    vec4 baseColorFactor;   // (r,g,b,a)
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
    vec4 emissiveFactor;    // (r,g,b,unused)
};

layout(std430, set = 0, binding = 5) buffer Materials {
    Material materials[];
};

// textures array (binding 6)
layout(set = 0, binding = 6) uniform sampler2D textures[];

/* -------------------------
   Ray payloads / hit attributes
   ------------------------- */
struct RayPayload {
    vec3   radiance;
    vec3   throughput;
    uint   seed;
    int    depth;
    int    pad;
};

layout(location = 0) rayPayloadEXT RayPayload payload;

/* -------------------------
   RNG - PCG-ish (32-bit)
   ------------------------- */
uint wang_hash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float rnd(inout uint state) {
    // simple Xorshift/LCG-ish single-step RNG
    state = state * 1664525u + 1013904223u;
    uint x = state ^ (state >> 16u);
    // convert to float in [0,1)
    const float inv = 1.0 / 4294967296.0;
    return float(x) * inv;
}

/* -------------------------
   Sampling helpers
   ------------------------- */
// cosine-weighted hemisphere sample, returns direction in local-space where z is normal
vec3 cosineSampleHemisphere(float u1, float u2) {
    float r = sqrt(u1);
    float phi = 2.0 * 3.14159265358979323846 * u2;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1));
    return vec3(x, y, z);
}

// build orthonormal basis from normal (TBN)
void makeTBN(vec3 n, out vec3 t, out vec3 b) {
    if (abs(n.z) < 0.999) {
        t = normalize(cross(n, vec3(0.0, 0.0, 1.0)));
    } else {
        t = normalize(cross(n, vec3(0.0, 1.0, 0.0)));
    }
    b = cross(n, t);
}

// transform a direction from local (z=normal) to world
vec3 toWorld(vec3 localDir, vec3 n) {
    vec3 t, b;
    makeTBN(n, t, b);
    return normalize(localDir.x * t + localDir.y * b + localDir.z * n);
}

/* -------------------------
   Microfacet / PBR helpers (GGX + Fresnel)
   References: common microfacet formulas
   ------------------------- */
float saturate(float v) { return clamp(v, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    // Schlick approximation
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}

float geometrySchlickGGX(float NdotV, float k) {
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float k) {
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

/* -------------------------
   Utility: safe normalize to avoid NaN
   ------------------------- */
vec3 normalizeSafe(vec3 v) {
    float len = length(v);
    if (len > 0.0) return v / len;
    return vec3(0.0, 0.0, 1.0);
}

/* -------------------------
   Conversions
   ------------------------- */
vec3 srgb_to_linear(vec3 c) {
    // approximate inverse gamma
    return pow(c, vec3(2.2));
}

vec3 linear_to_srgb(vec3 c) {
    return pow(max(vec3(0.0), c), vec3(1.0 / 2.2));
}

/* -------------------------
   Small helpers for texture sampling with material indices
   ------------------------- */
vec4 sampleTexture(int idx, vec2 uv) {
    if (idx < 0) return vec4(1.0); // white if no texture
    return texture(textures[idx], uv);
}

// retrieve Material safely
Material getMaterial(uint primID) {
    uint mid = materialIndex[primID];
    return materials[mid];
}

//////////////////////////////////////////////////////////////////////////////

// // Random number generator (PCG)
// float rand(inout uint seed) {
//     seed = seed * 747796405u + 2891336453u;
//     uint result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
//     result = (result >> 22u) ^ result;
//     return float(result) / 4294967295.0;
// }

// // Convert from tangent space to world space
// vec3 tangentToWorld(vec3 vec, vec3 N, vec3 T, vec3 B) {
//     return T * vec.x + B * vec.y + N * vec.z;
// }

// // Convert from tangent space to world space (simplified version without T and B)
// vec3 tangentToWorld(vec3 vec, vec3 N) {
//     vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
//     vec3 tangent = normalize(cross(up, N));
//     vec3 bitangent = cross(N, tangent);
    
//     return tangent * vec.x + bitangent * vec.y + N * vec.z;
// }

// // Helper function to fetch texture with nonuniform qualifier
// vec4 textureSample(int texIndex, vec2 uv) {
//     if (texIndex < 0) return vec4(1.0);
//     return texture(sampler2D(textures[nonuniformEXT(texIndex)], texSampler), uv);
// }

// // GGX/Trowbridge-Reitz normal distribution function
// float DistributionGGX(vec3 N, vec3 H, float roughness) {
//     float a = roughness * roughness;
//     float a2 = a * a;
//     float NdotH = max(dot(N, H), 0.0);
//     float NdotH2 = NdotH * NdotH;

//     float denom = (NdotH2 * (a2 - 1.0) + 1.0);
//     denom = PI * denom * denom;

//     return a2 / max(denom, 0.0000001);
// }

// // Beckmann normal distribution function
// float DistributionBeckmann(vec3 N, vec3 H, float roughness) {
//     float a = roughness * roughness;
//     float a2 = a * a;
//     float NdotH = max(dot(N, H), 0.0);
//     float NdotH2 = NdotH * NdotH;
    
//     float tan2 = (1.0 - NdotH2) / max(NdotH2, 0.0000001);
//     return exp(-tan2 / a2) / (PI * a2 * NdotH2 * NdotH2);
// }

// // Schlick-GGX geometry function
// float GeometrySchlickGGX(float NdotV, float roughness) {
//     float r = roughness + 1.0;
//     float k = (r * r) / 8.0;
//     return NdotV / (NdotV * (1.0 - k) + k);
// }

// // Smith geometry function
// float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
//     float NdotV = max(dot(N, V), 0.0);
//     float NdotL = max(dot(N, L), 0.0);
//     float ggx1 = GeometrySchlickGGX(NdotV, roughness);
//     float ggx2 = GeometrySchlickGGX(NdotL, roughness);
//     return ggx1 * ggx2;
// }

// // Fresnel-Schlick approximation
// vec3 FresnelSchlick(float cosTheta, vec3 F0) {
//     return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
// }

// // Fresnel-Schlick with roughness
// vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
//     return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
// }

// // Fresnel for dielectrics
// float FresnelDielectric(float cosTheta, float ior) {
//     float r0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
//     return r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);
// }

// // Lambertian diffuse BRDF
// vec3 BRDF_Lambert(vec3 albedo) {
//     return albedo * INV_PI;
// }

// // GGX specular BRDF
// vec3 BRDF_GGX(vec3 N, vec3 V, vec3 L, vec3 H, vec3 F0, float roughness) {
//     float NDF = DistributionGGX(N, H, roughness);
//     float G = GeometrySmith(N, V, L, roughness);
//     vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
//     vec3 numerator = NDF * G * F;
//     float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    
//     return numerator / denominator;
// }

// // Beckmann specular BRDF
// vec3 BRDF_Beckmann(vec3 N, vec3 V, vec3 L, vec3 H, vec3 F0, float roughness) {
//     float NDF = DistributionBeckmann(N, H, roughness);
//     float G = GeometrySmith(N, V, L, roughness);
//     vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
//     vec3 numerator = NDF * G * F;
//     float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    
//     return numerator / denominator;
// }

// // Evaluate complete BRDF
// vec3 EvaluateBRDF(Material mat, vec3 N, vec3 V, vec3 L, vec3 H) {
//     // Base reflectance at normal incidence
//     vec3 F0 = mix(vec3(0.04), mat.albedo, mat.metallic);
    
//     vec3 diffuse = BRDF_Lambert(mat.albedo) * (1.0 - mat.metallic);
//     vec3 specular = vec3(0.0);
    
//     // Choose specular BRDF based on type
//     if (mat.brdf_type == BRDF_TYPE_GGX) {
//         specular = BRDF_GGX(N, V, L, H, F0, mat.roughness);
//     } else if (mat.brdf_type == BRDF_TYPE_BECKMANN) {
//         specular = BRDF_Beckmann(N, V, L, H, F0, mat.roughness);
//     }
    
//     return diffuse + specular;
// }

// // Sample a direction based on BRDF
// vec3 SampleBRDF(Material mat, vec3 N, vec3 V, inout uint seed, out float pdf) {
//     float r1 = rand(seed);
//     float r2 = rand(seed);
    
//     if (mat.metallic > 0.5 || mat.roughness < 0.3) {
//         // Sample specular lobe (GGX importance sampling)
//         float a = mat.roughness * mat.roughness;
        
//         float phi = 2.0 * PI * r1;
//         float cosTheta = sqrt((1.0 - r2) / (1.0 + (a * a - 1.0) * r2));
//         float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        
//         vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
//         H = normalize(tangentToWorld(H, N));
        
//         vec3 L = normalize(reflect(-V, H));
        
//         // Calculate PDF
//         float NdotH = max(dot(N, H), 0.0);
//         float HdotV = max(dot(H, V), 0.0);
//         float D = DistributionGGX(N, H, mat.roughness);
//         pdf = D * NdotH / (4.0 * HdotV);
        
//         return L;
//     } else {
//         // Sample diffuse lobe (cosine-weighted)
//         float phi = 2.0 * PI * r1;
//         float cosTheta = sqrt(r2);
//         float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        
//         vec3 L = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
//         L = normalize(tangentToWorld(L, N));
        
//         pdf = max(dot(N, L), 0.0) * INV_PI;
        
//         return L;
//     }
// }