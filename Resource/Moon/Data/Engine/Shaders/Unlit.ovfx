// Unlit surface material: no lighting, albedo straight out of the texture.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md): the state and
// the parameters in `material { }`, and the shading itself as an entry point that only fills
// in MaterialInputs. The transform, the uniforms and main() are put in by MoonMatc from the
// shader prelude, so this file is the whole material.

material {
    name         : Unlit,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    // Variant whose fragment stage is empty: the shadow pass only needs geometry.
    features : [ ALPHA_CLIPPING ],
    passes   : [ SHADOW_PASS ],

    parameters : [
        { type : sampler2D, name : u_DiffuseMap, label : "Diffuse Map", group : "Maps" },
        { type : float4,    name : u_Diffuse, label : "Diffuse Color", group : "Base", color : true },
        { type : float2,    name : u_TextureTiling, label : "Tiling", group : "UV" },
        { type : float2,    name : u_TextureOffset, label : "Offset", group : "UV" },
        { type : float,     name : u_AlphaClippingThreshold,
          label : "Alpha Clipping", group : "Base", min : 0.0, max : 1.0, step : 0.001 }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        const vec2 texCoords = TileAndOffsetTexCoords(fs_in.TexCoords, u_TextureTiling, u_TextureOffset);
        const vec4 diffuseColor = texture(u_DiffuseMap, texCoords) * u_Diffuse;

    #if defined(ALPHA_CLIPPING)
        if (diffuseColor.a < u_AlphaClippingThreshold) {
            discard;
        }
    #endif

        m.baseColor = diffuseColor;
    }
}
