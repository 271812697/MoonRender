// G-buffer: the geometry pass of the SSAO chain, writing view-space position, view-space
// normal and a flat albedo into three colour attachments.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). Both stages are written by this
// file: it has to fill its own three outputs, and the position it hands the SSAO pass has to
// be in VIEW space (ubo_View * ubo_Model), which is not the transform the generated skeleton
// applies.

material {
    name         : gbuffer,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    parameters : [ ]
}

vertex {
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;
    layout (location = 2) in vec3 geo_Normal;
    layout (location = 3) in vec3 geo_Tangent;
    layout (location = 4) in vec3 geo_Bitangent;

    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;

    } vs_out;

    void main()
    {
        vs_out.FragPos = vec3(ubo_View * ubo_Model * vec4(geo_Pos, 1.0));
        vs_out.TexCoords = geo_TexCoords;
        vs_out.Normal = normalize(transpose(inverse(mat3(ubo_View * ubo_Model))) * geo_Normal);
        gl_Position = ubo_Projection * vec4(vs_out.FragPos, 1.0);
    }
}

fragment {
    layout (location = 0) out vec3 gPosition;
    layout (location = 1) out vec3 gNormal;
    layout (location = 2) out vec3 gAlbedo;

    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;

    } fs_in;

    void main()
    {
        gPosition = fs_in.FragPos;
        gNormal = normalize(fs_in.Normal);
        gAlbedo.rgb = vec3(0.95);
    }
}
