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

    const vec3 Fr = (D_GGX(pixel.roughness, NoH) * V_SmithGGXCorrelated(pixel.roughness, NoV, NoL))
        * F_Schlick(pixel.f0, pixel.f90, LoH);
    const vec3 Fd = pixel.diffuseColor * Fd_Lambert();

    return (Fd + Fr * pixel.energyCompensation) * radiance * NoL;
}
