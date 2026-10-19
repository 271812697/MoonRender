// Bloom composite: mixes the bloom chain's result into the image.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage.

material {
    name         : Bloom,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : sampler2D, name : _BloomTexture },
        { type : float,     name : _BloomStrength }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        const vec3 hdrColor = texture(_InputTexture, TexCoords).rgb;
        const vec3 bloomColor = texture(_BloomTexture, TexCoords).rgb;
        m.baseColor = vec4(mix(hdrColor, bloomColor, _BloomStrength), 1.0);
    }
}
