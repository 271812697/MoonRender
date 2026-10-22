// Mirror-plane shadow: the opaques of the scene flattened onto the mirror plane, drawn into
// a buffer the grid pass blurs and composites over the reflection.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), but of the kind that owns its
// stages: the world position is projected onto uMirroPlane and gl_Position is written here,
// which the generated model-view-projection transform cannot express. MoonMatc therefore
// puts only the prelude, the variant defines and the parameters in front of each block - no
// attributes, no varyings, no output, no main - and takes both stages verbatim, as the
// legacy file had them.

material {
    name         : MirrorPlaneShadow,
    shadingModel : unlit,
    blending     : opaque,
    culling      : back,     // the pass draws with SetBackfaceCulling(true)
    depthWrite   : false,    // ... and with depth testing and depth writing off

    vertexAttributes : [POSITION, UV0, NORMAL, TANGENT, BITANGENT],

    parameters : [
        { type : float4, name : uMirroPlane }
    ]
}

vertex {

    void main()
    {
        // Signed distance to the plane, then back along its normal by that distance: every
        // vertex lands on its own projection, so the mesh becomes a flat shadow.
        vec3 FragPos = vec3( ubo_Model * vec4(geo_Pos, 1.0));
        float offsetw=dot(uMirroPlane.xyz,FragPos)+uMirroPlane.w;
        FragPos = FragPos-offsetw*uMirroPlane.xyz;
        gl_Position = ubo_Projection * ubo_View *vec4(FragPos, 1.0);
    }
}

fragment {
    out vec4 FragColor;

    void main()
    {
       FragColor=vec4(1,0,0,1);
    }
}
