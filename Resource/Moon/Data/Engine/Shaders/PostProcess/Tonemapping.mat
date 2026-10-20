// Tonemapping: compresses the HDR image into the displayable range, with the curve the
// camera picked.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage. The
// curves are plain helper functions, which is what a material's fragment block is free to
// carry; the entry point only picks between them and writes the colour.

material {
    name         : Tonemapping,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : float,     name : _Exposure },
        { type : int,       name : _Mode },
        { type : int,       name : _GammaCorrection }
    ]
}

fragment {
    const float EPSILON = 0.0001;

    float luminance(vec3 v)
    {
        return max(dot(v, vec3(0.2126, 0.7152, 0.0722)), EPSILON);
    }

    vec3 applyGammaCorrection(vec3 color)
    {
        const float gamma = 2.2;
        return pow(color, vec3(1.0 / gamma));
    }

    vec3 neutral(vec3 color)
    {
        const float startCompression = 0.8 - 0.04;
        const float desaturation = 0.15;

        float x = min(color.r, min(color.g, color.b));
        float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
        color -= offset;

        float peak = max(color.r, max(color.g, color.b));
        if (peak < startCompression) return color;

        const float d = 1.0 - startCompression;
        float newPeak = 1.0 - d * d / (peak + d - startCompression);
        color *= newPeak / peak;

        float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
        return mix(color, vec3(newPeak), g);
    }

    vec3 reinhard(vec3 v)
    {
        return v / (1.0 + v);
    }

    vec3 reinhard_jodie(vec3 v)
    {
        float l = luminance(v);
        vec3 tv = v / (1.0 + v);
        return mix(v / (1.0 + l), tv, tv);
    }

    vec3 uncharted2_tonemap_partial(vec3 x)
    {
        const float A = 0.15;
        const float B = 0.50;
        const float C = 0.10;
        const float D = 0.20;
        const float E = 0.02;
        const float F = 0.30;
        return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    }

    vec3 uncharted2_filmic(vec3 v)
    {
        float exposure_bias = 2.0;
        vec3 curr = uncharted2_tonemap_partial(v * exposure_bias);

        vec3 W = vec3(11.2);
        vec3 white_scale = vec3(1.0) / uncharted2_tonemap_partial(W);
        return curr * white_scale;
    }

    // Filament's ACES tone mapper (aces::ACES in filament/filament/src/ToneMapper.cpp):
    // the RRT+ODT fit with its glow and red-modifier modules. The input here is linear
    // Rec.709 (our frame buffer), while Filament feeds it Rec.2020, so the two composed
    // matrices convert in and out of that space.
    const mat3 ACES_REC709_TO_AP0 = mat3(
        0.432998464f, 0.089428003f, 0.019164768f,
        0.375373218f, 0.816515114f, 0.118149266f,
        0.189333054f, 0.102997074f, 0.941990747f);
    const mat3 ACES_AP0_TO_AP1 = mat3(
        1.4514393161f, -0.0765537734f, 0.0083161484f,
        -0.2365107469f, 1.1762296998f, -0.0060324498f,
        -0.2149285693f, -0.0996759264f, 0.9977163014f);
    const mat3 ACES_AP1_TO_REC709 = mat3(
        1.730976665f, -0.131621514f, -0.024574181f,
        -0.603954334f, 1.134869945f, -0.125780763f,
        -0.080095110f, -0.008679589f, 1.065892703f);
    const mat3 ACES_AP1_TO_XYZ = mat3(
        0.662454181f, 0.272228717f, -0.005574649f,
        0.134004207f, 0.674081766f, 0.004060734f,
        0.156187687f, 0.053689517f, 1.010339100f);
    const mat3 ACES_XYZ_TO_AP1 = mat3(
        1.641023380f, -0.663662859f, 0.011721894f,
        -0.324803294f, 1.615331592f, -0.008284442f,
        -0.236424695f, 0.016756348f, 0.988394858f);
    const vec3 ACES_LUMINANCE_AP1 = vec3(0.272229f, 0.674082f, 0.0536895f);

    float acesSaturation(vec3 rgb)
    {
        float mi = min(min(rgb.r, rgb.g), rgb.b);
        float ma = max(max(rgb.r, rgb.g), rgb.b);
        return (max(ma, 1e-5) - max(mi, 1e-5)) / max(ma, 1e-2);
    }

    float acesYC(vec3 rgb)
    {
        float chroma = sqrt(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b));
        return (rgb.b + rgb.g + rgb.r + 1.75 * chroma) / 3.0;
    }

    float acesSigmoid(float x)
    {
        float t = max(1.0 - abs(x / 2.0), 0.0);
        return (1.0 + sign(x) * (1.0 - t * t)) / 2.0;
    }

    float acesGlow(float yc, float gain, float mid)
    {
        if (yc <= 2.0 / 3.0 * mid) return gain;
        if (yc >= 2.0 * mid) return 0.0;
        return gain * (mid / yc - 0.5);
    }

    float acesHue(vec3 rgb)
    {
        if (rgb.r == rgb.g && rgb.g == rgb.b) return 0.0;
        float h = degrees(atan(sqrt(3.0) * (rgb.g - rgb.b), 2.0 * rgb.r - rgb.g - rgb.b));
        return h < 0.0 ? h + 360.0 : h;
    }

    float acesCenterHue(float hue, float center)
    {
        float h = hue - center;
        if (h < -180.0) h += 360.0;
        else if (h > 180.0) h -= 360.0;
        return h;
    }

    vec3 acesDimSurround(vec3 ap1)
    {
        const vec3 xyz = ACES_AP1_TO_XYZ * ap1;
        float sum = max(xyz.x + xyz.y + xyz.z, 1e-6);
        vec3 xyy = vec3(xyz.x / sum, xyz.y / sum, xyz.y);
        xyy.z = clamp(xyy.z, 0.0, 65504.0);
        xyy.z = pow(xyy.z, 0.9811);
        float denom = max(xyy.y, 1e-6);
        vec3 outXYZ = vec3(xyy.x * xyy.z / denom, xyy.z, (1.0 - xyy.x - xyy.y) * xyy.z / denom);
        return ACES_XYZ_TO_AP1 * outXYZ;
    }

    vec3 ACESFilm(vec3 color)
    {
        const float RRT_GLOW_GAIN = 0.05;
        const float RRT_GLOW_MID = 0.08;
        const float RRT_RED_SCALE = 0.82;
        const float RRT_RED_PIVOT = 0.03;
        const float RRT_RED_WIDTH = 135.0;
        const float RRT_SAT_FACTOR = 0.96;
        const float ODT_SAT_FACTOR = 0.93;

        vec3 ap0 = ACES_REC709_TO_AP0 * color;

        float saturation = acesSaturation(ap0);
        float ycIn = acesYC(ap0);
        float s = acesSigmoid((saturation - 0.4) / 0.2);
        ap0 *= 1.0 + acesGlow(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);

        float centeredHue = acesCenterHue(acesHue(ap0), 0.0);
        float hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / RRT_RED_WIDTH));
        hueWeight *= hueWeight;
        ap0.r += hueWeight * saturation * (RRT_RED_PIVOT - ap0.r) * (1.0 - RRT_RED_SCALE);

        vec3 ap1 = clamp(ACES_AP0_TO_AP1 * ap0, 0.0, 65504.0);
        ap1 = mix(vec3(dot(ap1, ACES_LUMINANCE_AP1)), ap1, RRT_SAT_FACTOR);

        const float a = 2.785085;
        const float b = 0.107772;
        const float c = 2.936045;
        const float d = 0.887122;
        const float e = 0.806889;
        vec3 rgbPost = (ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e);

        vec3 linearCV = acesDimSurround(rgbPost);
        linearCV = mix(vec3(dot(linearCV, ACES_LUMINANCE_AP1)), linearCV, ODT_SAT_FACTOR);
        return ACES_AP1_TO_REC709 * linearCV;
    }

    // Filament's default dithering operator (triangle noise, inline_dithering.fs). The
    // output is 8 bit, so a smooth tonemapped gradient (the sky) bands without it.
    float triangleNoise(vec2 n)
    {
        n = fract(n * vec2(5.3987, 5.4421));
        n += dot(n.yx, n.xy + vec2(21.5351, 14.3137));
        float xy = n.x * n.y;
        return fract(xy * 95.4307) + fract(xy * 75.04961) - 1.0;
    }

    void material(inout MaterialInputs m) {
        vec3 color = texture(_InputTexture, TexCoords).rgb * _Exposure;

        switch (_Mode)
        {
            case 0: color = neutral(color); break;
            case 1: color = reinhard(color); break;
            case 2: color = reinhard_jodie(color); break;
            case 3: color = uncharted2_tonemap_partial(color); break;
            case 4: color = uncharted2_filmic(color); break;
            case 5: color = ACESFilm(color); break;
        }

        if (_GammaCorrection != 0)
        {
            color = applyGammaCorrection(color);
        }

        const vec2 screen = vec2(
            max(float(ubo_ScreenWidth), 1.0),
            max(float(ubo_ScreenHeight), 1.0));
        color += vec3(triangleNoise(gl_FragCoord.xy / screen) / 255.0);

        m.baseColor = vec4(color, 1.0);
    }
}
