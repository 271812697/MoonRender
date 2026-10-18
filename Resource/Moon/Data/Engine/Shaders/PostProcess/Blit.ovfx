// Straight copy of a texture into the output: the last stage of a post-process chain.
//
// A full-screen pass, so its domain is postProcess: MoonMatc generates the quad vertex stage
// (clip space, no model matrix) and this file only says what one pixel comes out as.

material {
    name         : Blit,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        m.baseColor = texture(_InputTexture, TexCoords);
    }
}
