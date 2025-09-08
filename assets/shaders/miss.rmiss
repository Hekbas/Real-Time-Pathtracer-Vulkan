#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

void main() {
    payload.pad = 0;
    // world-space ray direction (pointing away from origin along ray)
    vec3 dir = normalizeSafe(gl_WorldRayDirectionEXT);

    // simple procedural sky: horizon to zenith blend
    // horizon color (groundish / horizon)
    vec3 horizon = vec3(0.7, 0.8, 0.95); // light bluish-haze
    vec3 zenith  = vec3(0.02, 0.05, 0.2); // deep blue overhead
    // blend factor: y==1 => zenith, y==-1 => horizon
    float t = clamp(0.5 * (dir.y + 1.0), 0.0, 1.0);

    // a small boost for the sun direction (optional) — you can tweak or remove
    vec3 sunDir = normalize(vec3(0.0, 0.985, 0.17)); // angled sun
    float sunPower = 8.0;
    float sunSize = 0.995; // tighter -> smaller sun
    float sun = pow(saturate(max(dot(dir, sunDir), 0.0)), 2000.0) * sunPower;

    vec3 sky = mix(horizon, zenith, pow(t, 1.5));
    sky += vec3(sun);

    // whitebalance / intensity: you can change multiplier to taste
    float envIntensity = 1.0;

    // final radiance is throughput * environment radiance (linear)
    payload.radiance = payload.throughput * sky * envIntensity;
    
    // TEST
    //ivec2 px = ivec2(gl_LaunchIDEXT.xy);
    //imageStore(imgOutput, px, vec4(0.0, 1.0, 0.0, 1.0)); // bright green
}
