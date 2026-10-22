// Shadow fallback: the depth-only material the shadow pass draws a caster with when its own
// material has no SHADOW_PASS variant.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md): only the state in `material { }`
// and one entry point that fills in MaterialInputs. The transform, the uniforms and main()
// come from the shader prelude, so this file needs no vertex stage - the generated one is
// exactly the `ubo_Projection * ubo_View * ubo_Model * vec4(geo_Pos, 1.0)` the old vertex
// stage wrote by hand.

material {
    name         : ShadowFallback,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    parameters : [ ]
}

fragment {
    void material(inout MaterialInputs m) {
        // The pass writes no colour at all (the shadow pass runs with colorWriting off), it
        // only needs depth. The explicit write is kept from the old shader: the value is the
        // one the rasterizer would produce anyway, but writing gl_FragDepth is what turns
        // early-Z testing off, so dropping it would change how the pass behaves.
        gl_FragDepth = gl_FragCoord.z;

        // The generated fragment main always has a colour to output; leave it at the white
        // initMaterial() gives, since nothing reads it.
    }
}
