#version 460
#extension GL_EXT_ray_tracing : enable
#include "common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

// vec3 skyColor(vec3 direction)
// {
//     vec3 dir = normalize(direction);
    
//     // Rayleigh scattering approximation
//     float rayleigh = 1.0 - dir.y;
//     vec3 rayleighScattering = vec3(0.2, 0.4, 0.8) * rayleigh;
    
//     // Mie scattering (aerosols)
//     vec3 sunDir = normalize(vec3(0.5, 0.8, 0.3));
//     float mie = pow(max(0.0, dot(dir, sunDir)), 8.0);
//     vec3 mieScattering = vec3(1.0, 0.9, 0.8) * mie * 0.5;
    
//     return rayleighScattering + mieScattering;
// }

vec3 skyColor(vec3 dir) {
    float t = clamp(0.5 * (dir.y + 1.0), 0.0, 1.0);
    return mix(vec3(0.6, 0.7, 0.9), vec3(0.02, 0.02, 0.05), pow(1.0 - t, 2.0));
}

void main()
{
    // When a ray misses geometry, set payload.emission to environment contribution
    // get ray direction
    vec3 dir = normalize(gl_WorldRayDirectionEXT);
    payload.emission = skyColor(dir); // low-intensity sky (could set it stroner idk)
    payload.done = true;
}
