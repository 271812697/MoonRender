// SSAO: screen-space ambient occlusion, accumulated over a 64-sample hemisphere kernel from
// the G-buffer's view-space position and normal.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md): the state and
// the parameters in `material { }`, and the whole algorithm as an entry point that only fills
// in MaterialInputs. A full-screen pass (domain : postProcess), so MoonMatc supplies the quad
// vertex stage; this file is the whole material.

material {
    name         : ssao,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    // radius / bias / screensize used to carry a declaration-time default; GbufferPass::Draw
    // sets all three before every draw, so the defaults were dead and are dropped here.
    parameters : [
        { type : sampler2D, name : gPosition },
        { type : sampler2D, name : gNormal },
        { type : sampler2D, name : texNoise },
        { type : float,     name : radius },
        { type : float,     name : bias },
        { type : float2,    name : screensize },
        { type : mat4,      name : projection },
        { type : mat4,      name : view },
        // The hemisphere kernel is an array: `count` is its size. C++ fills it one element at
        // a time with SetProperty("samples[0]" .. "samples[63]").
        { type : float3,    name : samples, count : 64 }
    ]
}

fragment {
    int kernelSize = 64;

    void material(inout MaterialInputs m) {
        // get input for SSAO algorithm
        vec3 viewPos = texture(gPosition, TexCoords).xyz;
        vec3 normal = normalize(texture(gNormal, TexCoords).rgb);

        vec2 noiseScale=vec2(screensize.x/4,screensize.y/4);
        vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);
        // create TBN change-of-basis matrix: from tangent-space to view-space
        vec3 tangent = randomVec - normal * dot(randomVec, normal);
        if (length(tangent) < 1e-4)
        {
            // When randomVec is (anti-)parallel to the normal, the tangent degenerates
            // to zero and normalize(0) yields NaN (black speckles). Fall back to an
            // axis that is not aligned with the normal.
            vec3 axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
            tangent = normalize(cross(normal, axis));
        }
        else
        {
            tangent = normalize(tangent);
        }
        vec3 bitangent = cross(normal, tangent);
        mat3 TBN = mat3(tangent, bitangent, normal);
        // iterate over the sample kernel and calculate occlusion factor
        float occlusion = 0.0;
        for(int i = 0; i < kernelSize; ++i)
        {
            // get sample position
            vec3 samplePos = TBN * samples[i]; // from tangent to view-space
            samplePos = viewPos + samplePos * radius;

            // project sample position (to sample texture) (to get position on screen/texture)
            vec4 offset = vec4(samplePos, 1.0);
            offset = projection * offset; // from view to clip-space
            offset.xyz /= offset.w; // perspective divide
            offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0

            // get sample depth
            float sampleDepth = texture(gPosition, offset.xy).z; // get depth value of kernel sample

            // range check & accumulate
            float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleDepth));
            occlusion += (sampleDepth >= samplePos.z+bias? 1.0 : 0.0) * rangeCheck;
        }
        occlusion = 1.0 - (occlusion / kernelSize);
        m.baseColor = vec4(occlusion,occlusion,occlusion,1.0);
    }
}
