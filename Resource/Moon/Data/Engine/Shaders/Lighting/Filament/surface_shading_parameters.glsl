// Filament standard model - material inputs and per-pixel parameters.
// Ported from filament/shaders/src/surface_shading_parameters.fs and the get*PixelParams()
// functions in surface_shading_lit.fs. Only the fields the standard model reads are kept;
// the material DSL's own uniforms stay in the material.

struct FilamentMaterialInputs
{
    vec3 baseColor;          // linear
    float metallic;
    float reflectance;       // 0.5 maps to the 0.04 dielectric F0
    float specularFactor;    // KHR_materials_specular: scales the dielectric F0 and F90
    vec3 specularColorFactor;// KHR_materials_specular: tints the dielectric F0
    float roughness;         // perceptual roughness
    float ambientOcclusion;
    float transmission;
    vec3 absorption;         // Beer-Lambert extinction, 1/m
    vec3 emissive;           // linear
    float refractionIndex;
    float clearCoat;
    float clearCoatRoughness;
    float thickness;
};

struct PixelParams
{
    vec3 diffuseColor;
    vec3 f0;
    float f90;
    float specular;          // specularFactor, also applied to the IBL DFG term
    float perceptualRoughness;
    float roughness;         // alpha
    vec2 dfg;
    vec3 energyCompensation;
    float transmission;
    vec3 absorption;
    float refractionIndex;
    float clearCoat;
    float clearCoatRoughness;
    // Filament shades the clear coat with the geometric normal when the base layer has a
    // normal map (surface_light_indirect.fs: "we want to use the geometric normal for the
    // clear coat layer"), so the coat stays smooth instead of picking up the map's detail.
    vec3 clearCoatNormal;
    float thickness;
};

PixelParams preparePixelParams(FilamentMaterialInputs material, vec3 n, vec3 v,
    sampler2D dfgLut, vec3 geometricNormal)
{
    PixelParams pixel;
    pixel.diffuseColor = material.baseColor * (1.0 - material.metallic);

    // Filament surface_shading_lit.fs: with specularFactor/specularColorFactor the dielectric
    // F0 is tinted and scaled, and F90 becomes specularFactor; without them F90 is 1 and F0 is
    // the plain 0.16 * reflectance^2. The factors only touch the dielectric part - a metal's
    // F0 is its base color.
    const float specularFactor = material.specularFactor;
    const vec3 dielectricF0 = min(vec3(0.16 * material.reflectance * material.reflectance)
        * material.specularColorFactor, vec3(1.0)) * specularFactor;
    pixel.f0 = material.baseColor * material.metallic + dielectricF0 * (1.0 - material.metallic);
    pixel.f90 = material.metallic + specularFactor * (1.0 - material.metallic);
    pixel.specular = specularFactor;
    pixel.transmission = saturate(material.transmission);
    pixel.absorption = max(material.absorption, vec3(0.0));
    pixel.refractionIndex = material.refractionIndex;
    pixel.clearCoat = saturate(material.clearCoat);
    pixel.clearCoatRoughness = clamp(material.clearCoatRoughness, 0.045, 1.0);
    pixel.clearCoatNormal = geometricNormal;
    pixel.thickness = max(material.thickness, 0.0);

    // Filament getClearCoatPixelParams() with CLEAR_COAT_IOR_CHANGE (on by default in
    // Filament's MaterialBuilder): the base F0 was computed for an air -> 1.5 interface, but
    // under the clear coat the interface is 1.5 -> 1.5.
    if (pixel.clearCoat > 0.0)
    {
        pixel.f0 = mix(pixel.f0, f0ClearCoatToSurface(pixel.f0), pixel.clearCoat);
    }

    // Filament getRoughnessPixelParams(): the layer under the clear coat must be at least as
    // rough as the coat itself (the coat diffuses it), blended by the coat weight. The CAD
    // baseline (clearCoat 1, clearCoatRoughness 0.53, roughness 0.4) lands on 0.53, not 0.4.
    float perceptualRoughness = clamp(material.roughness, 0.045, 1.0);
    if (pixel.clearCoat > 0.0)
    {
        const float basePerceptualRoughness = max(perceptualRoughness, pixel.clearCoatRoughness);
        perceptualRoughness = mix(perceptualRoughness, basePerceptualRoughness, pixel.clearCoat);
    }
    pixel.perceptualRoughness = perceptualRoughness;
    pixel.roughness = perceptualRoughness * perceptualRoughness;

    const float NoV = max(dot(n, v), 1.0e-4);
    // Filament's split-sum DFG LUT. The LUT is a view-global texture bound by the renderer
    // (CompositeRenderer sets it on the renderer each frame), not a per-material property; a
    // material instance without it used to sample the white empty 2D, DFG=(1,1), E~=1, killing
    // the diffuse and turning the specular into a full mirror.
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
