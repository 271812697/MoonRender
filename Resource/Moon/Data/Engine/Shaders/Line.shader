// Line: the material that draws a flat white line, with no shading of its own.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md): only the state in `material { }`
// and one entry point that fills in MaterialInputs. The old vertex stage was the plain MVP
// transform, which is exactly what MoonMatc generates, so it is gone.

material {
    name         : Line,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    parameters : [ ]
}

fragment {
    // Kept from the old fragment stage: nothing here uses it yet, but the material has always
    // been compiled with the PBR helpers in scope, and MoonMatc now expands the include (the
    // two headers the prelude already carries are dropped for us).
    #include ":Shaders/Lighting/PBR.glsl"

    void material(inout MaterialInputs m) {
        m.baseColor = vec4(1, 1, 1, 1);
    }
}
