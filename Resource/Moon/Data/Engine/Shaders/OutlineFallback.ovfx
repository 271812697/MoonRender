// Outline fallback: the flat colour the outline pass paints over the stencil-marked
// silhouette of a selected object.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md): the state and
// the parameters in `material { }`, the colour as the fragment entry point. The transform, the
// uniforms and main() are put in by MoonMatc from the shader prelude, so the vertex stage is
// the generated skeleton and this file carries no vertex block.

material {
    name         : OutlineFallback,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : float4, name : _OutlineColor }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        m.baseColor = _OutlineColor;
    }
}
