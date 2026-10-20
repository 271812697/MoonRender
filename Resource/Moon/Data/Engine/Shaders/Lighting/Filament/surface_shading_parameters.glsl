// Filament standard model - material inputs and per-pixel parameters.
// Ported from filament/shaders/src/surface_shading_parameters.fs. Only the fields the
// standard model reads are kept; the material DSL's own uniforms stay in the material.

struct FilamentMaterialInputs
{
    vec3 baseColor;          // linear
    float metallic;
    float roughness;         // perceptual roughness
    float ambientOcclusion;
    float transmission;
    vec3 emissive;           // linear
};

struct PixelParams
{
    vec3 diffuseColor;
    vec3 f0;
    float f90;
    float perceptualRoughness;
    float roughness;         // alpha
    vec2 dfg;
    vec3 energyCompensation;
    float transmission;
};

PixelParams preparePixelParams(FilamentMaterialInputs material, vec3 n, vec3 v, sampler2D dfgLut)
{
    PixelParams pixel;
    pixel.perceptualRoughness = clamp(material.roughness, 0.045, 1.0);
    pixel.roughness = pixel.perceptualRoughness * pixel.perceptualRoughness;
    pixel.diffuseColor = material.baseColor * (1.0 - material.metallic);
    pixel.f0 = mix(vec3(0.04), material.baseColor, material.metallic);
    pixel.f90 = 1.0;
    pixel.transmission = material.transmission;

    const float NoV = max(dot(n, v), 1.0e-4);
    pixel.dfg = textureLod(dfgLut, vec2(NoV, pixel.perceptualRoughness), 0.0).rg;
    if (any(isnan(pixel.dfg)) || any(isinf(pixel.dfg)))
    {
        pixel.dfg = vec2(0.0);
    }
    pixel.dfg = clamp(pixel.dfg, 0.0, 1.0);

    // Our DFG LUT is (scale, bias): F = F0 * scale + bias. Filament divides the
    // energy compensation by the value of that term at F0 = 1, i.e. scale + bias.
    pixel.energyCompensation =
        vec3(1.0) + pixel.f0 * (1.0 / max(pixel.dfg.x + pixel.dfg.y, 1.0e-4) - 1.0);
    return pixel;
}
