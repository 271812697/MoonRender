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
