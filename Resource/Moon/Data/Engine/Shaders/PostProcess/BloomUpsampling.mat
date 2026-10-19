// Upsampling pass of the bloom chain: a 3x3 tent filter over the lower mip.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage.

material {
    name         : BloomUpsampling,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : float,     name : _FilterRadius }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        float x = _FilterRadius;
        float y = _FilterRadius;

        // Take 9 samples around current texel:
        // a - b - c
        // d - e - f
        // g - h - i
        // === ('e' is the current texel) ===
        vec3 a = texture(_InputTexture, vec2(TexCoords.x - x, TexCoords.y + y)).rgb;
        vec3 b = texture(_InputTexture, vec2(TexCoords.x,     TexCoords.y + y)).rgb;
        vec3 c = texture(_InputTexture, vec2(TexCoords.x + x, TexCoords.y + y)).rgb;

        vec3 d = texture(_InputTexture, vec2(TexCoords.x - x, TexCoords.y)).rgb;
        vec3 e = texture(_InputTexture, vec2(TexCoords.x,     TexCoords.y)).rgb;
        vec3 f = texture(_InputTexture, vec2(TexCoords.x + x, TexCoords.y)).rgb;

        vec3 g = texture(_InputTexture, vec2(TexCoords.x - x, TexCoords.y - y)).rgb;
        vec3 h = texture(_InputTexture, vec2(TexCoords.x,     TexCoords.y - y)).rgb;
        vec3 i = texture(_InputTexture, vec2(TexCoords.x + x, TexCoords.y - y)).rgb;

        // Apply weighted distribution, by using a 3x3 tent filter:
        //  1   | 1 2 1 |
        // -- * | 2 4 2 |
        // 16   | 1 2 1 |
        vec3 upsample = e * 4.0;
        upsample += (b + d + f + h) * 2.0;
        upsample += (a + c + g + i);
        upsample *= 1.0 / 16.0;

        m.baseColor = vec4(upsample, 1.0);
    }
}
