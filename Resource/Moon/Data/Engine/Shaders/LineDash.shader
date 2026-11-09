// LineDash: a line drawn with a line type, evaluated in the fragment stage.
//
// Same geometry contract as GradientLine (endpoints with a screen-space width, expanded by the
// geometry stage, antialiased edges), but the pattern comes from data:
//
//   uPatternTex  the line type table: one row per distinct pattern, one texel per run.
//                R = run length in pixels, G = 1 for ink / 0 for a gap. A run length of 0
//                marks the end of that row.
//   uPatternRows number of rows, so the row index can be clamped.
//
// Everything a run needs beyond that is per vertex (the effect channel), which is what lets all
// the dashed lines of a frame share one material and one draw call:
//   aExtra.x  distance along the polyline in screen pixels
//   aExtra.y  pattern row
//   aExtra.z  phase in pixels (continues a pattern across runs, and animates marching ants)
//   aExtra.w  period in pixels, so the pattern repeats with a single modulo
//
// A zero-length ink run (a dot) is widened on the CPU, so the loop never runs on a zero-width
// segment.

material {
    name         : LineDash,
    shadingModel : unlit,
    blending     : transparent,
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : mat4,      name : uViewProjMatrix },
        { type : float2,    name : uViewport },
        { type : sampler2D, name : uPatternTex },
        { type : float,     name : uPatternRows, default : 1.0 }
    ]
}

vertex {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
            smooth vec4 m_extra; \
        }

    #define kAntialiasing 2.0

    layout(location = 0) in vec4 aPositionSize;
    layout(location = 1) in vec4 aColor;
    // Effect channel: distance / row / phase / period.
    layout(location = 4) in vec4 aExtra;

    out VertexData vData;

    void main()
    {
        vData.m_color = aColor.abgr; // swizzle to correct endianness
        vData.m_color.a *= smoothstep(0.0, 1.0, aPositionSize.w / kAntialiasing);
        vData.m_size = max(aPositionSize.w, kAntialiasing);
        vData.m_extra = aExtra;
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
    }
}

geometry {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
            smooth vec4 m_extra; \
        }

    #define kAntialiasing 2.0

    layout(lines) in;
    layout(triangle_strip, max_vertices = 4) out;

    in  VertexData vData[];
    out VertexData vDataOut;

    void main()
    {
        vec2 pos0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
        vec2 pos1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;

        vec2 dir = pos0 - pos1;
        dir = normalize(vec2(dir.x, dir.y * uViewport.y / uViewport.x));
        vec2 tng0 = vec2(-dir.y, dir.x);
        vec2 tng1 = tng0 * vData[1].m_size / uViewport;
        tng0 = tng0 * vData[0].m_size / uViewport;

        gl_Position = vec4((pos0 - tng0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
        vDataOut.m_edgeDistance = -vData[0].m_size;
        vDataOut.m_size = vData[0].m_size;
        vDataOut.m_color = vData[0].m_color;
        vDataOut.m_extra = vData[0].m_extra;
        EmitVertex();

        gl_Position = vec4((pos0 + tng0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
        vDataOut.m_edgeDistance = vData[0].m_size;
        vDataOut.m_size = vData[0].m_size;
        vDataOut.m_color = vData[0].m_color;
        vDataOut.m_extra = vData[0].m_extra;
        EmitVertex();

        gl_Position = vec4((pos1 - tng1) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
        vDataOut.m_edgeDistance = -vData[1].m_size;
        vDataOut.m_size = vData[1].m_size;
        vDataOut.m_color = vData[1].m_color;
        vDataOut.m_extra = vData[1].m_extra;
        EmitVertex();

        gl_Position = vec4((pos1 + tng1) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
        vDataOut.m_edgeDistance = vData[1].m_size;
        vDataOut.m_size = vData[1].m_size;
        vDataOut.m_color = vData[1].m_color;
        vDataOut.m_extra = vData[1].m_extra;
        EmitVertex();
    }
}

fragment {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
            smooth vec4 m_extra; \
        }

    #define kAntialiasing 2.0
    #define kMaxRuns 8
    // Softness of the ink/gap boundaries, in pixels. A hard cut makes a dash end look like a
    // stair step on a slanted line; a 1.5 px ramp reads as an antialiased end and is still
    // crisp at normal zoom.
    #define kDashFeather 1.5

    in VertexData vData;
    layout(location = 0) out vec4 fResult;

    void main()
    {
        float s = mod(vData.m_extra.x + vData.m_extra.z, max(vData.m_extra.w, 1e-3));
        int row = clamp(int(vData.m_extra.y + 0.5), 0, int(uPatternRows) - 1);

        // Coverage along the line: 1 well inside an ink run, 0 well inside a gap, and a
        // smoothstep across the boundary. Using the same signed distance on both sides makes the
        // transition continuous (0.5 exactly on the boundary) instead of a step.
        // A period of 0 means the style carries no runs at all (Solid): everything is ink.
        const float period = vData.m_extra.w;
        float coverage = 1.0;
        if (period > 0.0)
        {
            coverage = 0.0;
            float acc = 0.0;
            for (int i = 0; i < kMaxRuns; ++i)
            {
                vec4 run = texelFetch(uPatternTex, ivec2(i, row), 0);
                if (run.r <= 0.0)
                {
                    break; // end of this row's runs
                }
                if (s >= acc && s < acc + run.r)
                {
                    const float edge = min(s - acc, acc + run.r - s);
                    coverage = (run.g > 0.5)
                        ? smoothstep(-kDashFeather * 0.5, kDashFeather * 0.5, edge)
                        : smoothstep(-kDashFeather * 0.5, kDashFeather * 0.5, -edge);
                    break;
                }
                acc += run.r;
            }
        }
        if (coverage <= 0.002)
        {
            discard;
        }

        // The width feather from GizmoLine: keeps the sides antialiased.
        float d = abs(vData.m_edgeDistance) / vData.m_size;
        d = smoothstep(1.0, 1.0 - (kAntialiasing / vData.m_size), d);
        fResult = vec4(vData.m_color.rgb, vData.m_color.a * coverage * d);
    }
}
