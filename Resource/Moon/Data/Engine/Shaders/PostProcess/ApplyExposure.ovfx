// Applies the auto-exposure luminance to the HDR image.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage.

material {
    name         : ApplyExposure,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : sampler2D, name : _ExposureTexture }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        const float exposure = texture(_ExposureTexture, vec2(0.5)).r;
        const vec3 color = texture(_InputTexture, TexCoords).rgb * exposure;
        m.baseColor = vec4(color, 1.0);
    }
}
