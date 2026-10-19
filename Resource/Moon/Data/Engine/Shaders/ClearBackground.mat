// Sky background: a full-screen quad filled either with a single colour (the PureColor
// feature) or with the vertical gradient between uBottomColor and uTopColor.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), but of the kind that owns its
// stages: the pass hands the quad over in clip space and gl_Position is written here, which
// the generated transform cannot express. MoonMatc therefore puts only the prelude, the
// variant defines and the parameters in front of each block - no attributes, no varyings, no
// output, no main - and takes both stages verbatim, as the legacy file had them.

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
        { type : float4, name : uBottomColor }
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


    void main()
    {
    #if !defined(PureColor)
       // Distance from the centre, normalised so the quad corners are 1: the band the
       // two colours are mixed over.
       float t=length(TexCoords-vec2(0.5,0.5))/0.7071;
       FragColor =mix(uBottomColor,uTopColor,t);
    #else
        FragColor = uClearColor;
    #endif

    }
}
