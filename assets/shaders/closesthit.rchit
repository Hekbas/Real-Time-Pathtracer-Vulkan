#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

hitAttributeEXT vec2 attribs; // barycentric coords

const int MAX_DEPTH = 6;

// GGX sample (returns half-vector in local space where z = normal)
vec3 sampleGGX(float u1, float u2, float alpha) {
    float phi = 2.0 * 3.14159265358979323846 * u1;
    // inverse CDF for GGX
    float a2 = alpha * alpha;
    float denom = 1.0 - u2;
    float cosTheta = sqrt((1.0 - u2) / (1.0 + (a2 - 1.0) * u2));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

void main() {
    payload.pad = 1;

    // get primitive / vertex indices
    uint primID = gl_PrimitiveID; // assumes this symbol is available in your toolchain
    uint triIndex = primID * 3u;
    uint i0 = indices[triIndex + 0u];
    uint i1 = indices[triIndex + 1u];
    uint i2 = indices[triIndex + 2u];

    // barycentrics: attribs.x = b1, attribs.y = b2, b0 = 1 - b1 - b2
    float b1 = attribs.x;
    float b2 = attribs.y;
    float b0 = 1.0 - b1 - b2;

    // interpolate vertex attributes (world-space positions/normals/tangents/uv)
    Vertex v0 = vertices[i0];
    Vertex v1 = vertices[i1];
    Vertex v2 = vertices[i2];

    vec3 p = v0.pos * b0 + v1.pos * b1 + v2.pos * b2;
    vec3 n = normalizeSafe(v0.normal * b0 + v1.normal * b1 + v2.normal * b2);
    vec3 tan = normalizeSafe(v0.tangent * b0 + v1.tangent * b1 + v2.tangent * b2);
    vec2 uv = v0.uv * b0 + v1.uv * b1 + v2.uv * b2;

    // view vector (V) - use incoming ray direction (world space)
    vec3 Wo = -normalizeSafe(gl_WorldRayDirectionEXT); // outgoing direction toward camera

    // fetch material
    Material mat = getMaterial(primID);

    // baseColor (SRGB texture -> linear)
    vec4 baseColTex = sampleTexture(mat.baseColorTex, uv);
    vec3 baseColor = srgb_to_linear(baseColTex.rgb) * mat.baseColorFactor.rgb;
    float alpha = mat.baseColorFactor.a; // not used for now (alphaMode handling omitted)

    // metallic & roughness
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if (mat.metallicRoughnessTex >= 0) {
        vec4 mr = sampleTexture(mat.metallicRoughnessTex, uv); // glTF: R=occlusion, G=roughness, B=metallic
        roughness *= mr.g;
        metallic *= mr.b;
    }
    roughness = clamp(roughness, 0.025, 1.0);
    float alphaR = roughness * roughness; // alpha for GGX (common remapping)

    // emissive
    vec3 emissive = srgb_to_linear(sampleTexture(mat.emissiveTex, uv).rgb) * mat.emissiveFactor.rgb;

    // normal mapping (if available)
    vec3 N = n;
    if (mat.normalTex >= 0) {
        vec3 nmap = texture(textures[mat.normalTex], uv).xyz * 2.0 - 1.0;
        // build TBN from interpolated tangent & normal
        vec3 T = normalizeSafe(tan);
        vec3 B = normalizeSafe(cross(N, T));
        mat3 TBN = mat3(T, B, N);
        N = normalizeSafe(TBN * nmap);
    }

    // add emissive contribution scaled by throughput
    vec3 emissionContrib = payload.throughput * emissive;

    // if maximum depth or throughput is nearly zero -> return emission
    if (payload.depth >= MAX_DEPTH) {
        payload.radiance = emissionContrib;
        return;
    }

    // Russian Roulette after a few bounces
    if (payload.depth >= 3) {
        float p_cont = clamp(max(max(payload.throughput.r, payload.throughput.g), payload.throughput.b), 0.05, 1.0);
        float q = 1.0 - p_cont;
        if (rnd(payload.seed) < q) {
            payload.radiance = emissionContrib;
            return;
        }
        // account for probability
        payload.throughput /= (1.0 - q);
    }

    // Fresnel base reflectance
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // choose sampling strategy: specular probability from F0 luminance
    float specularProb = clamp(max(max(F0.r, F0.g), F0.b), 0.05, 0.95);
    float xi = rnd(payload.seed);

    vec3 L; // sampled incoming direction
    float pdf = 1.0;
    vec3 brdf = vec3(0.0);
    float NdotL = 0.0;
    float NdotV = max(dot(N, Wo), 0.0);

    if (xi < specularProb) {
        // sample GGX (specular)
        float u1 = rnd(payload.seed);
        float u2 = rnd(payload.seed);
        vec3 h_local = sampleGGX(u1, u2, alphaR); // local half-vector
        vec3 H = toWorld(h_local, N);
        float VdotH = max(dot(Wo, H), 1e-6);
        // reflect view across H to get light direction
        L = normalizeSafe(2.0 * VdotH * H - Wo);
        NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) {
            payload.radiance = emissionContrib;
            return;
        }

        // D, G, F
        float NdotH = max(dot(N, H), 0.0);
        float D = distributionGGX(NdotH, alphaR);
        float k = (alphaR + 1.0) * (alphaR + 1.0) / 8.0; // Schlick-GGX remap
        float G = geometrySmith(NdotV, NdotL, k);
        vec3 F = fresnelSchlick(max(dot(H, Wo), 0.0), F0);

        // microfacet specular
        brdf = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-6);

        // PDF for sampling L via sampling H:
        // pdf_H = D * NdotH / (4 * VdotH)  -> pdf_L = pdf_H / (4 * VdotH) ? canonical: pdf = D*NdotH/(4*VdotH)
        float pdf_h = D * NdotH / max(4.0 * VdotH, 1e-6);
        pdf = max(pdf_h, 1e-6);

        // adjust for sampling probability
        pdf = pdf * specularProb;
    } else {
        // sample cosine-weighted hemisphere (diffuse)
        float u1 = rnd(payload.seed);
        float u2 = rnd(payload.seed);
        vec3 local = cosineSampleHemisphere(u1, u2); // z is cos
        L = toWorld(local, N);
        NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) {
            payload.radiance = emissionContrib;
            return;
        }

        // Lambertian diffuse (with energy conservation: multiply by (1 - metallic))
        brdf = (1.0 - metallic) * baseColor / 3.14159265358979323846;

        // pdf for cosine hemisphere
        pdf = local.z / 3.14159265358979323846;
        pdf = pdf * (1.0 - specularProb);
    }

    // compute new throughput = throughput * (brdf * cos / pdf)
    float cosTerm = NdotL;
    vec3 newThroughput = payload.throughput * (brdf * cosTerm / max(pdf, 1e-6));

    // GLSL-safe NaN/huge check: NaN => x != x; also reject huge values
    if (any(notEqual(newThroughput, newThroughput)) || any(greaterThan(abs(newThroughput), vec3(1e20)))) {
        payload.radiance = emissionContrib;
        return;
    }

    // Setup payload for recursive ray: store emission separately because trace overwrites payload.radiance
    vec3 savedEmission = emissionContrib;

    // update payload for next bounce
    payload.throughput = newThroughput;
    payload.depth = payload.depth + 1;

    // advance seed a little more to decorrelate (rnd already advanced)
    // jitter ray origin to avoid self-intersection
    vec3 origin = p + N * 0.001;

    // clear payload.radiance so returned radiance is only from traced path
    payload.radiance = vec3(0.0);

    // trace secondary ray (missIndex = 1 as earlier)
    uint  rayFlags = gl_RayFlagsOpaqueEXT;
    float tMin     = 0.001;
    float tMax     = 10000.0;

    traceRayEXT(
        topLevelAS,     // acceleration structure
        rayFlags,       // rayFlags
        0xFF,           // cullMask
        0,              // sbtRecordOffset
        0,              // sbtRecordStride
        1,              // missIndex
        origin.xyz,     // ray origin
        tMin,           // ray min range
        L.xyz,          // ray direction
        tMax,           // ray max range
        0               // payload (location = 0)
    );

    // payload.radiance now contains radiance returned from next path (including its own emission)
    vec3 returned = payload.radiance;

    // final accumulated radiance = emissive contribution + returned (returned already scaled by newThroughput)
    payload.radiance = savedEmission + returned;
    return;
}
