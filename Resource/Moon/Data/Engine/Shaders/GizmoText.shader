// Gizmo text: a glyph quad drawn in world space.
//
// The glyphs come from the same freetype atlas the 2D overlay uses (one atlas per requested
// size), so the quad carries an atlas rectangle in uv and the fragment stage samples the
// glyph's alpha out of it. The quad itself is built on the CPU - four corners in world space,
// oriented by the caller - which keeps text inside the same vertex-list model as every other
// gizmo primitive: nothing is cached between frames.

material {
    name         : GizmoText,
    shadingModel : unlit,
    blending     : transparent,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : mat4,      name : uViewProjMatrix },
        { type : sampler2D, name : uAtlas }
    ]
}

vertex {
    layout(location = 0) in vec4 aPositionSize;
    layout(location = 1) in vec4 aColor;
    layout(location = 3) in vec2 aUv;

    out vec2 vUv;
    out vec4 vColor;

    void main()
    {
        vUv = aUv;
        vColor = aColor.abgr;
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
    }
}

fragment {
    in vec2 vUv;
    in vec4 vColor;

    out vec4 fragColor;

    void main()
    {
        vec4 glyph = texture(uAtlas, vUv);
        fragColor = vec4(vColor.rgb, vColor.a * glyph.a);
    }
}
