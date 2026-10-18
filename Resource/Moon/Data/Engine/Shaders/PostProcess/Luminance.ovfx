// Luminance pass of the auto-exposure chain: renders the scene's luminance, with the
// centre-weighted "bulge" the exposure meter reads.
//
// A full-screen pass (domain : postProcess) - MoonMatc supplies the quad vertex stage.

material {
    name         : Luminance,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : float,     name : _CenterWeightBias }
    ]
}

fragment {
    const float EPSILON = 0.0001;

    float luminance(vec3 v)
    {
        return max(dot(v, vec3(0.2126, 0.7152, 0.0722)), EPSILON);
    }

    float remapStrength(float userStrength)
    {
        return log(max(userStrength, EPSILON));
    }

    vec2 bulge(vec2 texCoords, float strength)
    {
        if (strength < EPSILON)
        {
            return texCoords;
        }

        const float scale = remapStrength(strength);
        const float radius = 1;
        vec2 center = vec2(0.5);
        vec2 uv = texCoords - center;
        float bulgeDist = smoothstep(scale, radius, distance(center, texCoords) / distance(center, vec2(1.0)));
        uv = uv * bulgeDist * 0.5 + center;
        uv = uv * 2.0 - 0.5;
        return uv;
    }

    void material(inout MaterialInputs m) {
        const vec2 bulgeCoords = bulge(TexCoords, _CenterWeightBias);
        const vec3 color = texture(_InputTexture, bulgeCoords).rgb;
        m.baseColor = vec4(vec3(luminance(color)), 1.0);
    }
}
