// Gizmo points: one round, antialiased sprite per point the immediate-mode gizmo draws.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own their main():
// the vertex stage writes gl_Position from a pre-multiplied view-projection and has to set
// gl_PointSize itself, and the fragment stage decorates the sprite with gl_PointCoord and
// writes its own single output. The generated transform and skeleton cannot express any of
// that, so MoonMatc takes the two blocks verbatim and only puts the prelude and the declared
// parameters in front of them - no attribute declarations, no VS_OUT, no FRAGMENT_COLOR, no
// main.

material {
    name         : GizmoPoint,
    shadingModel : unlit,

    // The immediate-mode gizmo pass draws with SRC_ALPHA / ONE_MINUS_SRC_ALPHA, and the
    // fragment stage modulates the sprite's alpha to soften its edge, so the state is
    // transparent rather than opaque; culling is off and depth writes stay on.
    blending     : transparent,
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : mat4, name : uViewProjMatrix }
    ]
}

vertex {
    // The varying block is written as a macro because the two stages have to agree on it
    // element for element (qualifiers included).
    #define VertexData \
    	_VertexData { \
    		noperspective float m_edgeDistance; \
    		noperspective float m_size; \
    		smooth vec4 m_color; \
    	}

    #define kAntialiasing 2.0

    layout(location=0) in vec4 aPositionSize;
    layout(location=1) in vec4 aColor;

    out VertexData vData;

    void main() 
    {
        vData.m_color = aColor.abgr; // swizzle to correct endianness
        vData.m_color.a *= smoothstep(0.0, 1.0, aPositionSize.w / kAntialiasing);
        vData.m_size = max(aPositionSize.w, kAntialiasing);
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
        gl_PointSize = vData.m_size;
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
    layout(location=0) out vec4 fResult;

    void main() 
    {
        fResult = vData.m_color;
        float d = length(gl_PointCoord.xy - vec2(0.5));
        d = smoothstep(0.5, 0.5 - (kAntialiasing / vData.m_size), d);
        fResult.a *= d;
    }
}
