// The view-cube cell: one textured, edged cell of the view cube (and the outline of a topology
// polygon in the picking pass).
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), with all three stages written by
// this file. None of them fits a generated skeleton: the vertex stage keeps the legacy attribute
// set (position+size, colour, normal, uv) and picks its own model/view/projection pair, the
// geometry stage turns a triangle into the same triangle with the three screen-space edge
// equations attached, and the fragment stage shades the cell from the edge texture / block
// texture. So every stage is raw and keeps the legacy text: it declares its own attributes, its
// own interface blocks, its own output and its own main().
//
// One structural note on the geometry stage: the legacy pipeline only had a geometry stage when
// WITH_EDGE was defined, but a material that declares one gets it in every variant MoonMatc
// builds. The variant without WITH_EDGE therefore gets a pass-through that emits the triangle
// unchanged and forwards the same varyings the fragment stage's non-edge branch reads - the
// visible result is the legacy one (no edge feathering, no edge equations).

material {
    name         : GizmoCell,
    shadingModel : unlit,
    // ImRenderer::drawMesh(): blending on with SRC_ALPHA, culling = 0, depth writing on.
    // (drawMeshPick() has blending off, and a pass cannot carry its own state yet.)
    blending     : transparent,
    culling      : none,
    depthWrite   : true,

    passes   : [ PICKING_PASS ],
    features : [ WITH_EDGE, CUSTOM_PROJECT, CUSTOM_VIEWPORT, FIXED_SCALE, BLOCK_COLOR ],

    parameters : [
        // Declared for every variant: the ones a variant does not read are dropped by the
        // driver. u_ViewMatrix is used by the vertex stage under CUSTOM_PROJECT and by the
        // fragment stage unconditionally; the rest are guarded by a feature or the pass.
        { type : mat4,           name : uModelMatrix },
        { type : mat4,           name : uViewMatrix },
        { type : mat4,           name : uVProjMatrix },
        { type : float2,         name : uViewPortSize },
        { type : float2,         name : uViewPortStart },
        { type : usamplerBuffer, name : edgeTexture },
        { type : sampler2D,      name : u_AlbedoMap },
        { type : samplerBuffer,  name : blockTexture },
        { type : int,            name : polygonId }
    ]
}

vertex {
    layout(location=0) in vec4 aPositionSize;
    layout(location=1) in vec4 aColor;
    layout(location=2) in vec3 aNormal;
    layout(location=3) in vec2 aUv;

    out VSOutPut{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
    }vData;

    void main()
    {
        vData.color = aColor.rgba; // swizzle to correct endianness
        vData.pos= aPositionSize;
        //the follow doesn't use CUSTOM_PROJECT
        #if defined(FIXED_SCALE)
            //orth
            if(ubo_CameraType==0){
              float ratio=400.0/(ubo_Projection[1][1]*ubo_ScreenHeight);
              vData.pos.xyz=vData.pos.xyz*ratio;
            }
            else{
              vec4 viewPos= ubo_View * uModelMatrix*vec4(vData.pos.xyz,1.0f);
              viewPos.z/=viewPos.w;
              float h=viewPos.z*1/ubo_Projection[1][1];
              float ratio=400.0*abs(h)/ubo_ScreenHeight;
              vData.pos.xyz=vData.pos.xyz*ratio;
            }
        #endif
        vData.normal=normalize(mat3(uModelMatrix)*aNormal);

        vData.uv=aUv;
    #if defined(CUSTOM_PROJECT)
        gl_Position = uVProjMatrix*uViewMatrix * uModelMatrix*vec4(vData.pos.xyz, 1.0);
    #else
        gl_Position = ubo_Projection * ubo_View * uModelMatrix*vec4(vData.pos.xyz, 1.0);
    #endif
    }
}

