// Section contour: the curve where a mesh crosses the active clip plane, drawn as a thin line.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), with all three stages written by
// this file. The geometry stage emits the two points where a triangle crosses ubo_plane, which
// no "adjust the attributes, then the fixed transform" contract can express - a geometry stage
// is always raw for that reason. The vertex stage is raw as well: the geometry stage consumes
// the world position the legacy vertex main passes on (a plain `out vec3 FragPos`, not the
// generated VS_OUT block), so both stages keep the interface the old file wrote. The fragment
// stage only writes a flat colour, but it is raw too, so the geometry stage that only feeds
// gl_Position does not have to declare a VS_OUT block it has no use for.
//
// The maths is the legacy maths: ubo_plane signs give the crossing, the points are mixed on the
// edge, and the depth is nudged towards the camera by u_DepthBias so the line wins against the
// surfaces it lies on.

material {
    name         : SectionContour,
    shadingModel : unlit,
    // SectionContourRenderPass: no culling, depth writing off, depth test on (GREATER_EQUAL),
    // blending untouched.
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    vertexAttributes : [POSITION],

    parameters : [
        // Both keep the initializer they were declared with: nothing in the draw site sets
        // u_DepthBias, so the GLSL declaration-time value is the only thing that gives it one.
        { type : float,  name : u_DepthBias, default : 1e-6 },
        { type : float3, name : color,       default : vec3(1.0, 0.0, 1.0) }
    ]
}

vertex {

    out vec3 FragPos;

    void main()
    {
        FragPos = vec3(ubo_Model * vec4(geo_Pos, 1.0));
        gl_Position = ubo_Projection * ubo_View * vec4(FragPos, 1.0);
    }
}

geometry {
    layout(triangles) in;
    layout(line_strip, max_vertices = 2) out;

    in vec3 FragPos[];

    void main()
    {
        float d[3];
        for (int i = 0; i < 3; i++)
            d[i] = dot(ubo_plane.xyz, FragPos[i]) + ubo_plane.w;

        int posCnt = 0;
        int negCnt = 0;
        for (int i = 0; i < 3; i++)
        {
            if (d[i] >= 0.0) posCnt++;
            else negCnt++;
        }
        if (posCnt == 0 || negCnt == 0) return;

        // Emit the segment where the triangle crosses the plane.
        for (int i = 0; i < 3; i++)
        {
            int j = (i + 1) % 3;
            if ((d[i] >= 0.0) != (d[j] >= 0.0))
            {
                float t = d[i] / (d[i] - d[j]);
                vec3 p = mix(FragPos[i], FragPos[j], t);
                gl_Position = ubo_Projection * ubo_View * vec4(p, 1.0);
                // Nudge the depth toward the camera so coplanar lines win against
                // the surfaces they lie on.
                gl_Position.z -= u_DepthBias * gl_Position.w;
                EmitVertex();
            }
        }
        EndPrimitive();
    }
}

fragment {
    out vec4 FRAGMENT_COLOR;

    void main()
    {
        FRAGMENT_COLOR = vec4(color, 1.0);
    }
}
