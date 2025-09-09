#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
precision highp float;

const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;

struct HitPayload
{
    vec3 position;
    vec3 normal;
    vec3 emission;
    vec3 albedo;
    float roughness;
    float metallic;
    float ior;
    float alpha;
    int material_type;
    bool done;
};

const highp float M_PI = 3.14159265358979323846;
const highp float EPS = 1e-5;

// --- RNG (pcg/rand) ---
uint pcg(inout uint state)
{
    uint prev = state * 747796405u + 2891336453u;
    uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    state = prev;
    return (word >> 22u) ^ word;
}
uvec2 pcg2d(uvec2 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return v;
}
float rand(inout uint seed)
{
    uint val = pcg(seed);
    return (float(val) * (1.0 / float(0xffffffffu)));
}

// --- Coordinate system helper ---
void createCoordinateSystem(in vec3 N, out vec3 T, out vec3 B) {
    if (abs(N.x) > abs(N.y))
        T = normalize(vec3(N.z, 0.0, -N.x));
    else
        T = normalize(vec3(0.0, -N.z, N.y));
    B = cross(N, T);
}

// --- GGX / Fresnel / Smith helpers ---
float saturate(float x) { return clamp(x, 0.0, 1.0); }

float schlickFresnel(float cosTheta, float F0_scalar) {
    return F0_scalar + (1.0 - F0_scalar) * pow(1.0 - cosTheta, 5.0);
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// convert roughness -> alpha for GGX
float roughnessToAlpha(float roughness) {
    return max(0.001, roughness * roughness);
}

float GGX_D(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = M_PI * denom * denom;
    return a2 / denom;
}

// Smith GGX G1 and combined G
float smithG1(float NdotV, float alpha) {
    float a = alpha;
    float k = (a * a) / 2.0; // UE4 style
    return NdotV / (NdotV * (1.0 - k) + k);
}
float smithG(float NdotV, float NdotL, float alpha) {
    return smithG1(NdotV, alpha) * smithG1(NdotL, alpha);
}

// GGX sampling (world-space) - approximate VNDF sampling with commonly used approach
vec3 sampleGGX(vec3 N, vec3 V, float roughness, inout uint seed) {
    float a = roughnessToAlpha(roughness);
    float r1 = rand(seed);
    float r2 = rand(seed);

    float phi = 2.0 * M_PI * r1;
    float cosTheta = sqrt( (1.0 - r2) / (1.0 + (a*a - 1.0) * r2) );
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    // half vector in tangent space
    vec3 Ht = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // build TBN
    vec3 T, B;
    createCoordinateSystem(N, T, B);
    vec3 H = normalize(Ht.x * T + Ht.y * B + Ht.z * N);

    // reflect view around H to get L
    vec3 L = reflect(-V, H);
    return normalize(L);
}

// cosine-weighted hemisphere sample (diffuse)
vec3 sampleCosineHemisphere(vec3 N, inout uint seed) {
    float r1 = rand(seed);
    float r2 = rand(seed);
    float phi = 2.0 * M_PI * r1;
    float r = sqrt(r2);
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - r2));
    vec3 T, B;
    createCoordinateSystem(N, T, B);
    return normalize(x * T + y * B + z * N);
}

// PDF helpers
float pdfCosineHemisphere(float NdotL) {
    return NdotL / M_PI;
}
float pdfGGX(vec3 N, vec3 V, vec3 L, float roughness) {
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), EPS);
    float alpha = roughnessToAlpha(roughness);
    float D = GGX_D(NdotH, alpha);
    // convert half-vector pdf to direction pdf
    return (D * NdotH) / (4.0 * VdotH);
}

// Evaluate BRDF (diffuse + GGX specular)
// albedo is linear (not divided by PI here)
vec3 evalBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Fresnel F0
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(VdotH, F0);

    float alpha = roughnessToAlpha(roughness);
    float D = GGX_D(NdotH, alpha);
    float G = smithG(NdotV, NdotL, alpha);

    vec3 spec = (D * G * F) / (4.0 * NdotV * NdotL + 1e-6);
    vec3 diff = (1.0 - metallic) * (albedo / M_PI);
    return diff + spec;
}
