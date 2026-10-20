// The topology edge lines: one flat colour for the whole line batch, cut by the section plane
// in the CLIP_PLANE variant.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own
// their main(): the vertex stage declares a flat line id varying of its own besides the block
// it passes, and the fragment stage declares its own output. MoonMatc puts the prelude, the
// variant #defines and the parameters in front of them and generates nothing else for either
// stage.

material {
    name         : GeomertyLine,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,           // the line material draws with culling off
    depthWrite   : false,          // coplanar with their faces: the line pass uses LEQUAL

    // The viewport clips the edges of a sectioned body with the section plane.
    vertexAttributes : [POSITION, UV0, NORMAL, DOMAIN_ID],

    features : [ CLIP_PLANE ],

    parameters : [
        // Nothing sets it and its only use is commented out in the vertex stage, so the 1e-6
        // it used to default to with the initializer was never observable. Kept by name.
        { type : float,         name : u_DepthBias },
        // Unread so far: the colour comes from `color` below, not from the buffer.
        { type : samplerBuffer, name : lineColorTex },
        { type : float3,        name : color }
    ]
}

vertex {

out VS_OUT
{
    vec3 FragPos;
    vec2 DomainId;

} vs_out;
flat out int lineId;

void main()
{
    vs_out.FragPos = vec3(ubo_Model * vec4(geo_Pos, 1.0));
    vs_out.DomainId=geo_TexCoords;
    lineId=int(round(geo_TexCoords.x));
    gl_Position = ubo_Projection * ubo_View * vec4(vs_out.FragPos, 1.0);
    // Nudge the NDC depth of the line slightly toward the camera so it always
    // passes the depth test against its coplanar faces (z-fighting).
    //gl_Position.z -= u_DepthBias * gl_Position.w;
}
}

fragment {
#include ":Shaders/Lighting/PBR.glsl"

in VS_OUT
{
    vec3 FragPos;
    vec2 DomainId;
} fs_in;
flat in int lineId;
out vec4 FRAGMENT_COLOR;
void main()
{
#if defined(CLIP_PLANE)
    if(ubo_enableClip==1){
        float offsetw=dot(ubo_plane.xyz,fs_in.FragPos)+ubo_plane.w;
        if(offsetw>0){
            discard;
        }    
    }
#endif
    FRAGMENT_COLOR = vec4(color, 1);
}
}
