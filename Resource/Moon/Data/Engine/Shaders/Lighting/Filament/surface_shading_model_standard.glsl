// Filament standard model - direct lighting lobe.
// Ported from filament/shaders/src/surface_shading_model_standard.fs (surfaceShading).

vec3 surfaceShading(PixelParams pixel, vec3 n, vec3 v, vec3 l, vec3 radiance)
{
    const float NoL = saturate(dot(n, l));
    if (NoL <= 0.0)
    {
        return vec3(0.0);
    }

    const vec3 h = normalize(v + l);
    const float NoV = saturate(dot(n, v));
    const float NoH = saturate(dot(n, h));
    const float LoH = saturate(dot(l, h));

    vec3 Fr = (D_GGX(pixel.roughness, NoH) * V_SmithGGXCorrelated(pixel.roughness, NoV, NoL))
        * F_Schlick(pixel.f0, pixel.f90, LoH);
    // Filament surfaceShading(): Fd *= (1 - pixel.transmission).
    vec3 Fd = pixel.diffuseColor * Fd_Lambert() * (1.0 - pixel.transmission);
    // Filament clearCoatLobe(): GGX distribution at clearCoatRoughness, Kelemen visibility
    // and an F0 = 0.04 Fresnel layer; the base lobes are attenuated by (1 - Fcc) once.
    vec3 clearCoatLobe = vec3(0.0);
    if (pixel.clearCoat > 0.0)
    {
        const float ccRoughness = pixel.clearCoatRoughness * pixel.clearCoatRoughness;
        const float D = D_GGX(ccRoughness, NoH);
        const float V = 0.25 / max(LoH * LoH, 1.0e-4);
        const float F = F_Schlick(0.04, 1.0, LoH) * pixel.clearCoat;
        clearCoatLobe = vec3(D * V * F);
        Fd *= (1.0 - F);
        Fr *= (1.0 - F);
    }

    return (Fd + Fr * pixel.energyCompensation + clearCoatLobe) * radiance * NoL;
}
