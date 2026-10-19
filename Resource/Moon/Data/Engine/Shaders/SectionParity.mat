// Section parity fill: INVERTs the stencil for every surface layer between the eye and the
// clip plane, so the cap pass can tell by parity whether the plane cuts the model at a pixel.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md): only the state in `material { }`
// and one entry point that fills in MaterialInputs. The old vertex stage was the plain MVP
// transform, which is exactly what MoonMatc generates, so it is gone; the fragment reads the
// world position from the generated fs_in.

material {
    name         : SectionParity,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [ ]
}

fragment {
    void material(inout MaterialInputs m) {
        // A surface crossing is closer to the eye than the clip plane iff the
        // fragment lies on the same side of the plane as the camera. Crossing the
        // plane puts the fragment on the opposite side, so discard it. The stencil
        // pass then INVERTs the parity for every remaining surface layer.
        float fFrag = dot(ubo_plane.xyz, fs_in.FragPos) + ubo_plane.w;
        float fEye = dot(ubo_plane.xyz, ubo_ViewPos) + ubo_plane.w;
        if (fFrag * fEye <= 0.0) discard;

        m.baseColor = vec4(1.0);
    }
}
