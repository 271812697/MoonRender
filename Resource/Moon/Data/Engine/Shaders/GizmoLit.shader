// Gizmo lit triangles: the shaded faces of the immediate-mode overlay.
//
// The widgets that draw through WidgetViewData carry triangle soups with per-vertex normals
// computed once when the geometry was built (TriangleFace::RebuildNormals), so this material
// only has to turn that normal into light.
//
// The light rig lives in the camera's frame, not the world's: the key light comes from the
// upper-left of the screen and the fill from the lower-right, both mostly from behind the
// camera. That is what keeps a handle readable from every orbit angle - a world-fixed light
// makes the same plate bright from one side and nearly black from another. Brightness is kept
// in a narrow band (~0.75 .. ~1.0 of the vertex colour) on purpose: dark sides read as noise on
// a tool, blown-out sides lose the colour coding.
//
// Two-sided by construction: culling is off and the normal is flipped for back faces, because a
// triangle soup carries no guaranteed winding and the model matrix may mirror.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own their main():
// the vertex stage writes gl_Position from a pre-multiplied view-projection, the fragment stage
// writes its own single output.

material {
    name         : GizmoLit,
    shadingModel : unlit,

    // The same state as the unlit gizmo triangles: the vertex colour carries the alpha (hover /
    // drag highlights) and the gizmo draws both faces, so blending is on and culling is off.
    // Depth writes stay on, so a shaded plate occludes the handles drawn after it.
    blending     : transparent,
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : mat4,   name : uViewProjMatrix },
        // Camera position and basis in world space: the rim term needs the former, the
        // screen-aligned light rig needs the latter.
        { type : float3, name : u_ViewPos },
        { type : float3, name : u_CameraRight },
        { type : float3, name : u_CameraUp }
    ]
}

vertex {
    // aUv is unused but kept: the triangle draw shares one vertex buffer and one vertex format
    // with the rest of the immediate-mode renderer.
    layout(location=0) in vec4 aPositionSize;
    layout(location=1) in vec4 aColor;
    layout(location=2) in vec3 aNormal;
    layout(location=3) in vec2 aUv;

    out vec3 vNormal;
    out vec3 vWorldPos;
    out vec4 vColor;

    void main()
    {
        vColor = aColor.abgr; // swizzle to correct endianness
        vNormal = aNormal;
        vWorldPos = aPositionSize.xyz;
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
    }
}

fragment {
    in vec3 vNormal;
    in vec3 vWorldPos;
    in vec4 vColor;

    layout(location=0) out vec4 fResult;

    // The shading constants. Tuning the look means touching these five numbers only.
    const float kAmbient   = 0.52;  // floor: no orientation ever goes below it, handles never go black
    const float kKeyLight  = 0.40;  // key light, upper-left of the screen
    const float kFillLight = 0.10;  // fill light, lower-right of the screen
    const float kRimLight  = 0.12;  // rim: keeps the silhouette of faces turned away
    const float kMaxShade  = 1.05;  // guard so a later tweak cannot clip the handles to white

    void main()
    {
        vec3 N = normalize(vNormal);
        if (!gl_FrontFacing)
        {
            // The triangle soup has no winding contract: shade the side that faces the camera.
            N = -N;
        }

        vec3 V = normalize(u_ViewPos - vWorldPos);
        vec3 right = normalize(u_CameraRight);
        vec3 up = normalize(u_CameraUp);

        // Both lights sit in front of the surface (the V term), offset to the upper-left and
        // lower-right of the screen. Facing the camera is therefore always well lit, and the
        // variation comes from how far the face turns away - not from where the object happens
        // to sit in the world.
        vec3 keyDir  = normalize(right * -0.40 + up *  0.55 + V * 0.72);
        vec3 fillDir = normalize(right *  0.65 + up * -0.35 + V * 0.45);

        // Half-Lambert: [-1, 1] -> [0, 1], so a face turning away falls off smoothly instead of
        // clamping to zero.
        float key  = 0.5 + 0.5 * dot(N, keyDir);
        float fill = 0.5 + 0.5 * dot(N, fillDir);

        // Rim only on fragments seen at grazing angles, i.e. on the silhouette.
        float facing = clamp(dot(N, V), 0.0, 1.0);
        float rim = pow(1.0 - facing, 3.0);

        float shade = kAmbient + kKeyLight * key + kFillLight * fill + kRimLight * rim;
        shade = min(shade, kMaxShade);

        fResult = vec4(vColor.rgb * shade, vColor.a);
    }
}
