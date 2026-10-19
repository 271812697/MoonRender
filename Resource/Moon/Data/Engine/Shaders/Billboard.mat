// Billboard: a camera-facing sprite - the texture, tinted, scaled with the distance to the
// camera so it keeps its screen size.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), with both stages written by this
// file. The vertex stage cannot use the generated transform: it strips the rotation out of the
// model-view matrix (so the quad always faces the camera) and multiplies the local position by
// the distance to the camera, which no "adjust the attributes, then the fixed MVP" contract can
// express. The fragment stage keeps its own loose VS_OUT input to match the block the vertex
// stage writes, so it is a raw stage too. MoonMatc generates nothing for either stage beyond
// the prelude, the variant #defines and the parameters.
//
// State: there is no draw site for this shader in the tree any more (it is only registered in
// EditorResources), so the three state fields are a reading of what a blended sprite needs, not
// a copy of a SetBlendable/SetDepthWriting call: transparent because the tint's alpha is
// blended, culling none because a camera-facing quad's winding is not meaningful, depthWrite
// false because the sprite is alpha blended.

material {
    name         : Billboard,
    shadingModel : unlit,
    blending     : transparent,
    culling      : none,
    depthWrite   : false,

    parameters : [
        // How much the sprite grows with the distance to the camera.
        { type : float,     name : u_Scale,         default : 1.0 },
        // The tint, the map and the tiling that repeats it across the quad. The draw site
        // would have set these; the old declaration-time defaults are carried over so an
        // untouched property keeps the value it always had.
        { type : float4,    name : u_Diffuse,       default : vec4(1.0, 1.0, 1.0, 1.0) },
        { type : sampler2D, name : u_DiffuseMap },
        { type : float2,    name : u_TextureTiling, default : vec2(1.0, 1.0) },
        { type : float2,    name : u_TextureOffset, default : vec2(0.0, 0.0) }
    ]
}

vertex {
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;
    layout (location = 2) in vec3 geo_Normal;

    out VS_OUT
    {
        vec2 TexCoords;
    } vs_out;

    void main()
    {
        vs_out.TexCoords = geo_TexCoords;

        mat4 model = ubo_Model;
        float distanceToCamera = distance(ubo_ViewPos, model[3].xyz);

        mat4 modelView = ubo_View * model;

        // Column 0:
        modelView[0][0] = 1;
        modelView[0][1] = 0;
        modelView[0][2] = 0;

        // Column 1:
        modelView[1][0] = 0;
        modelView[1][1] = 1;
        modelView[1][2] = 0;

        // Column 2:
        modelView[2][0] = 0;
        modelView[2][1] = 0;
        modelView[2][2] = 1;

        gl_Position = ubo_Projection * modelView * vec4(geo_Pos * distanceToCamera * u_Scale, 1.0);
    }
}

fragment {
    out vec4 FRAGMENT_COLOR;

    in VS_OUT
    {
        vec2 TexCoords;
    } fs_in;

    void main()
    {
        FRAGMENT_COLOR = texture(u_DiffuseMap, u_TextureOffset + vec2(mod(fs_in.TexCoords.x * u_TextureTiling.x, 1), mod(fs_in.TexCoords.y * u_TextureTiling.y, 1))) * u_Diffuse;
    }
}
