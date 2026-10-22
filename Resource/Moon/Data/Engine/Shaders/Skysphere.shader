// Skysphere: draws a sky texture on a sphere centred on the camera, as the scene background.
// Its OUTLINE_PASS and PICKING_PASS variants exist but discard, so the sky can be neither picked
// nor outlined.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own
// their main() because the skeleton's fixed transform cannot express them: the vertex stage
// swaps in a unit ortho projection for an orthographic camera (the sphere sits on the camera, so
// the real near plane would clip it), and the fragment stage declares its own varying and output.
// Nothing is generated for these stages - the attributes, the VS_OUT block, the output and the
// mains below are the whole stage.
//
// The uniforms used to have declaration defaults (u_Diffuse = vec4(1.0), u_TextureTiling =
// vec2(1.0), u_TextureOffset = vec2(0.0)); the DSL cannot carry a default, and every one of them
// is set from Resource/Moon/Data/Engine/Materials/Skysphere.ovmat, so dropping them changes
// nothing.

material {
    name         : Skysphere,
    shadingModel : unlit,
    // Skysphere.ovmat asks for front-face culling with back-face culling off (the mesh is seen
    // from the inside), no depth test and no depth writes.
    blending     : opaque,
    culling      : front,
    depthWrite   : false,

    passes : [ OUTLINE_PASS, PICKING_PASS ],

    parameters : [
        { type : float4,    name : u_Diffuse, label : "Diffuse Color", group : "Base", color : true },
        { type : sampler2D, name : u_DiffuseMap, label : "Diffuse Map", group : "Maps" },
        { type : float2,    name : u_TextureTiling, label : "Tiling", group : "UV" },
        { type : float2,    name : u_TextureOffset, label : "Offset", group : "UV" },
        // Each extra pass reads only its own colour; the other one is dropped by the driver.
        { type : float4,    name : _PickingColor, ui : false },
        { type : float4,    name : _OutlineColor, ui : false }
    ]
}

vertex {
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;

    out VS_OUT
    {
        vec3 dir;
    } vs_out;

    void main()
    {
        vs_out.dir = geo_Pos;
        mat4 p = ubo_Projection;
        if (ubo_CameraType == 0)
        {
            // The unit skysphere mesh is centered on the camera. An ortho camera's near plane
            // would clip it, so use a unit ortho projection that exactly fits the mesh.
            p = mat4(
                2.0, 0.0, 0.0, 0.0,
                0.0, 2.0, 0.0, 0.0,
                0.0, 0.0, 2.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            );
        }
        gl_Position = p *ubo_View  * vec4(ubo_ViewPos +  geo_Pos, 1.0);
    }
}

fragment {
    in VS_OUT
    {
        vec3 dir;
    } fs_in;

    out vec4 FRAGMENT_COLOR;
    #define PI         3.14159265358979323
    #define INV_PI     0.31830988618379067
    #define INV_TWO_PI 0.15915494309189533
    void main()
    {
    #if defined(PICKING_PASS)
        // The skysphere cannot be picked.
        discard;
    #elif defined(OUTLINE_PASS)
        // The skysphere cannot be outlined.
        discard;
    #else
        vec3 d=normalize(fs_in.dir);
        float theta = acos(clamp(d.y, -1.0, 1.0));
        vec2 uv = vec2((PI + atan(d.z, d.x)) * INV_TWO_PI, 1.0-theta * INV_PI);

        vec2 texCoords = TileAndOffsetTexCoords(uv, u_TextureTiling, u_TextureOffset);
        FRAGMENT_COLOR = texture(u_DiffuseMap, texCoords) * u_Diffuse;
    #endif
    }
}
