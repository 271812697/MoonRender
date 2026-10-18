// Gizmo line: a screen-space thick line with antialiased edges.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), with all three stages written by
// this file: the generated skeletons cannot host it, because the geometry stage expands one
// line into a screen-space triangle strip, which no "adjust the attributes, then the fixed
// transform" contract can express. The vertex stage keeps the endpoints and their size, the
// geometry stage turns them into a quad, and the fragment stage feathers the edges.

material {
    name         : GizmoLine,
    shadingModel : unlit,
    blending     : transparent,
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : mat4,   name : uViewProjMatrix },
        { type : float2, name : uViewport }
    ]
}

vertex {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
        }

    #define kAntialiasing 2.0

    layout(location = 0) in vec4 aPositionSize;
    layout(location = 1) in vec4 aColor;

    out VertexData vData;

    void main()
    {
        vData.m_color = aColor.abgr; // swizzle to correct endianness
        vData.m_color.a *= smoothstep(0.0, 1.0, aPositionSize.w / kAntialiasing);
        vData.m_size = max(aPositionSize.w, kAntialiasing);
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
    }
}

geometry {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
        }

    #define kAntialiasing 2.0

    // expand line -> triangle strip
    layout(lines) in;
    layout(triangle_strip, max_vertices = 4) out;

    in  VertexData vData[];
    out VertexData vDataOut;

    void main()
    {
        vec2 pos0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
        vec2 pos1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;

        vec2 dir = pos0 - pos1;
        dir = normalize(vec2(dir.x, dir.y * uViewport.y / uViewport.x)); // correct for aspect ratio
        vec2 tng0 = vec2(-dir.y, dir.x);
        vec2 tng1 = tng0 * vData[1].m_size / uViewport;
        tng0 = tng0 * vData[0].m_size / uViewport;

        // line start
        gl_Position = vec4((pos0 - tng0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
        vDataOut.m_edgeDistance = -vData[0].m_size;
        vDataOut.m_size = vData[0].m_size;
        vDataOut.m_color = vData[0].m_color;
        EmitVertex();

        gl_Position = vec4((pos0 + tng0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
        vDataOut.m_color = vData[0].m_color;
        vDataOut.m_edgeDistance = vData[0].m_size;
        vDataOut.m_size = vData[0].m_size;
        EmitVertex();

        // line end
        gl_Position = vec4((pos1 - tng1) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
        vDataOut.m_edgeDistance = -vData[1].m_size;
        vDataOut.m_size = vData[1].m_size;
        vDataOut.m_color = vData[1].m_color;
        EmitVertex();

        gl_Position = vec4((pos1 + tng1) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
        vDataOut.m_color = vData[1].m_color;
        vDataOut.m_size = vData[1].m_size;
        vDataOut.m_edgeDistance = vData[1].m_size;
        EmitVertex();
    }
}

fragment {
    #define VertexData \
        _VertexData { \
            noperspective float m_edgeDistance; \
            noperspective float m_size; \
            smooth vec4 m_color; \
        }

    #define kAntialiasing 2.0

    in VertexData vData;
    layout(location = 0) out vec4 fResult;

    void main()
    {
        fResult = vData.m_color;
        float d = abs(vData.m_edgeDistance) / vData.m_size;
        d = smoothstep(1.0, 1.0 - (kAntialiasing / vData.m_size), d);
        fResult.a *= d;
    }
}
