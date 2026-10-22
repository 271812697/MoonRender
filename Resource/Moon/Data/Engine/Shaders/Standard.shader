// Standard: the shaded surface material - PBR from Lighting/PBR.glsl, with the two metalness
// workflows, parallax and normal mapping, distance fade and the edge expansion on top.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md). All three stages
// own their main(): the vertex stage uses the attributes MoonMatc declares from
// vertexAttributes (POSITION, UV0, NORMAL, TANGENT, BITANGENT) and passes a TBN basis and
// the parallax pair of its
// own, the geometry stage expands each triangle so it carries the edge equations, and the
// fragment stage declares the matching input block (GS_OUT with the edges, VS_OUT without) and
// its own output. MoonMatc puts the prelude, the variant #defines and the parameters in front of
// them and generates nothing else for the three stages.

material {
    name         : Standard,
    // The shading below is the PBR header's: the runtime has no lit shading model yet.
    shadingModel : unlit,
    // What the scene pass draws the shaded bodies with, i.e. Rendering::Data::Material's
    // defaults: opaque, backface culling on, depth writing on.
    blending     : opaque,
    culling      : back,
    depthWrite   : true,

    vertexAttributes : [POSITION, UV0, NORMAL, TANGENT, BITANGENT],

    features : [
        PARALLAX_MAPPING,
        NORMAL_MAPPING,
        DISTANCE_FADE,
        SPECULAR_WORKFLOW,
        WITH_EDGE
    ],
    // The archive compiles no optional feature (same as an .ovmat with an empty feature set);
    // the designer recompiles with whatever set the user enables.
    defaultFeatures : [],

    // Engine state variants; the renderer selects one per draw (scene lights / shadows).
    variantFilter : [DIRECTIONAL_LIGHTING, DYNAMIC_LIGHTING, SHADOWING],

    passes : [ SHADOW_PASS, REFLECTION_PASS ],

    // One entry per uniform the fragment stage used to declare by hand, same names (the C++
    // side calls SetProperty with them) and the initializer kept as `default`. A uniform the
    // old file guarded with `#if defined(FEATURE)` is declared for every variant: the one a
    // variant does not read is dropped by the driver.
    //
    // label / group / min / max / step / color / ui are the material designer's schema (see
    // docs/ShaderPipeline.md): the panel builds its form from them, so a designer never has to
    // guess a slider range or which vec4 is a colour. The engine-owned uniforms are marked
    // `ui : false` and do not show up there at all.
    parameters : [
        // --- base ---------------------------------------------------------------------------
        { type : float4, name : u_Albedo, default : vec4(1.0),
          label : "Base Color", group : "Base", color : true },
        { type : float,  name : u_Roughness, default : 1.0,
          label : "Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,  name : u_Metallic, default : 1.0,
          label : "Metallic", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,  name : u_Specular, default : 1.0,
          label : "Specular", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,  name : u_Glossiness, default : 1.0,
          label : "Glossiness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,  name : u_RefractionIndex, default : 1.5,
          label : "Refraction Index", group : "Base", min : 1.0, max : 3.0, step : 0.01 },
        { type : float,  name : u_Transmission, default : 0.0,
          label : "Transmission", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,  name : u_AlphaClippingThreshold, default : 0.0,
          label : "Alpha Clipping", group : "Base", min : 0.0, max : 1.0, step : 0.001 },
        { type : bool,   name : u_AlphaDithering, default : false,
          label : "Alpha Dithering", group : "Base" },

        // --- emission -----------------------------------------------------------------------
        { type : float3, name : u_EmissiveColor, default : vec3(1.0, 1.0, 1.0),
          label : "Emissive Color", group : "Emission" },
        { type : float,  name : u_EmissiveIntensity, default : 0.0,
          label : "Emissive Intensity", group : "Emission", min : 0.0, max : 10.0, step : 0.01 },

        // --- parallax -----------------------------------------------------------------------
        { type : float, name : u_HeightScale, default : 0.05,
          label : "Height Scale", group : "Parallax", min : 0.0, max : 0.2, step : 0.001 },
        { type : bool,  name : u_ParallaxClipEdges, default : false,
          label : "Clip Edges", group : "Parallax" },
        { type : int,   name : u_MinLayers, default : 8,
          label : "Min Layers", group : "Parallax", min : 1, max : 64, step : 1 },
        { type : int,   name : u_MaxLayers, default : 64,
          label : "Max Layers", group : "Parallax", min : 1, max : 128, step : 1 },

        // --- distance fade ------------------------------------------------------------------
        { type : float, name : u_DistanceFadeStart, default : 100.0,
          label : "Fade Start", group : "Distance Fade", min : 0.0, max : 500.0, step : 0.5 },
        { type : float, name : u_DistanceFadeLength, default : 10.0,
          label : "Fade Length", group : "Distance Fade", min : 0.0, max : 200.0, step : 0.5 },

        // --- uv transform -------------------------------------------------------------------
        { type : float2, name : u_TextureTiling, default : vec2(1.0, 1.0),
          label : "Tiling", group : "UV" },
        { type : float2, name : u_TextureOffset, default : vec2(0.0, 0.0),
          label : "Offset", group : "UV" },

        // --- post ---------------------------------------------------------------------------
        { type : bool, name : u_BuiltInToneMapping, default : false,
          label : "Tone Mapping", group : "Built-in" },
        { type : bool, name : u_BuiltInGammaCorrection, default : false,
          label : "Gamma Correction", group : "Built-in" },

        // --- maps ---------------------------------------------------------------------------
        { type : sampler2D, name : u_AlbedoMap, label : "Albedo Map", group : "Maps" },
        // The material's AO map; the fragment multiplies it with the screen-space occlusionTex.
        { type : sampler2D, name : u_AmbientOcclusionMap, label : "AO Map", group : "Maps" },
        { type : sampler2D, name : u_MaskMap, label : "Mask Map", group : "Maps" },
        { type : sampler2D, name : u_EmissiveMap, label : "Emissive Map", group : "Maps" },
        // SPECULAR_WORKFLOW reads the specular/glossiness pair, every other variant the
        // metallic/roughness one. Both sets are here, the unused one is dropped.
        { type : sampler2D, name : u_SpecularMap, label : "Specular Map", group : "Maps" },
        { type : sampler2D, name : u_GlossinessMap, label : "Glossiness Map", group : "Maps" },
        { type : sampler2D, name : u_MetallicMap, label : "Metallic Map", group : "Maps" },
        { type : sampler2D, name : u_RoughnessMap, label : "Roughness Map", group : "Maps" },
        { type : sampler2D, name : u_RoughnessMetallicMap, label : "Roughness/Metallic Map", group : "Maps" },
        { type : sampler2D, name : u_NormalMap, label : "Normal Map", group : "Maps" },
        { type : sampler2D, name : u_HeightMap, label : "Height Map", group : "Maps" },

        // --- advanced -----------------------------------------------------------------------
        // SHADOW_PASS only: that variant drops the parallax path (see the #undef below).
        { type : float, name : u_ShadowClippingThreshold, default : 0.5,
          label : "Shadow Clipping", group : "Advanced", min : 0.0, max : 1.0, step : 0.001 },

        // The renderer owns these: it sets them per frame and they are not material design.
        // The edge expansion reads it in the geometry stage: the width, in pixels, of the
        // dark rim it draws. It used to be a commented-out uniform, so the rim was zero wide.
        { type : float, name : lineWidth, default : 1.0,
          label : "Edge Width", group : "Edges", min : 0.0, max : 4.0, step : 0.1 },

        { type : sampler2D,   name : _ShadowMap, ui : false },
        { type : mat4,        name : _LightSpaceMatrix, ui : false },
        { type : samplerCube, name : _IrradianceCube, ui : false },
        { type : samplerCube, name : _PrefilterCube, ui : false },
        { type : sampler2D,   name : _BRDFLut, ui : false },
        { type : sampler2D,   name : occlusionTex, ui : false }
    ]
}

