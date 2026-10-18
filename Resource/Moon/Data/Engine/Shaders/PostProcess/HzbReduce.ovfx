// Hierarchical-Z reduce pass: each output texel keeps the FARTHEST surface of the 2x2 input
// block it covers, so that "if even the farthest surface in a region is closer than an
// object, the object is fully occluded" stays conservative.
//
// This project renders with REVERSED-Z (GLBackend sets glDepthRange(1,0) and PipelineState
// defaults to depthFunc GREATER), i.e. window depth 1 = near and 0 = far/background, so the
// farthest surface is the SMALLEST value and the reduce takes the MIN. A pipeline using
// regular depth has to take the MAX here instead.
//
// A full-screen pass (domain : postProcess).

material {
    name         : HzbReduce,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture },
        { type : float2,    name : _InputResolution }
    ]
}

fragment {
    void material(inout MaterialInputs m) {
        vec2 halfTexel = 0.5 / _InputResolution;

        float a = texture(_InputTexture, TexCoords + vec2(-halfTexel.x, -halfTexel.y)).r;
        float b = texture(_InputTexture, TexCoords + vec2( halfTexel.x, -halfTexel.y)).r;
        float c = texture(_InputTexture, TexCoords + vec2(-halfTexel.x,  halfTexel.y)).r;
        float d = texture(_InputTexture, TexCoords + vec2( halfTexel.x,  halfTexel.y)).r;

        m.baseColor = vec4(min(min(a, b), min(c, d)), 0.0, 0.0, 1.0);
    }
}
