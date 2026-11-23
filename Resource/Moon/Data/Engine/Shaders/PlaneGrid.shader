// PlaneGrid: the sketch's background grid, evaluated per pixel on a quad of the sketch plane.
//
// The quad is geometry, not a full-screen pass. Its four corners are the visible part of the
// plane, and the pieces that run past a clip plane are handed to the vertex stage already slid
// onto that plane - exactly what the line-built grid did with its segments
// (SketchObjWidget::drawBackground). The depth therefore comes from the vertex pipeline, like
// every other overlay primitive: the model occludes the grid where it stands in front of the
// plane, and the grid is never cut away at the edge of the frustum.
//
// The pattern itself is still evaluated per pixel, from the plane coordinates carried in the uv
// attribute: one-pixel lines at any zoom (the width comes from the screen-space derivatives, not
// from geometry thickness), two levels that fade in and out with their own on-screen spacing
// instead of popping a whole density step at once, and no moire on a grazing view.

material {
    name         : PlaneGrid,
    shadingModel : unlit,
    blending     : transparent,
    culling      : none,
    // Same depth state as the line and triangle overlay materials: tested against the scene and
    // written where the grid covers the plane, so the axes (nudged a hair towards the camera) and
    // the model settle the comparison the way they do for any other primitive.
    depthWrite   : true,
    depthTest    : true,

    parameters : [
        { type : mat4,   name : uViewProjMatrix },
        { type : float4, name : uMinorColor, default : vec4(0.55, 0.55, 0.55, 0.18) },
        { type : float4, name : uMajorColor, default : vec4(0.45, 0.45, 0.45, 0.35) },
        { type : float,  name : uStep, default : 10.0 },
        { type : float,  name : uLineWidthPx, default : 1.0 },
        // The major lines are the same hairlines, a little heavier: 1.5x the minor width.
        { type : float,  name : uMajorWidthScale, default : 1.5 }
    ]
}

vertex {
    layout(location = 0) in vec4 aPositionSize;
    // The plane's (u, v), in sketch units; the pattern is evaluated from it.
    layout(location = 3) in vec2 aUv;

    out vec2 vUv;

    void main()
    {
        vUv = aUv;
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
    }
}

fragment {
    layout(location = 0) out vec4 fResult;
    in vec2 vUv;

    // The on-screen cell size below which a level starts to dissolve, and the one where it is
    // gone. Kept low on purpose: a level that fades out at ten pixels leaves the middle distance
    // empty, which reads as the grid stopping rather than receding.
    const float kGridFadeStartPx = 4.0;
    const float kGridFadeEndPx = 1.2;

    // Antialiased grid lines of one cell size, in the plane's (u, v) coordinates.
    // p_widthPx is the line's thickness in pixels; the falloff is one pixel wide, centred on the
    // line's edge, so a 1 px line reads as a hairline instead of a two pixel smear.
    float gridMask(vec2 p_uv, float p_step, float p_widthPx)
    {
        vec2 cell = p_uv / p_step;
        // Cells per pixel. Its reciprocal is the on-screen spacing, and it also grows without
        // bound towards the horizon - which is exactly the signal to thin the level out.
        vec2 cellPerPixel = fwidth(cell);
        float spacingPx = 1.0 / max(max(cellPerPixel.x, cellPerPixel.y), 1e-8);
        // The level stays at full strength until its cells are a few pixels across and only
        // dissolves below one - the grid has to be readable deep into the distance, and fading
        // everything under ten pixels is what made it vanish half way to the horizon.
        float fade = smoothstep(kGridFadeEndPx, kGridFadeStartPx, spacingPx);

        // Distance to the nearest cell boundary, in pixels.
        vec2 f = fract(cell);
        vec2 toBoundary = min(f, 1.0 - f) / max(cellPerPixel, vec2(1e-8));
        float distance = min(toBoundary.x, toBoundary.y);

        const float halfWidth = p_widthPx * 0.5;
        float line = 1.0 - smoothstep(halfWidth - 0.5, halfWidth + 0.5, distance);
        return line * fade;
    }

    void main()
    {
        float minor = gridMask(vUv, uStep, uLineWidthPx);
        float major = gridMask(vUv, uStep * 5.0, uLineWidthPx * uMajorWidthScale);

        float minorAlpha = minor * uMinorColor.a;
        float majorAlpha = major * uMajorColor.a;
        float alpha = max(minorAlpha, majorAlpha);
        if (alpha <= 0.002)
        {
            discard;
        }
        vec3 color = majorAlpha > minorAlpha ? uMajorColor.rgb : uMinorColor.rgb;
        fResult = vec4(color, alpha);
    }
}
