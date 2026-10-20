// Filament standard model - BRDF terms.
// Ported from filament/shaders/src/surface_brdf.fs (Heitz 2014, GGX, Schlick).
// `roughness` here is alpha = perceptualRoughness^2, exactly as in Filament.

float D_GGX(float roughness, float NoH)
{
    const float a = NoH * roughness;
    // Filament clamps k rather than d: 453.5 is the largest value that still fits fp16, so a
    // mid-range GPU does not overflow into a black/orange pixel in the brightest highlight.
    const float k = min(roughness / (1.0 - NoH * NoH + a * a), 453.5);
    return k * k * (1.0 / PI);
}

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
    const float a2 = roughness * roughness;
    const float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    const float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1.0e-5);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH)
{
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

float F_Schlick(float f0, float f90, float VoH)
{
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 F_Schlick(vec3 f0, float VoH)
{
    const float f = pow5(1.0 - VoH);
    return f + f0 * (1.0 - f);
}
