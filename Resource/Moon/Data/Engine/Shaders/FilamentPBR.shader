// FilamentPBR: the Filament standard model, in the material DSL's `lit` form.
//
// This is the alignment/reference material: its defaults match Qt-Filament's MaterialPreview()
// (base colour 184/255 #b8b8b8, metallic 0, roughness 0.40), so the CAD view and the reference
// renderer can be compared on the same numbers.
//
// The file used to write the whole surface by hand inside a `fragment { void main() }` block
// (preparePixelParams + evaluateFilamentSurface + the IBL calls), declared `unlit` because the
// DSL had no other option. That version is in git history; it is what the generated path was
// verified against. Now the material only fills `MaterialInputs`, exactly like a Filament .mat,
// and MoonMatc supplies the rest:
//
//   - the vertex stage is the generated skeleton (MVP, world position, normal matrix, and the
//     TBN the material asks for through MATERIAL_NEEDS_TBN),
//   - the fragment stage is `initMaterial` -> `material()` -> `evaluateMaterial()`, where
//     evaluateMaterial() is the `lit` surface prelude (Lighting/Filament/*),
//   - `MATERIAL_HAS_*` follows from which fields this file assigns; the ones it leaves alone
//     keep their neutral defaults and the surface prelude compiles nothing for them.
//
// The maps are sampled unconditionally. An unassigned texture slot holds the engine's white
// 1x1 fallback (the neutral multiplier), except the normal map, which declares
// `fallback : flatNormal` so it decodes to (0, 0, 1). That is what Filament's default instance
// does, and the reason the old `u_Has*Map` / `u_UseNormalMap` booleans are gone.

material {
    name         : FilamentPBR,
    shadingModel : lit,
    blending     : opaque,
    culling      : back,
    depthWrite   : true,

    // Without a variant filter the renderer never sets the lighting variant bits, so
    // `evaluateFilamentSurface` compiled without its directional/point cases: the surface got the
    // ambient sphere light and the IBL but no sun - a flat, dark ball with the sun's highlight
    // missing. That is the same list Standard declares.
    variantFilter : [DIRECTIONAL_LIGHTING, DYNAMIC_LIGHTING, SHADOWING],

    // Filament's per-material `specularAntiAliasing` (GEOMETRIC_SPECULAR_AA). Off to match the
    // Qt-Filament reference material (aiDefaultMat does not declare it).
    specularAntiAliasing : false,
    // Filament's `clearCoatIorChange`, on there too: the layer under a clear coat sees a
    // 1.5 -> 1.5 interface instead of air -> 1.5.
    clearCoatIorChange : true,

    parameters : [
        { type : sampler2D, name : u_AlbedoMap, label : "Albedo Map", group : "Textures" },
        { type : float4,    name : u_Albedo, default : vec4(0.7215686, 0.7215686, 0.7215686, 1.0), label : "Base Color", group : "Base", color : true },
        { type : sampler2D, name : u_MetallicMap, label : "Metallic Map", group : "Textures" },
        { type : float,     name : u_Metallic, default : 0.0, label : "Metallic", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : sampler2D, name : u_RoughnessMap, label : "Roughness Map", group : "Textures" },
        { type : float,     name : u_Roughness, default : 0.4, label : "Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_Reflectance, default : 0.5, label : "Reflectance", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_SpecularFactor, default : 1.0, label : "Specular Factor", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float3,    name : u_SpecularColorFactor, default : vec3(1.0), label : "Specular Color Factor", group : "Base", color : true },
        { type : float,     name : u_ClearCoat, default : 0.0, label : "Clear Coat", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_ClearCoatRoughness, default : 0.0, label : "Clear Coat Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : sampler2D, name : u_AmbientOcclusionMap, label : "Ambient Occlusion Map", group : "Textures" },
        // `fallback : flatNormal` asks the runtime for a 1x1 (0.5, 0.5, 1.0) texture when the
        // designer assigned no map: it decodes to the neutral tangent-space normal, so the
        // sample below is unconditional - no "has normal map" uniform, like filament.
        { type : sampler2D, name : u_NormalMap, label : "Normal Map", group : "Textures", fallback : flatNormal },
        { type : sampler2D, name : u_EmissiveMap, label : "Emissive Map", group : "Textures" },
        { type : float3,    name : u_EmissiveColor, default : vec3(0.0), label : "Emissive Color", group : "Base", color : true },
        { type : float,     name : u_EmissiveIntensity, default : 0.0, label : "Emissive Intensity", group : "Base", min : 0.0, max : 10.0, step : 0.01 },
        { type : float,     name : u_Transmission, default : 0.0, label : "Transmission", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float3,    name : u_Absorption, default : vec3(0.0), label : "Absorption", group : "Parameters", color : true },
        { type : float,     name : u_RefractionIndex, default : 1.5, label : "Refraction Index", group : "Parameters", min : 1.0, max : 3.0, step : 0.01 },
        { type : float,     name : u_Thickness, default : 1.0, label : "Thickness", group : "Parameters", min : 0.0, max : 10.0, step : 0.01 }
    ]
}

fragment {
    void material(inout MaterialInputs m)
    {
        prepareMaterial(m);

        // sRGB sources are decoded once each; lighting works in linear.
        const vec4 albedoTexel = texture(u_AlbedoMap, fs_in.TexCoords);
        const vec3 emissiveTexel = texture(u_EmissiveMap, fs_in.TexCoords).rgb;

        m.baseColor = vec4(pow(albedoTexel.rgb, vec3(2.2)) * pow(u_Albedo.rgb, vec3(2.2)),
            albedoTexel.a * u_Albedo.a);
        m.metallic = texture(u_MetallicMap, fs_in.TexCoords).r * u_Metallic;
        m.roughness = texture(u_RoughnessMap, fs_in.TexCoords).r * u_Roughness;
        m.reflectance = u_Reflectance;
        m.ambientOcclusion = texture(u_AmbientOcclusionMap, fs_in.TexCoords).r;
        m.emissive = vec4(pow(emissiveTexel, vec3(2.2)) * pow(u_EmissiveColor, vec3(2.2))
            * u_EmissiveIntensity, 1.0);

        m.specularFactor = u_SpecularFactor;
        m.specularColorFactor = u_SpecularColorFactor;
        m.clearCoat = u_ClearCoat;
        m.clearCoatRoughness = u_ClearCoatRoughness;
        m.transmission = u_Transmission;
        m.absorption = u_Absorption;
        m.ior = u_RefractionIndex;
        m.thickness = u_Thickness;

        // Tangent-space normal; evaluateMaterial() puts it in world space through the
        // interpolated tangent frame (MATERIAL_NEEDS_TBN is 1 because this file writes the
        // field). With no map assigned the slot holds the flat-normal fallback, which decodes
        // to (0, 0, 1) - the neutral normal - so no runtime toggle is needed.
        m.normal = normalize(texture(u_NormalMap, fs_in.TexCoords).rgb * 2.0 - 1.0);
    }
}