vertex {


    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
    } vs_out;

    void main()
    {
        vs_out.FragPos = vec3(ubo_Model * vec4(geo_Pos, 1.0));
        vs_out.TexCoords = geo_TexCoords;
        vs_out.Normal = normalize(mat3(transpose(inverse(ubo_Model))) * geo_Normal);
        //vs_out.TBN = ConstructTBN(ubo_Model, geo_Normal, geo_Tangent, geo_Bitangent);
        vs_out.TBN = ConstructTBN(ubo_Model, geo_Normal, geo_Tangent, cross(geo_Normal, geo_Tangent));

    #if defined(PARALLAX_MAPPING)
        const mat3 TBNi = transpose(vs_out.TBN);
        vs_out.TangentViewPos = TBNi * ubo_ViewPos;
        vs_out.TangentFragPos = TBNi * vs_out.FragPos;
    #endif

        gl_Position = ubo_Projection * ubo_View * vec4(vs_out.FragPos, 1.0);
    }
}

geometry {
#if defined(WITH_EDGE)
    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
    }gs_in[];
    out GS_OUT{
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
        noperspective vec4 edgeEqn[3];
    } gs_out;
    //in VS_OUT gs_in [];
    //out GS_OUT gs_out;

    layout(triangles) in;
    layout(triangle_strip, max_vertices = 3) out;

    //uniform samplerBuffer edgeTexture;

    void main()
    {
        vec2 pos[4];

        for (int i = 0; i < 3; i++)
        {
            pos[i] = gl_in[i].gl_Position.xy/gl_in[i].gl_Position.w;
            pos[i] = pos[i]*vec2(0.5) + vec2(0.5);
            pos[i] = pos[i]*vec2(ubo_ScreenWidth,ubo_ScreenHeight);
        }
        pos[3] = pos[0];

        float ccw = sign(cross(vec3(pos[1] - pos[0], 0.0), vec3(pos[2] - pos[0], 0.0)).z);

        for (int i = 0; i < 3; i++)
        {
            vec2 tmp = normalize(pos[i+1] - pos[i]);
            tmp = ccw*vec2(-tmp.y, tmp.x);
            float d = dot(pos[i], tmp);
            gs_out.edgeEqn[i] = vec4(tmp.x, tmp.y, lineWidth, -d);
        }

        //float edgeValues = 255.0*texelFetch(edgeTexture, gl_PrimitiveIDIn + PrimitiveIDOffset).r;
        //if (edgeValues < 4.0) edgeEqn[2].z = lineWidth;
        //if (mod(edgeValues, 4.0) < 2.0) edgeEqn[1].z = lineWidth;
        //if (mod(edgeValues, 2.0) < 1.0) edgeEqn[0].z = lineWidth;


        for (int i = 0; i < 3; i++)
        {
            gl_PrimitiveID = gl_PrimitiveIDIn;

            gs_out.FragPos=gs_in[i].FragPos;
            gs_out.TexCoords=gs_in[i].TexCoords;
            gs_out.Normal=gs_in[i].Normal;
            gs_out.TBN=gs_in[i].TBN;
            #if defined(PARALLAX_MAPPING)
              gs_out.TangentViewPos=gs_in[i].TangentViewPos;
              gs_out.TangentFragPos=gs_in[i].TangentFragPos;
            #endif

            gl_Position = gl_in[i].gl_Position;

            EmitVertex();
        }
        EndPrimitive();
    }
