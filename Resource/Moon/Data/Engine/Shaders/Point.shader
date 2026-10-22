// Topology dots: one sphere of dots instanced once per vertex of the selected mesh, each dot
// placed at the world position of its own instance and coloured with the instance colour.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), but of the kind that owns its
// stages: the sprite radius comes from projection terms and the camera type, the position
// from the instance attribute, and gl_Position is written here, which the generated
// transform cannot express. MoonMatc therefore puts only the prelude, the variant defines
// and the parameters in front of each block - no attribute declarations, no VS_OUT
// varyings, no FRAGMENT_COLOR, no main - and takes both stages verbatim, with the varying
// block and the colour output the legacy file declared.

material {
    name         : Point,
    shadingModel : unlit,
    blending     : opaque,
    culling      : back,
    depthWrite   : true,

    // The point pass has no uniforms of its own: the engine UBO comes from the prelude.
    vertexAttributes : [POSITION, UV0, NORMAL, TANGENT, BITANGENT],

    parameters : [ ]
}

vertex {
    // Mesh vertex plus the two instancing attributes the pass feeds: location 5 is a vertex
    // position of the selected mesh (one instance per vertex), 6 its colour.
    //instance data
    layout (location = 5) in vec3 aInstanceOffset;
    layout (location = 6) in vec3 aInstanceColor;

    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        vec3 Color;
    } vs_out;

    void main()
    { 
        // What the viewport covers at distance d is 2*d/ubo_Projection[1][1] of world
        // space, so a sprite of 'pixels' pixels has a world radius of
        // pixels * 2 * d / (ubo_Projection[1][1] * ubo_ScreenHeight).
        const float pixels = 5.0;
        const float projectY = 2.0 / ubo_Projection[1][1];
        const float ratio = pixels / ubo_ScreenHeight;

        // The instance offset is a vertex of the selected mesh, so it is in the
        // model's own space: the point it places is only in the world after the model
        // matrix. The sprite is built around that world position.
        const vec3 worldPoint = (ubo_Model * vec4(aInstanceOffset, 1.0)).xyz;
        float radius = projectY * ratio;                 // orthographic: constant pixels
        if (ubo_CameraType != 0)                         // perspective: constant pixels too,
            radius *= length(worldPoint - ubo_ViewPos);  // so the world radius grows with d

        vs_out.FragPos = worldPoint + normalize(geo_Pos) * radius;
        vs_out.TexCoords = geo_TexCoords;
        vs_out.Color = aInstanceColor;
        vs_out.Normal = normalize(mat3(transpose(inverse(ubo_Model))) * geo_Normal);

        gl_Position = ubo_Projection * ubo_View *vec4(vs_out.FragPos, 1.0);
    }
}


fragment {
    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        vec3 Color;
    } fs_in;

    out vec4 FRAGMENT_COLOR;

    void main()
    {
        //fs_in.(Normal+1.0)/2.0
        FRAGMENT_COLOR=vec4((fs_in.Normal+1.0)/2.0,1.0);
    }
}
