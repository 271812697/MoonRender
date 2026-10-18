// BRDF lookup table for image-based lighting: the split-sum approximation pre-integrates the
// GGX BRDF over the (NdotV, roughness) domain once, and the lit shaders sample the result.
//
// A full-screen pass (domain : postProcess), a material MoonMatc consumes
// (see docs/ShaderPipeline.md) - MoonMatc supplies the quad vertex stage. The target is the
// two-channel RG32F LUT, so only the first two components of baseColor are stored; the
// generated fragment output is a vec4 and the extra components are discarded by the driver.

material {
    name         : BrdfLut,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false
}

fragment {
    const float PI = 3.14159265359;
    // ----------------------------------------------------------------------------
    // http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
    // efficient VanDerCorpus calculation.
    float RadicalInverse_VdC(uint bits)
    {
         bits = (bits << 16u) | (bits >> 16u);
         bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
         bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
         bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
         bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
         return float(bits) * 2.3283064365386963e-10; // / 0x100000000
    }
    // ----------------------------------------------------------------------------
    vec2 Hammersley(uint i, uint N)
    {
        return vec2(float(i)/float(N), RadicalInverse_VdC(i));
    }
    // ----------------------------------------------------------------------------
    vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
    {
        float a = roughness*roughness;

        float phi = 2.0 * PI * Xi.x;
        float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
        float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

        // from spherical coordinates to cartesian coordinates - halfway vector
        vec3 H;
        H.x = cos(phi) * sinTheta;
        H.y = sin(phi) * sinTheta;
        H.z = cosTheta;

        // from tangent-space H vector to world-space sample vector
        vec3 up          = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent   = normalize(cross(up, N));
        vec3 bitangent = cross(N, tangent);

        vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
        return normalize(sampleVec);
    }
    // ----------------------------------------------------------------------------
    float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
    {
        // Filament's correlated Smith visibility (V_SmithGGXCorrelated), converted back to
        // the full geometry term G = V * 4 * NoL * NoV that the split-sum integral takes.
        // The Schlick-GGX form this replaces did not match the visibility the lit shaders
        // use, so the baked LUT integrated a different BRDF than the one it lit.
        float NdotV = max(dot(N, V), 0.0);
        float NdotL = max(dot(N, L), 0.0);
        float alpha = roughness * roughness;
        float a2 = alpha * alpha;
        float lambdaV = NdotL * sqrt((NdotV - a2 * NdotV) * NdotV + a2);
        float lambdaL = NdotV * sqrt((NdotL - a2 * NdotL) * NdotL + a2);
        float V_Smith = 0.5 / max(lambdaV + lambdaL, 1.0e-5);
        return V_Smith * 4.0 * NdotV * NdotL;
    }
    // ----------------------------------------------------------------------------
    vec2 IntegrateBRDF(float NdotV, float roughness)
    {
        vec3 V;
        V.x = sqrt(1.0 - NdotV*NdotV);
        V.y = 0.0;
        V.z = NdotV;

        float A = 0.0;
        float B = 0.0;

        vec3 N = vec3(0.0, 0.0, 1.0);

        const uint SAMPLE_COUNT = 1024u;
        for(uint i = 0u; i < SAMPLE_COUNT; ++i)
        {
            // generates a sample vector that's biased towards the
            // preferred alignment direction (importance sampling).
            vec2 Xi = Hammersley(i, SAMPLE_COUNT);
            vec3 H = ImportanceSampleGGX(Xi, N, roughness);
            vec3 L = normalize(2.0 * dot(V, H) * H - V);

            float NdotL = max(L.z, 0.0);
            float NdotH = max(H.z, 0.0);
            float VdotH = max(dot(V, H), 0.0);

            if(NdotL > 0.0)
            {
                float G = GeometrySmith(N, V, L, roughness);
                float G_Vis = (G * VdotH) / (NdotH * NdotV);
                float Fc = pow(1.0 - VdotH, 5.0);

                A += (1.0 - Fc) * G_Vis;
                B += Fc * G_Vis;
            }
        }
        A /= float(SAMPLE_COUNT);
        B /= float(SAMPLE_COUNT);
        return vec2(A, B);
    }
    // ----------------------------------------------------------------------------
    void material(inout MaterialInputs m) {
        vec2 integratedBRDF = IntegrateBRDF(TexCoords.x, TexCoords.y);
        m.baseColor = vec4(integratedBRDF, 0.0, 1.0);
    }
}
