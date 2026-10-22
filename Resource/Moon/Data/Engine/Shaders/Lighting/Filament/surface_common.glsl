// Filament standard model - small shared helpers.
// Ported from filament/shaders/src (surface_brdf.fs, surface_shading_parameters.fs).

#include ":Shaders/Common/Constants.glsl"

float Fd_Lambert()
{
    return 1.0 / PI;
}

float pow5(float x)
{
    const float x2 = x * x;
    return x2 * x2 * x;
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(vec3 x)
{
    return clamp(x, 0.0, 1.0);
}

// Filament's normalFiltering() (Kaplanyan 2016, Tokuyoshi 2017): widen the roughness where the
// shading normal changes quickly across a pixel, so a high-gloss surface does not sparkle along
// silhouettes and normal-map seams.
float normalFiltering(float perceptualRoughness, vec3 worldNormal)
{
    vec3 du = dFdx(worldNormal);
    vec3 dv = dFdy(worldNormal);
    float variance = 0.15 * (dot(du, du) + dot(dv, dv));
    float roughness = perceptualRoughness * perceptualRoughness;
    float kernelRoughness = min(2.0 * variance, 0.2);
    float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);
    return sqrt(sqrt(squareRoughness));
}
