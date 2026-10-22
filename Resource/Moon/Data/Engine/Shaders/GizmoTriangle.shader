// Gizmo triangles: the flat, vertex-coloured triangles of the immediate-mode gizmo (the
// picking quads and the axis plates).
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own their main():
// the vertex stage writes gl_Position from a pre-multiplied view-projection, and the fragment
// stage writes its own single output. The generated transform and skeleton cannot express
// either, so MoonMatc takes the two blocks verbatim and only puts the prelude and the declared
// parameters in front of them - no attribute declarations, no VS_OUT, no FRAGMENT_COLOR, no
// main.

material {
    name         : GizmoTriangle,
    shadingModel : unlit,

    // The immediate-mode gizmo pass draws with SRC_ALPHA / ONE_MINUS_SRC_ALPHA and the vertex
    // colour carries the alpha, so the state is transparent rather than opaque; culling is off
    // (the gizmo draws both faces of its plates) and depth writes stay on.
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

    // aNormal and aUv are unused but kept: the triangle draw shares one vertex buffer and one
    // vertex format with the rest of the immediate-mode renderer.
    layout(location=0) in vec4 aPositionSize;
    layout(location=1) in vec4 aColor;
    layout(location=2) in vec3 aNormal;
    layout(location=3) in vec2 aUv; 


    out VertexData vData;

    void main() 
    {
        vData.m_color = aColor.abgr; // swizzle to correct endianness
        vData.m_size = max(aPositionSize.w, kAntialiasing);
        gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);

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
    }
}
