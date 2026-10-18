// Auto-exposure: turns the scene's average luminance into the exposure multiplier the
// tonemapping pass reads on the next frame.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage. The
// 1x1 output holds the adapted exposure: the previous frame's value is read back from
// _InputTexture and eased towards the new one, which is what keeps the adaptation smooth.

material {
    name         : AutoExposure,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : sampler2D, name : _LuminanceTexture },
        { type : float,     name : _MinLuminanceEV },
        { type : float,     name : _MaxLuminanceEV },
        { type : float,     name : _ExposureCompensationEV },
        { type : float,     name : _ElapsedTime },
        { type : float,     name : _SpeedUp },
        { type : float,     name : _SpeedDown },
        { type : int,       name : _Progressive }
    ]
}

fragment {
    // Photographic Middle Gray Reference
    const float MIDDLE_GRAY = 0.18;

    // To avoid division by zero and log2 of zero
    const float EPSILON = 0.0001;

    float SafeLog2(float x)
    {
        return log2(max(x, EPSILON));
    }

    // Convert Luminance to Exposure Value (EV100)
    float LuminanceToEV100(float luminance)
    {
        return SafeLog2(luminance / MIDDLE_GRAY) * 2.0;
    }

    // Convert EV100 back to Luminance
    float EV100ToLuminance(float ev100)
    {
        return exp2(ev100 * 0.5) * MIDDLE_GRAY;
    }

    // Advanced Exposure Calculation
    float CalculateExposureMultiplier(float luminance, float minLuminanceEV, float maxLuminanceEV, float exposureCompensation)
    {
        // Convert average luminance to EV100
        float luminanceEV = LuminanceToEV100(luminance);

        // Clamp EV within specified range
        float clampedLuminanceEV = clamp(luminanceEV, minLuminanceEV, maxLuminanceEV);

        // Apply exposure compensation
        float compensatedEV = clampedLuminanceEV - exposureCompensation;

        // Calculate exposure multiplier
        // Using middle gray as reference, with protection against extreme values
        float exposureMultiplier = MIDDLE_GRAY / max(EV100ToLuminance(compensatedEV), EPSILON);

        return exposureMultiplier;
    }

    float InterpolateExposure(float newExposure, float oldExposure)
    {
        if (_Progressive != 0)
        {
            const float delta = newExposure - oldExposure;
            const float speed = delta > 0.0 ? _SpeedUp : _SpeedDown;
            return oldExposure + delta * (1.0 - exp2(-_ElapsedTime * speed));
        }

        return newExposure;
    }

    void material(inout MaterialInputs m) {
        const float averageLuminance =
            textureLod(_LuminanceTexture, vec2(0.5), textureQueryLevels(_LuminanceTexture) - 1).r;
        const float newExposure =
            CalculateExposureMultiplier(averageLuminance, _MinLuminanceEV, _MaxLuminanceEV, _ExposureCompensationEV);
        const float previousExposure = max(texture(_InputTexture, vec2(0.5)).r, 0.0001);
        const float interpolatedExposure = InterpolateExposure(newExposure, previousExposure);
        m.baseColor = vec4(vec3(interpolatedExposure), 1.0);
    }
}
