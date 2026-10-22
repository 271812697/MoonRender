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
    vec3 color = Fd + Fr * pixel.energyCompensation;

    // Filament clearCoatLobe(): GGX distribution at clearCoatRoughness, Kelemen visibility and
    // an F0 = 0.04 Fresnel layer; the base lobes are attenuated by (1 - Fcc) once. With a
    // normal map the lobe uses the geometric normal and its own NoL (Filament's standard
    // model returns before multiplying the coat by the base NoL).
    if (pixel.clearCoat > 0.0)
    {
        const float ccRoughness = pixel.clearCoatRoughness * pixel.clearCoatRoughness;
        const float ccNoH = saturate(dot(pixel.clearCoatNormal, h));
        const float ccNoL = saturate(dot(pixel.clearCoatNormal, l));
        const float D = D_GGX(ccRoughness, ccNoH);
        const float V = 0.25 / max(LoH * LoH, 1.0e-4);
        const float F = F_Schlick(0.04, 1.0, LoH) * pixel.clearCoat;
        const vec3 clearCoatLobe = vec3(D * V * F);
        color *= (1.0 - F);
        return (color * radiance * NoL) + (clearCoatLobe * radiance * ccNoL);
    }

    return color * radiance * NoL;
}
