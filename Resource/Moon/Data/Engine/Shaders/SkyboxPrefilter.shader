// Skybox: specular prefilter. Runs the GGX importance sampling that bakes one roughness level of
// the prefiltered environment cube per mip, which the IBL reads for specular reflections.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own
// their main() because the skeleton's fixed transform cannot express them: the vertex stage
// writes gl_Position straight from the cube position on the camera (there is no model matrix
// involved), and the fragment stage declares its own varying and output. Nothing is generated for
// these stages - the attributes, the VS_OUT block, the output and the mains below are the whole
// stage.

material {
    name         : SkyboxPrefilter,
    shadingModel : unlit,
    blending     : opaque,
    // No culling: the cube is seen from the inside while a face is captured, and depth writing
    // stays on for that capture (see SkyboxRenderPass::computeSkyTexture).
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : samplerCube, name : SkyboxCube },
        { type : int,     name : u_CaptureAxis, default : 0 },
        { type : float,   name : u_CaptureSide, default : 1.0 },
        { type : float2,  name : u_CaptureViewport, default : vec2(1024.0) },
        { type : float,       name : roughness },
        { type : float,       name : lodOffset, default : 0.0 },
        // Source LOD that makes one output texel cover the same solid angle as one texel of
        // the 256x256 cube Filament filters from (log2(sourceWidth / 256)). The mirror level
        // (roughness zero) has no kernel width to derive a LOD from, so it uses this one
        // instead of a single texel of the 1024 source.
        { type : float,       name : baseLod, default : 0.0 },
        { type : float,       name : intensity, default : 1.0 }
    ]
}

vertex {
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;

    out VS_OUT
    {
        vec3 dir;
    } vs_out;

    void main()
    {
        vs_out.dir = geo_Pos;
        gl_Position = ubo_Projection *ubo_View  * vec4(ubo_ViewPos +  geo_Pos, 1.0);
    }
}

fragment {
    in VS_OUT
    {
        vec3 dir;
    } fs_in;

    out vec4 FRAGMENT_COLOR;
    const float PI = 3.14159265359;
    // ----------------------------------------------------------------------------
    float DistributionGGX(vec3 N, vec3 H, float roughness)
    {
        float a = roughness*roughness;
        float a2 = a*a;
        float NdotH = max(dot(N, H), 0.0);
        float NdotH2 = NdotH*NdotH;

        float nom   = a2;
        float denom = (NdotH2 * (a2 - 1.0) + 1.0);
        denom = PI * denom * denom;

        return nom / denom;
    }
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
    float random(vec2 w)
    {
        const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
        return fract(m.z * fract(dot(w, m.xy)));
    }

    // Filament's IBLPrefilterContext HDR compression (hdrLinear=1024, hdrMax=16384):
    // without it the studio softboxes produce severe fireflies in the prefiltered mips.
    vec3 compressHDR(vec3 color, float linear, float compressed)
    {
        const vec3 rec709 = vec3(0.2126, 0.7152, 0.0722);
        float luma = dot(color, rec709);
        float s = 1.0;
        if (luma > linear)
        {
            s = ((linear * linear - compressed * luma) /
                 ((2.0 * linear - compressed - luma) * luma));
        }
        return color * s;
    }

    mat3 tangentSpace(vec3 N)
    {
        vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        mat3 R;
        R[0] = normalize(cross(up, N));
        R[1] = cross(N, R[0]);
        R[2] = N;
        return R;
    }

    vec3 hemisphereImportanceSampleDggx(vec2 u, float a)
    {
        float phi = 2.0 * PI * u.x;
        float cosTheta2 = (1.0 - u.y) / (1.0 + (a + 1.0) * ((a - 1.0) * u.y));
        float cosTheta = sqrt(cosTheta2);
        float sinTheta = sqrt(1.0 - cosTheta2);
        return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    }

    void main()
    {
        // The same face-direction mapping as the skybox capture: sampling the source cube with
        // fs_in.dir (the cube's local position) mirrored the prefiltered environment.
        vec2 captureP = vec2(gl_FragCoord.x / u_CaptureViewport.x * 2.0 - 1.0,
            1.0 - gl_FragCoord.y / u_CaptureViewport.y * 2.0);
        vec3 N;
        if (u_CaptureAxis == 0)      N = vec3(u_CaptureSide, captureP.y, u_CaptureSide * -captureP.x);
        else if (u_CaptureAxis == 1) N = vec3(captureP.x, u_CaptureSide, u_CaptureSide * -captureP.y);
        else                         N = vec3(u_CaptureSide * captureP.x, captureP.y, u_CaptureSide);
        N = normalize(N);

        // Filament (IBLPrefilterContext): a random rotation around the face normal, a sample
        // count of one at roughness zero and 1024 otherwise, and the kernel's per-sample LOD
        // (from the sample density) plus the per-level lodOffset.
        uint SAMPLE_COUNT = (roughness <= 0.0001) ? 1u : 1024u;

        float angle = 2.0 * PI * random(gl_FragCoord.xy);
        float c = cos(angle);
        float s = sin(angle);
        mat3 rotation;
        rotation[0] = vec3(c, s, 0.0);
        rotation[1] = vec3(-s, c, 0.0);
        rotation[2] = vec3(0.0, 0.0, 1.0);
        mat3 T = tangentSpace(N) * rotation;

        float a = roughness * roughness; // linear roughness, as Filament's kernel uses
        vec3 prefilteredColor = vec3(0.0);
        float totalWeight = 0.0;

        for (uint i = 0u; i < SAMPLE_COUNT; ++i)
        {
            vec2 u = Hammersley(i, SAMPLE_COUNT);
            vec3 H = hemisphereImportanceSampleDggx(u, a);
            float NoH = H.z;
            float NoL = clamp(2.0 * NoH * NoH - 1.0, 0.0, 1.0);
            vec3 L = vec3(2.0 * NoH * H.x, 2.0 * NoH * H.y, NoL);

            // V == N, so pdf = D * 0.25; the kernel stores l = -log4(sampleCount * pdf).
            float pdf = DistributionGGX(vec3(0.0, 0.0, 1.0), H, roughness) * 0.25;
            float l = (pdf > 0.0) ? -0.5 * log2(float(SAMPLE_COUNT) * pdf) : 0.0;

            // A roughness-zero level has no measurable kernel: it is a single sample along N.
            // Sample the source at baseLod, the LOD whose texel is the size of one *output*
            // texel, so the mirror level is a filtered copy of the 1024 source (as Filament's
            // level 0 is of its 256 source) instead of one aliased source texel.
            float sampleLod = (roughness <= 0.0001) ? baseLod : (l + lodOffset);
            prefilteredColor += compressHDR(textureLod(SkyboxCube, T * L, sampleLod).rgb, 1024.0, 16384.0) * NoL;
            totalWeight += NoL;
        }

        prefilteredColor = prefilteredColor / max(totalWeight, 1.0);

        FRAGMENT_COLOR = vec4(prefilteredColor * intensity, 1.0);
    }
}
