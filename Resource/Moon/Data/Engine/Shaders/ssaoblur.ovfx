// SSAO blur: the depth-aware pass that smooths the SSAO buffer before it is composited.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage.

material {
    name         : ssaoblur,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : ssaoInput },
        { type : sampler2D, name : gPosition },
        { type : float,     name : u_BlurDepthTolerance }
    ]
}

fragment {
    // Depth-aware (bilateral) blur: symmetric 7x7 binomial spatial kernel combined
    // with a depth-similarity weight, so the AO is smoothed inside a surface but
    // never bleeds across depth discontinuities (silhouettes, thin walls).
    const float kKernel[7] = float[](1.0, 6.0, 15.0, 20.0, 15.0, 6.0, 1.0);

    void material(inout MaterialInputs m) {
        vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
        float centerDepth = texture(gPosition, TexCoords).z;

        vec3 result = vec3(0.0);
        float weightSum = 0.0;
        for (int x = -3; x <= 3; ++x)
        {
            for (int y = -3; y <= 3; ++y)
            {
                vec2 uv = TexCoords + vec2(float(x), float(y)) * texelSize;
                float depthDiff = abs(texture(gPosition, uv).z - centerDepth);
                float w = kKernel[x + 3] * kKernel[y + 3];
                w *= exp(-depthDiff * depthDiff / (u_BlurDepthTolerance * u_BlurDepthTolerance));
                result += texture(ssaoInput, uv).rgb * w;
                weightSum += w;
            }
        }
        m.baseColor = vec4(result / max(weightSum, 1e-5), 1.0);
    }
}
