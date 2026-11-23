// Sky background: a full-screen quad filled either with a single colour (the PureColor
// feature) or with a backdrop gradient.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), but of the kind that owns its
// stages: the pass hands the quad over in clip space and gl_Position is written here, which
// the generated transform cannot express. MoonMatc therefore puts only the prelude, the
// variant defines and the parameters in front of each block - no attributes, no varyings, no
// output, no main - and takes both stages verbatim, as the legacy file had them.
//
// The gradient is fixed to the screen: the ramp is a function of the pixel's height, so moving
// the camera never moves the backdrop. A ramp taken from the view direction (a "dome") turns
// with the camera instead, and the moving ramp sliding under the fixed dither pattern reads as
// flicker while the view is being dragged. The colours are picked as they are seen and
// converted to linear here, because this pass writes the linear buffer the tonemap encodes
// afterwards.

material {
    name         : ClearBackground,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,     // the present quad is drawn with culling off
    depthWrite   : false,    // ... and with depth testing and depth writing off

    // The flat-fill variant; without it the gradient runs.
    features : [ PureColor ],

    parameters : [
        { type : float4, name : uClearColor },
        { type : float4, name : uTopColor },
        { type : float4, name : uBottomColor },
        // Screen height (0 = bottom of the viewport, 1 = top) where the two colours meet, and
        // how much of the height the whole ramp covers.
        { type : float,  name : uGradientCenter,   default : 0.5 },
        { type : float,  name : uGradientSpread,   default : 1.0 },
        // > 1 keeps the dark end around longer and pushes the bright end towards the top.
        { type : float,  name : uGradientCurve,    default : 1.0 },
        // Screen-space corner darkening; 0 draws none.
        { type : float,  name : uGradientVignette, default : 0.06 }
    ]
}

vertex {
    layout(location = 0) in vec2 geo_Pos;
    layout(location = 1) in vec2 geo_TexCoords;

    out vec2 TexCoords;

    void main()
    {
        TexCoords = geo_TexCoords;
        gl_Position = vec4(geo_Pos, 1.0, 1.0);
    }
}

fragment {
    in vec2 TexCoords;

    out vec4 FragColor;

    vec3 ToLinear(vec3 color)
    {
        // The panel edits these as screen colours; the frame buffer holds linear values and the
        // tonemap encodes them afterwards, so one conversion is needed here.
        return pow(max(color, vec3(0.0)), vec3(2.2));
    }

    void main()
    {
    #if defined(PureColor)
        FragColor = uClearColor;
    #else
        // Height of this pixel in the viewport: 0 at the bottom row, 1 at the top one (the
        // present quad's UVs run that way). Taken from the quad and not from the engine block on
        // purpose: nothing about this backdrop may depend on the frame - no view matrix, no
        // screen size - so no camera move can make it change.
        const float screenY = clamp(TexCoords.y, 0.0, 1.0);
        float t = clamp((screenY - uGradientCenter) / max(uGradientSpread, 0.001) + 0.5, 0.0, 1.0);
        t = pow(t, max(uGradientCurve, 0.05));
        vec3 color = mix(ToLinear(uBottomColor.rgb), ToLinear(uTopColor.rgb), t);

        // Faint corner darkening: the corners are the farthest the picture gets from the middle,
        // and that little falloff is what gives a flat backdrop depth. It starts well outside
        // the middle of the screen so the ramp itself keeps its contrast.
        if (uGradientVignette > 0.0)
        {
            const vec2 p = abs(TexCoords - 0.5) * 2.0;
            color *= 1.0 - uGradientVignette * smoothstep(0.90, 1.35, length(p));
        }

        FragColor = vec4(color, 1.0);
    #endif
    }
}
