// Topology vertex dots: one round point sprite per vertex of a shape.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md): the state and
// the parameters in `material { }`, and the shading as an entry point that only fills in
// MaterialInputs. The transform, the varying and main() are put in by MoonMatc from the
// shader prelude; the one thing the skeleton has no way to write is the sprite size, which is
// why the vertex block exists at all.

material {
    name         : GeomertyPoint,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    // The clip-plane variant the viewport draws; without it the pass does not clip.
    features : [ CLIP_PLANE ],

    parameters : [
        { type : float,  name : u_PointSize },
        { type : float4, name : color }
    ]
}

vertex {
    // gl_PointSize is a vertex-stage output of its own rather than part of the geometry
    // transform, so the skeleton leaves it alone: without this the driver draws a single
    // pixel instead of the dot the material asks for.
    void materialVertex(inout vec3 position, inout vec2 texCoords, inout vec3 normal) {
        gl_PointSize = u_PointSize;
    }
}

fragment {
    void material(inout MaterialInputs m) {
    #if defined(CLIP_PLANE)
        if (ubo_enableClip == 1) {
            float offsetw = dot(ubo_plane.xyz, fs_in.FragPos) + ubo_plane.w;
            if (offsetw > 0) {
                discard;
            }
        }
    #endif

        // A round dot, softly cut at the border: the square a point sprite is would
        // otherwise read as a tiny quad at the corner of the body.
        float d = length(gl_PointCoord - vec2(0.5));
        if (d > 0.5) {
            discard;
        }
        float edge = smoothstep(0.5, 0.40, d);
        m.baseColor = vec4(color.rgb, color.a * edge);
    }
}
