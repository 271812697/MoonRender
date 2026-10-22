// FilamentPBR: the Filament standard model, ported line by line.
//
// This is the experimental material used to align the real-time result with Filament
// (shaders/src/surface_*.fs). Its includes live in Lighting/Filament/; the engine's
// Standard material is left untouched so the two can be compared on the material ball.

material {
    name : FilamentPBR,
    shadingModel : unlit,
    blending : opaque,
    culling : back,
    depthWrite : true,

    vertexAttributes : [POSITION, UV0, NORMAL, TANGENT, BITANGENT],

    variantFilter : [
        DIRECTIONAL_LIGHTING,
        DYNAMIC_LIGHTING,
        SHADOWING,
        FOG,
        PICKING,
        SSR,
        DEPTH
    ],

    parameters : [
        // label / group / min / max / step / color are the designer schema. The material
        // designer and the actor inspector read them instead of guessing from the name.
        { type : sampler2D, name : u_AlbedoMap, label : "Albedo Map", group : "Textures" },
        { type : bool,      name : u_HasAlbedoMap, default : false, ui : false },
        // Defaults match Qt-Filament's MaterialPreview(): base colour 184/255 (#b8b8b8),
        // metallic 0 and roughness 0.40, so a fresh instance starts from the reference values.
        { type : float4,    name : u_Albedo, default : vec4(0.7215686, 0.7215686, 0.7215686, 1.0), label : "Base Color", group : "Base", color : true },
        { type : sampler2D, name : u_MetallicMap, label : "Metallic Map", group : "Textures" },
        { type : bool,      name : u_HasMetallicMap, default : false, ui : false },
        { type : float,     name : u_Metallic, default : 0.0, label : "Metallic", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : sampler2D, name : u_RoughnessMap, label : "Roughness Map", group : "Textures" },
        { type : bool,      name : u_HasRoughnessMap, default : false, ui : false },
        { type : float,     name : u_Roughness, default : 0.4, label : "Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_Reflectance, default : 0.5, label : "Reflectance", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_SpecularFactor, default : 1.0, label : "Specular Factor", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float3,    name : u_SpecularColorFactor, default : vec3(1.0), label : "Specular Color Factor", group : "Base", color : true },
        { type : float,     name : u_ClearCoat, default : 0.0, label : "Clear Coat", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_ClearCoatRoughness, default : 0.0, label : "Clear Coat Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : sampler2D, name : u_AmbientOcclusionMap, label : "Ambient Occlusion Map", group : "Textures" },
        { type : bool,      name : u_HasAOMap, default : false, ui : false },
        { type : sampler2D, name : u_NormalMap, label : "Normal Map", group : "Textures" },
        // A uniform rather than a feature on purpose: the editor builds its texture list
        // from the variant's parameters, and a map only read under a feature is optimized
        // out of the default variant, so it never showed up in the panel.
        { type : bool,      name : u_UseNormalMap, default : false, label : "Use Normal Map", group : "Base" },
        // Filament's material flag `specularAntiAliasing` (GEOMETRIC_SPECULAR_AA). It is off
        // for the Qt-Filament reference material (aiDefaultMat does not declare it), so the
        // alignment preview keeps it off; a CAD material may turn it on.
        { type : bool,      name : u_SpecularAA, default : false, label : "Specular AA", group : "Base" },
        { type : sampler2D, name : u_EmissiveMap, label : "Emissive Map", group : "Textures" },
        { type : float3,    name : u_EmissiveColor, default : vec3(0.0), label : "Emissive Color", group : "Base", color : true },
        { type : float,     name : u_EmissiveIntensity, default : 0.0, label : "Emissive Intensity", group : "Base", min : 0.0, max : 10.0, step : 0.01 },
        { type : float,     name : u_Transmission, default : 0.0, label : "Transmission", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float3,    name : u_Absorption, default : vec3(0.0), label : "Absorption", group : "Parameters", color : true },
        { type : float,     name : u_RefractionIndex, default : 1.5, label : "Refraction Index", group : "Parameters", min : 1.0, max : 3.0, step : 0.01 },
        { type : float,     name : u_Thickness, default : 1.0, label : "Thickness", group : "Parameters", min : 0.0, max : 10.0, step : 0.01 },

        { type : samplerCube, name : _IrradianceCube, ui : false },
        { type : samplerCube, name : _PrefilterCube, ui : false },
        { type : sampler2D,   name : _BRDFLut, ui : false },
        { type : sampler2D,   name : _ShadowMap, ui : false },
        { type : mat4,        name : _LightSpaceMatrix, ui : false }
    ]
}

vertex {
    // The engine's Geometry::Vertex layout (Mesh::Upload): position, texCoords, normal,
    // tangent, bitangent. The earlier order (normal at 1) fed the UVs in as normals, which
    // turned the sphere's shading into concentric bands.

    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    } vs_out;

    void main()
    {
        vs_out.FragPos = vec3(ubo_Model * vec4(geo_Pos, 1.0));
        vs_out.TexCoords = geo_TexCoords;
        vs_out.Normal = normalize(mat3(transpose(inverse(ubo_Model))) * geo_Normal);
        vs_out.TBN = ConstructTBN(ubo_Model, geo_Normal, geo_Tangent, cross(geo_Normal, geo_Tangent));
        gl_Position = ubo_Projection * ubo_View * vec4(vs_out.FragPos, 1.0);
    }
}

fragment {
    #include ":Shaders/Lighting/Filament/surface_lighting.glsl"

    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    } fs_in;

    out vec4 FRAGMENT_COLOR;

    void main()
    {
        // The panel shows no map for a fresh material, but the texture property can still hold
        // the black "empty" texture, which multiplied the whole PBR input to zero. Only sample a
        // map when the designer marked it as assigned.
        const vec4 albedoTexel = u_HasAlbedoMap ? texture(u_AlbedoMap, fs_in.TexCoords) : vec4(1.0);
        const vec4 albedo = albedoTexel * u_Albedo;
        const float metallic = (u_HasMetallicMap ? texture(u_MetallicMap, fs_in.TexCoords).r : 1.0) * u_Metallic;
        const float roughness = (u_HasRoughnessMap ? texture(u_RoughnessMap, fs_in.TexCoords).r : 1.0) * u_Roughness;
        const float ao = u_HasAOMap ? texture(u_AmbientOcclusionMap, fs_in.TexCoords).r : 1.0;

        // sRGB sources are decoded once each; lighting works in linear.
        const vec3 emissive = pow(texture(u_EmissiveMap, fs_in.TexCoords).rgb, vec3(2.2))
            * pow(u_EmissiveColor, vec3(2.2)) * u_EmissiveIntensity;

        FilamentMaterialInputs material;
        material.baseColor = pow(albedoTexel.rgb, vec3(2.2)) * pow(u_Albedo.rgb, vec3(2.2));
        material.metallic = metallic;
        material.roughness = roughness;
        material.reflectance = u_Reflectance;
        material.specularFactor = u_SpecularFactor;
        material.specularColorFactor = u_SpecularColorFactor;
        material.refractionIndex = u_RefractionIndex;
        material.thickness = u_Thickness;
        material.clearCoat = u_ClearCoat;
        material.clearCoatRoughness = u_ClearCoatRoughness;
        material.ambientOcclusion = ao;
        material.transmission = u_Transmission;
        material.absorption = u_Absorption;
        material.emissive = emissive;

        vec3 n = normalize(fs_in.Normal);
        const vec3 geometricNormal = n;
        const vec3 v = normalize(ubo_ViewPos - fs_in.FragPos);

        if (u_UseNormalMap)
        {
            const vec3 tangentNormal =
                normalize(texture(u_NormalMap, fs_in.TexCoords).rgb * 2.0 - 1.0);
            n = normalize(fs_in.TBN * tangentNormal);
        }

        // Filament's specular AA (GEOMETRIC_SPECULAR_AA): widen the roughness by how fast the
        // *geometric* normal changes across this pixel. Filament enables it per material, and
        // the reference material leaves it off.
        FilamentMaterialInputs filteredMaterial = material;
        if (u_SpecularAA)
        {
            filteredMaterial.roughness = normalFiltering(material.roughness, geometricNormal);
            filteredMaterial.clearCoatRoughness =
                normalFiltering(material.clearCoatRoughness, geometricNormal);
        }

        const PixelParams pixel = preparePixelParams(
            filteredMaterial, n, v, _BRDFLut, geometricNormal);

        vec3 color = evaluateFilamentSurface(
            material, pixel, fs_in.FragPos, n, v,
            _IrradianceCube, _PrefilterCube, _ShadowMap, _LightSpaceMatrix, _BRDFLut);
        color += emissive;

        FRAGMENT_COLOR = vec4(color, albedo.a);
    }
}
