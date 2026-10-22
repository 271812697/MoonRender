// Bloom downsampling: the 13-tap filter that halves the image each step of the chain.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage. The
// KARIS_AVERAGE variant swaps the plain 13-tap downsample for one that weights each block of
// four samples by 1 / (1 + luma), which is what keeps the first mip free of fireflies.

material {
    name         : BloomDownsampling,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    features : [ KARIS_AVERAGE ],

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : float2,    name : _InputResolution }
    ]
}

fragment {
    vec3 PowVec3(vec3 v, float p)
    {
        return vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p));
    }

    const float invGamma = 1.0 / 2.2;

    vec3 ToSRGB(vec3 v) { return PowVec3(v, invGamma); }

    float sRGBToLuma(vec3 col)
    {
        return dot(col, vec3(0.299, 0.587, 0.114));
    }

    float KarisAverage(vec3 col)
    {
        // Formula is 1 / (1 + luma)
        float luma = sRGBToLuma(ToSRGB(col)) * 0.25;
        return 1.0 / (1.0 + luma);
    }

    void material(inout MaterialInputs m) {
        vec2 srcTexelSize = 1.0 / _InputResolution;
        float x = srcTexelSize.x;
        float y = srcTexelSize.y;

        // Take 13 samples around current texel:
        // a - b - c
        // - j - k -
        // d - e - f
        // - l - m -
        // g - h - i
        // === ('e' is the current texel) ===
        vec3 a = texture(_InputTexture, vec2(TexCoords.x - 2 * x, TexCoords.y + 2 * y)).rgb;
        vec3 b = texture(_InputTexture, vec2(TexCoords.x,       TexCoords.y + 2 * y)).rgb;
        vec3 c = texture(_InputTexture, vec2(TexCoords.x + 2 * x, TexCoords.y + 2 * y)).rgb;

        vec3 d = texture(_InputTexture, vec2(TexCoords.x - 2 * x, TexCoords.y)).rgb;
        vec3 e = texture(_InputTexture, vec2(TexCoords.x,         TexCoords.y)).rgb;
        vec3 f = texture(_InputTexture, vec2(TexCoords.x + 2 * x, TexCoords.y)).rgb;

        vec3 g = texture(_InputTexture, vec2(TexCoords.x - 2 * x, TexCoords.y - 2 * y)).rgb;
        vec3 h = texture(_InputTexture, vec2(TexCoords.x,         TexCoords.y - 2 * y)).rgb;
        vec3 i = texture(_InputTexture, vec2(TexCoords.x + 2 * x, TexCoords.y - 2 * y)).rgb;

        vec3 j = texture(_InputTexture, vec2(TexCoords.x - x, TexCoords.y + y)).rgb;
        vec3 k = texture(_InputTexture, vec2(TexCoords.x + x, TexCoords.y + y)).rgb;
        vec3 l = texture(_InputTexture, vec2(TexCoords.x - x, TexCoords.y - y)).rgb;
        vec3 mm = texture(_InputTexture, vec2(TexCoords.x + x, TexCoords.y - y)).rgb;

        // Apply weighted distribution:
        // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
        // a,b,d,e * 0.125
        // b,c,e,f * 0.125
        // d,e,g,h * 0.125
        // e,f,h,i * 0.125
        // j,k,l,m * 0.5
        // This shows 5 square areas that are being sampled. But some of them overlap,
        // so to have an energy preserving downsample we need to make some adjustments.
        // The weights are the distributed, so that the sum of j,k,l,m (e.g.)
        // contribute 0.5 to the final color output. The code below is written
        // to effectively yield this sum. We get:
        // 0.125*5 + 0.03125*4 + 0.0625*4 = 1

        vec3 downsample = vec3(0.0);

        // Karis average on each block of 4 samples: without it very bright subpixels make the
        // bloom pulsate.
    #if defined(KARIS_AVERAGE)
        vec3 groups[5];
        // We are writing to mip 0, so we need to apply Karis average to each block
        // of 4 samples to prevent fireflies (very bright subpixels, leads to pulsating
        // artifacts).
        groups[0] = (a + b + d + e) * (0.125 / 4.0);
        groups[1] = (b + c + e + f) * (0.125 / 4.0);
        groups[2] = (d + e + g + h) * (0.125 / 4.0);
        groups[3] = (e + f + h + i) * (0.125 / 4.0);
        groups[4] = (j + k + l + mm) * (0.5 / 4.0);
        groups[0] *= KarisAverage(groups[0]);
        groups[1] *= KarisAverage(groups[1]);
        groups[2] *= KarisAverage(groups[2]);
        groups[3] *= KarisAverage(groups[3]);
        groups[4] *= KarisAverage(groups[4]);
        downsample = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];
        downsample = max(downsample, 0.0001);
    #else
        downsample = e * 0.125;                   // ok
        downsample += (a + c + g + i) * 0.03125;  // ok
        downsample += (b + d + f + h) * 0.0625;   // ok
        downsample += (j + k + l + mm) * 0.125;   // ok
    #endif

        m.baseColor = vec4(downsample, 1.0);
    }
}