geometry {
    in VSOutPut{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
    }gs_in[];

    layout(triangles) in;
    layout(triangle_strip, max_vertices = 3) out;

#if defined(WITH_EDGE)
    out GS_OUT{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
        noperspective vec4 edgeEqn[3];
    } gs_out;

    void main()
    {
        vec2 pos[4];

        for (int i = 0; i < 3; i++)
        {
            pos[i] = gl_in[i].gl_Position.xy/gl_in[i].gl_Position.w;
            pos[i] = pos[i]*vec2(0.5) + vec2(0.5);
            #if defined(CUSTOM_VIEWPORT)
                pos[i] = pos[i]*uViewPortSize+uViewPortStart;
            #else
                pos[i] = pos[i]*vec2(ubo_ScreenWidth,ubo_ScreenHeight);
            #endif
        }
        pos[3] = pos[0];

        float ccw = sign(cross(vec3(pos[1] - pos[0], 0.0), vec3(pos[2] - pos[0], 0.0)).z);

        for (int i = 0; i < 3; i++)
        {
            vec2 tmp = normalize(pos[i+1] - pos[i]);
            tmp = ccw*vec2(-tmp.y, tmp.x);
            float d = dot(pos[i], tmp);
            gs_out.edgeEqn[i] = vec4(tmp.x, tmp.y, 0.0, -d);
        }

        uint edgeValues = texelFetch(edgeTexture, gl_PrimitiveIDIn).r;
        if (float(edgeValues) < 4.0) gs_out.edgeEqn[0].z = 1.5;
        if (mod(float(edgeValues), 4.0) < 2.0) gs_out.edgeEqn[1].z = 1.5;
        if (mod(float(edgeValues), 2.0) < 1.0) gs_out.edgeEqn[2].z = 1.5;


        for (int i = 0; i < 3; i++)
        {
            gl_PrimitiveID = gl_PrimitiveIDIn;
            gs_out.pos=gs_in[i].pos;
            gs_out.normal=gs_in[i].normal;
            gs_out.color=gs_in[i].color;
            gs_out.uv=gs_in[i].uv;
            gl_Position = gl_in[i].gl_Position;
            EmitVertex();
        }
        EndPrimitive();
    }
#else
    // The variant that does not want edges: the triangle goes through untouched. The output
    // block cannot be called VSOutPut, that name is this stage's input block.
    out VSOutPutPass{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
    } gs_out;

    void main()
    {
        for (int i = 0; i < 3; i++)
        {
            gs_out.pos=gs_in[i].pos;
            gs_out.normal=gs_in[i].normal;
            gs_out.color=gs_in[i].color;
            gs_out.uv=gs_in[i].uv;
            gl_Position = gl_in[i].gl_Position;
            EmitVertex();
        }
        EndPrimitive();
    }
#endif
}

fragment {
    #include ":Shaders/Lighting/PBR.glsl"

#if defined(WITH_EDGE)
    in GS_OUT{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
        noperspective vec4 edgeEqn[3];
    } fs_in;
#else
    in VSOutPutPass{
        vec4 pos;
        vec3 normal;
        vec4 color;
        vec2 uv;
    }fs_in;
#endif

    layout(location=0) out vec4 fResult;

    void main()
    {
    #if defined(PICKING_PASS)
        fResult = vec4(0,int(round(fs_in.pos.w))/255.0,polygonId/255.0,254.0/255.0);
    #else
        vec4 albedo = texture(u_AlbedoMap, fs_in.uv)*fs_in.color;
    #if defined(BLOCK_COLOR)
        int domainIndex=int(round(fs_in.pos.w));
        vec4 domainColor=texelFetch(blockTexture,domainIndex).rgba;
        albedo.rgb*=domainColor.rgb;
        albedo.a=domainColor.a;
    #endif

    #if defined(WITH_EDGE)
        float edist[3];
        edist[0] = dot(fs_in.edgeEqn[0].xy, gl_FragCoord.xy) + fs_in.edgeEqn[0].w;
        edist[1] = dot(fs_in.edgeEqn[1].xy, gl_FragCoord.xy) + fs_in.edgeEqn[1].w;
        edist[2] = dot(fs_in.edgeEqn[2].xy, gl_FragCoord.xy) + fs_in.edgeEqn[2].w;
        if (edist[0] < -0.5 && fs_in.edgeEqn[0].z > 0.0) discard;
        if (edist[1] < -0.5 && fs_in.edgeEqn[1].z > 0.0) discard;
        if (edist[2] < -0.5 && fs_in.edgeEqn[2].z > 0.0) discard;
        edist[0] += fs_in.edgeEqn[0].z;
        edist[1] += fs_in.edgeEqn[1].z;
        edist[2] += fs_in.edgeEqn[2].z;
        float emix = clamp(0.5 + 0.5*1.5 - min( min( edist[0], edist[1]), edist[2]), 0.0, 1.0);
        albedo.rgb = mix(albedo.rgb, vec3(0.3,0.3,1.0), emix);
    #endif

        vec3 viewDir=vec3(uViewMatrix[0][2],uViewMatrix[1][2],uViewMatrix[2][2]);
        vec3 lightDir=viewDir;
        vec3 N=fs_in.normal;
        if(dot(N,viewDir)<0){
            N=-N;
        }

        float diff=max(dot(lightDir,N),0);
        vec3 diffuse=diff*albedo.rgb;
        vec3 specular=sign(diffuse)*pow(max(dot(N,lightDir),0),100)*0.03*albedo.rgb;
        vec3 col=diffuse+specular+vec3(0.05,0.05,0.05);
        //col=pow(col, vec3(2.2));
        #if defined(BLOCK_COLOR)
         fResult = vec4(col,albedo.a);
        #else
         fResult = vec4(col,1.0);
        #endif
    #endif
    }
}
