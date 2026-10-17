// Separable Gaussian blur used by the SSAO / shadow blur steps.
//
// A full-screen pass (domain : postProcess); the kernel size is a feature, so MoonMatc
// compiles one variant per size and the `#if defined(...)` chain below picks the weights.

material {
    name         : Blur,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    features : [ k11x11, k13x13, k15x15 ],

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : bool,      name : horizontal }
    ]
}

fragment {
#if defined(k11x11)
    const float weight[6] = float[] (
        0.1198770,  // offset 0
        0.1161890,  // offset +1 -1
        0.1057910,  // offset +2 -2
        0.0904881,  // offset +3 -3
        0.0727092,  // offset +4 -4
        0.0548838   // offset +5 -5
    );
#else
#if defined(k13x13)
    // precomputed 13x13 Gaussian blur filter (sigma = 4)
    const float weight[7] = float[] (
        0.1112200,  // offset 0
        0.1077980,  // offset +1 -1
        0.0981515,  // offset +2 -2
        0.0839534,  // offset +3 -3
        0.0674585,  // offset +4 -4
        0.0509203,  // offset +5 -5
        0.0361079   // offset +6 -6
    );
#else
#if defined(k15x15)
    const float weight[8] = float[] (
        0.1061150,  // offset 0
        0.1028510,  // offset +1 -1
        0.0936465,  // offset +2 -2
        0.0801001,  // offset +3 -3
        0.0643623,  // offset +4 -4
        0.0485832,  // offset +5 -5
        0.0344506,  // offset +6 -6
        0.0229491   // offset +7 -7
    );
#else
    const float weight[5] = float[] (0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);
#endif
#endif
#endif

    void material(inout MaterialInputs m) {
        vec2 tex_offset = 1.0 / textureSize(_InputTexture, 0);
        vec3 result = texture(_InputTexture, TexCoords).rgb * weight[0];
    #if defined(k11x11)
        int k = 6;
    #else
    #if defined(k13x13)
        int k = 7;
    #else
    #if defined(k15x15)
        int k = 8;
    #else
        int k = 5;
    #endif
    #endif
    #endif
        if (horizontal)
        {
            for (int i = 1; i < k; ++i)
            {
                result += texture(_InputTexture, TexCoords + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
                result += texture(_InputTexture, TexCoords - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            }
        }
        else
        {
            for (int i = 1; i < k; ++i)
            {
                result += texture(_InputTexture, TexCoords + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
                result += texture(_InputTexture, TexCoords - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            }
        }
        m.baseColor = vec4(result, 1.0);
    }
}