#else
    // MoonMatc emits a geometry stage for every variant of a material that has a geometry
    // block, but the legacy build only ever compiled this one for WITH_EDGE (the loader
    // skipped a stage whose body was wrapped in an `#if defined(...)` the feature set did not
    // contain). Without WITH_EDGE the fragment stage reads VS_OUT, so what is left for the
    // other variants is a pass-through: the same varying block in and out, the vertices
    // emitted untouched. It changes nothing about the picture, and it keeps the variants
    // without the edge expansion compiling and linking.
    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
    } gs_in[];

    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
    } gs_out;

    layout(triangles) in;
    layout(triangle_strip, max_vertices = 3) out;

    void main()
    {
        for (int i = 0; i < 3; i++)
        {
            gs_out.FragPos = gs_in[i].FragPos;
            gs_out.TexCoords = gs_in[i].TexCoords;
            gs_out.Normal = gs_in[i].Normal;
            gs_out.TBN = gs_in[i].TBN;
        #if defined(PARALLAX_MAPPING)
            gs_out.TangentViewPos = gs_in[i].TangentViewPos;
            gs_out.TangentFragPos = gs_in[i].TangentFragPos;
        #endif

            gl_Position = gl_in[i].gl_Position;

            EmitVertex();
        }
        EndPrimitive();
    }
#endif
}

fragment {
#if defined(REFLECTION_PASS)
    // Disable IBL for the reflection capture pass, otherwise reflective objects will
    // bounce infinitely, causing some artifacts. This define must be set before the PBR
    // shader is included, so it must be defined here.
    #define DISABLE_IMAGE_BASED_LIGHTING
#endif

    #include ":Shaders/Lighting/PBR.glsl"

    // The geometry stage hands the edge equations on with the surface varyings; without the
    // edge expansion the vertex stage writes the plain VS_OUT block.
#if defined(WITH_EDGE)
    in GS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
        noperspective vec4 edgeEqn[3];
    } fs_in;

    // surface with edges

    //uniform float lineWidth;
    //uniform vec3 edgeColor;
    //uniform float edgeOpacity;
#else
    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;
        mat3 TBN;
    #if defined(PARALLAX_MAPPING)
        vec3 TangentViewPos;
        vec3 TangentFragPos;
    #endif
    } fs_in;
#endif

    out vec4 FRAGMENT_COLOR;

#if defined(SHADOW_PASS)
    #undef PARALLAX_MAPPING // Disable parallax mapping in shadow pass
#endif

    void main()
    {


        if(ubo_enableClip==1){
            float offsetw=dot(ubo_plane.xyz,fs_in.FragPos)+ubo_plane.w;
            if(offsetw>0){
                discard;
            }
        }

    #if defined(DISTANCE_FADE)
        const float distanceAlpha = DistanceFadeAlpha(fs_in.FragPos, ubo_ViewPos, u_DistanceFadeStart, u_DistanceFadeLength);
        const float ditheredDistanceAlpha = Dithering(distanceAlpha, gl_FragCoord.xy);
        if (ditheredDistanceAlpha < 0)
        {
            discard;
        }
    #endif

        vec2 texCoords = TileAndOffsetTexCoords(fs_in.TexCoords, u_TextureTiling, u_TextureOffset);

    #if defined(PARALLAX_MAPPING)
        texCoords = ApplyParallaxOcclusionMapping(texCoords, u_HeightMap, fs_in.TangentViewPos, fs_in.TangentFragPos, u_HeightScale, u_MinLayers, u_MaxLayers);
        if (u_ParallaxClipEdges && IsParallaxOutOfBounds(texCoords, u_TextureTiling, u_TextureOffset, ubo_Projection))
        {
            discard;
        }
    #endif

        vec4 albedoTexel = texture(u_AlbedoMap, texCoords);
        vec4 albedo = albedoTexel * u_Albedo;
        albedo.a *= texture(u_MaskMap, texCoords).r;

    #if defined(SHADOW_PASS)
        if (albedo.a < u_ShadowClippingThreshold)
        {
            discard;
        }
    #else

        if (albedo.a < u_AlphaClippingThreshold)
        {
            discard;
        }

        if (u_AlphaDithering && Dithering(albedo.a, gl_FragCoord.xy) < 0)
        {
            discard;
        }

        // The albedo map and the picker tint are both sRGB: decode them separately and combine
        // in linear. Decoding the product (the old path) darkens any tint that is not white.
        vec3 linearAlbedo = pow(albedoTexel.rgb, vec3(2.2)) * pow(u_Albedo.rgb, vec3(2.2));

    #if defined(NORMAL_MAPPING)
        //mat3 tbn=ComputeTBN(fs_in.FragPos,fs_in.Normal,texCoords);
        //const vec3 normal = ComputeNormal(texCoords, fs_in.Normal, u_NormalMap, tbn);
        vec3 normal = ComputeNormal(texCoords, fs_in.Normal, u_NormalMap, fs_in.TBN);
    #else
        vec3 normal = normalize(fs_in.Normal);
    #endif
        vec3 VD=normalize(ubo_ViewPos-fs_in.FragPos);
        if(ubo_CameraType==0){
           VD=vec3(ubo_View[0][2],ubo_View[1][2],ubo_View[2][2]);
        }
        if(dot(VD,normal)<0.0f){
            normal=-normal;
        }

    #if defined(SPECULAR_WORKFLOW)
        const float specular = texture(u_SpecularMap, texCoords).r * u_Specular;
        const float glossiness = texture(u_GlossinessMap, texCoords).r * u_Glossiness;
        const float metallic = 1.0 - specular;
        const float roughness = 1.0 - glossiness;
    #else
        const float metallic = texture(u_MetallicMap, texCoords).b* u_Metallic;
        const float roughness = texture(u_RoughnessMap, texCoords).g * u_Roughness;
    #endif
        vec4 ndcpos= ubo_Projection * ubo_View * vec4(fs_in.FragPos, 1.0);
        ndcpos.xyz=ndcpos.xyz/ndcpos.w;
        ndcpos.xy=(ndcpos.xy+1.0)/2.0;
        // The material's AO map and the engine's screen-space AO both occlude: the map
        // carries the baked detail, the screen-space term the larger crevices. The shader
        // used to read only occlusionTex, so assigning an AO Map in the designer did nothing.
        const float materialAO = texture(u_AmbientOcclusionMap, texCoords).r;
        const float screenAO = texture(occlusionTex, ndcpos.xy).r;
        const float ao = materialAO * screenAO;

    #if defined(WITH_EDGE)
        float edist[3];
        edist[0] = dot(fs_in.edgeEqn[0].xy, gl_FragCoord.xy) + fs_in.edgeEqn[0].w;
        edist[1] = dot(fs_in.edgeEqn[1].xy, gl_FragCoord.xy) + fs_in.edgeEqn[1].w;
        edist[2] = dot(fs_in.edgeEqn[2].xy, gl_FragCoord.xy) + fs_in.edgeEqn[2].w;
        if (edist[0] < -0.5 && fs_in.edgeEqn[0].z > 0.0) discard;
        if (edist[1] < -0.5 && fs_in.edgeEqn[1].z > 0.0) discard;
        if (edist[2] < -0.5 && fs_in.edgeEqn[2].z > 0.0) discard;
        // The width is the band, not an offset added to the distance: adding it pushed
        // every interior pixel's distance above 1, so any width above zero removed the
        // edges instead of widening them (width 0 was the only visible setting).
        const float edgeWidth = fs_in.edgeEqn[0].z;
        float emix = clamp(0.5 + 0.5 * edgeWidth - min(min(edist[0], edist[1]), edist[2]), 0.0, 1.0);
        linearAlbedo = mix(linearAlbedo, vec3(0.0), emix);
    #endif
       vec3 pbr = PBRLighting(
            linearAlbedo,
            metallic,
            roughness,
            ao,
            normal,
            VD,
            fs_in.FragPos,
            _ShadowMap,
            _LightSpaceMatrix,
            _IrradianceCube,
            _PrefilterCube,
            _BRDFLut,
            u_Transmission,
            u_RefractionIndex
        );

        // Simple built-in tonemapping (Reinhard) and gamma correction for elements
        // not affected by post-processing (e.g. debug, UI, etc.), but still aiming
        // to approximate the final PBR look.
        // The selector goes in as a bvec3: `mix(genType, genType, genBType)`, i.e. the same
        // per-component select of the two colours, which is what the scalar-bool overload
        // means. The scalar one is spelled out in the GLSL 4.5 spec but is not implemented by
        // glslangValidator (14.3.0: "no matching overloaded function found"), so the scalar
        // form would not validate; nothing about the values changes.
        pbr = mix(pbr, pbr / (pbr + vec3(1.0)), bvec3(u_BuiltInToneMapping));
        pbr = mix(pbr, pow(pbr, vec3(1.0 / 2.2)), bvec3(u_BuiltInGammaCorrection));

        // The emissive map is sRGB and the colour comes from the panel's colour picker, which is
    // sRGB too: decode each separately, then scale by the (linear) intensity. Decoding the
    // product instead would darken any tint that is not white or black.
    const vec3 emissive =
        pow(texture(u_EmissiveMap, texCoords).rgb, vec3(2.2))
        * pow(u_EmissiveColor, vec3(2.2))
        * u_EmissiveIntensity;
        pbr += emissive;

        FRAGMENT_COLOR = vec4(pbr, albedo.a);
    #endif
    }
}
