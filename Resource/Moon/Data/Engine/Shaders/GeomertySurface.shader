// Topology shape faces: the shaded surface of a batch mesh, with every triangle's edges
// expanded into screen-space geometry so their antialiasing can be feathered per pixel.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md), with all three
// stages written by this file - the generated skeletons cannot host any of them. The vertex
// stage builds the world normal with the inverse-transpose of the model matrix and carries a
// fourth attribute (the domain id) the skeleton has no varying for; the geometry stage insets
// each triangle and emits its edge equations; the fragment stage shades with those equations
// rather than with the three varyings the skeleton would pass. MoonMatc puts the prelude, the
// variant #defines and the parameters in front of the blocks and generates nothing else.

material {
    name         : GeomertySurface,
    // Filament's standard model, generated rather than called by hand: MoonMatc splices the
    // surface stack in (Lighting/Filament/*) and this file only fills MaterialInputs. The
    // fragment stays a raw stage because of the geometry stage's edge equations, domain colour,
    // clip plane and depth peel - it calls evaluateSurface(), the varying-agnostic entry point.
    shadingModel : lit,
    blending     : opaque,        // the face material itself is opaque; the same shader also
                                  // backs a SetTransparent(true) instance (the Transparents pass)
    culling      : none,          // SetBackfaceCulling(false) at the TopoActor draw site
    depthWrite   : true,          // both face materials keep depth writing on

    // Filament's per-material specularAntiAliasing (GEOMETRIC_SPECULAR_AA): compile-time, like
    // every other material property in filament. Off, matching the Qt-Filament reference.
    specularAntiAliasing : false,
    // Filament's clearCoatIorChange, on there too.
    clearCoatIorChange : true,

    vertexAttributes : [POSITION, UV0, NORMAL, DOMAIN_ID],

    features : [ WITH_EDGE, DISABLE_DOMAIN_COLOR ],
    passes   : [ Transparents ],

    // Filament's per-material variant filter: the renderer adds these bits to the variant key for
    // every draw, exactly as it does for Standard. The fragment used to declare
    // VARIANT_HAS_DIRECTIONAL_LIGHTING / DYNAMIC_LIGHTING by hand because it had no filter and the
    // keyed path was never taken for it; that pinned both light paths into every variant and could
    // never produce a shadowed one. SHADOWING stays out of the list until the shadow pass is wired
    // into the CAD draw (the filter only helps if there is a valid shadow map to sample).
    variantFilter : [ DIRECTIONAL_LIGHTING, DYNAMIC_LIGHTING ],

    parameters : [
        // label / group / min / max / step / color are the designer schema, read by the
        // material designer and the actor inspector instead of guessing from the name.
        // The edge expansion reads the width in the geometry stage and feathers with it in
        // the fragment stage, so it is one parameter for both.
        { type : float,     name : lineWidth, default : 1.0, label : "Line Width", group : "Edge", min : 0.0, max : 10.0, step : 0.1 },
        { type : float3,    name : edgeColor, label : "Edge Color", group : "Edge", color : true },
        { type : float,     name : edgeOpacity, default : 1.0, label : "Edge Opacity", group : "Edge", min : 0.0, max : 1.0, step : 0.005 },

        { type : sampler2D, name : u_AlbedoMap, label : "Albedo Map", group : "Textures" },
        { type : float4,    name : u_Albedo, default : vec4(0.7215686, 0.7215686, 0.7215686, 1.0), label : "Base Color", group : "Base", color : true },
        { type : sampler2D, name : u_AmbientOcclusionMap, label : "Ambient Occlusion Map", group : "Textures" },

        { type : sampler2D, name : u_EmissiveMap, label : "Emissive Map", group : "Textures" },
        { type : float3,    name : u_EmissiveColor, default : vec3(0.0, 0.0, 0.0), label : "Emissive Color", group : "Base", color : true },
        { type : float,     name : u_EmissiveIntensity, default : 0.0, label : "Emissive Intensity", group : "Base", min : 0.0, max : 10.0, step : 0.01 },

        { type : sampler2D, name : u_MetallicMap, label : "Metallic Map", group : "Textures" },
        { type : float,     name : u_Metallic, default : 0.0, label : "Metallic", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : sampler2D, name : u_RoughnessMap, label : "Roughness Map", group : "Textures" },
        { type : float,     name : u_Roughness, default : 0.4, label : "Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },

        // Filament standard model inputs, the same set and the same defaults as FilamentPBR.mat
        // (which in turn match Qt-Filament's MaterialPreview()): base #b8b8b8, metallic 0,
        // roughness 0.40, reflectance 0.5, no clear coat.
        { type : float,     name : u_Reflectance, default : 0.5, label : "Reflectance", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_ClearCoat, default : 0.0, label : "Clear Coat", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_ClearCoatRoughness, default : 0.0, label : "Clear Coat Roughness", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float,     name : u_Thickness, default : 1.0, label : "Thickness", group : "Parameters", min : 0.0, max : 10.0, step : 0.01 },
        { type : float,     name : u_SpecularFactor, default : 1.0, label : "Specular Factor", group : "Base", min : 0.0, max : 1.0, step : 0.005 },
        { type : float3,    name : u_SpecularColorFactor, default : vec3(1.0), label : "Specular Color Factor", group : "Base", color : true },
        { type : float3,    name : u_Absorption, default : vec3(0.0), label : "Absorption", group : "Parameters", color : true },

        { type : float,     name : u_RefractionIndex, default : 1.5, label : "Refraction Index", group : "Parameters", min : 1.0, max : 3.0, step : 0.01 },       // 1.0 = air, 1.33 = water, 1.5 = glass, etc.
        { type : float,     name : u_Transmission, default : 0.0, label : "Transmission", group : "Parameters", min : 0.0, max : 1.0, step : 0.005 },          // 0.0 = opaque, 1.0 = fully transmissive

        // The Transparents pass: the depth peel this material is drawn with when it is the
        // transparent instance of the face material. Engine-owned, not a material parameter.
        { type : sampler2D, name : DepthPeelTex, ui : false },
        { type : sampler2D, name : OpaqueDepthTex, ui : false },

        // The IBL cubes, the DFG LUT, the shadow map and its matrix are NOT declared here any
        // more: for `shadingModel : lit` MoonMatc injects them (InjectEngineGlobals), the way
        // filament keeps them in the frame/per-view blocks instead of in the material.

        // One colour per B-rep domain, indexed by the domain id attribute.
        { type : samplerBuffer, name : domainColorTex, ui : false }
    ]
}

vertex {

out VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 Normal;
    vec2 DomainId;
} vs_out;

void main()
{
    vec4 worldPos = ubo_Model * vec4(geo_Pos, 1.0);
    mat3 normalMatrix = mat3(transpose(inverse(ubo_Model)));
    vec3 worldNormal = normalize(normalMatrix * geo_Normal);
    vs_out.FragPos = worldPos.xyz;
    vs_out.TexCoords = geo_TexCoords;
    vs_out.Normal = worldNormal;
    vs_out.DomainId=geo_DomainId;    
    gl_Position = ubo_Projection * ubo_View * worldPos;
}
}

geometry {
in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 Normal;
    vec2 DomainId;

}gs_in[];
out GS_OUT{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 Normal;
    vec2 DomainId;
#if defined(WITH_EDGE)
    noperspective vec4 edgeEqn[3];
    float screenLengthOutput;
#endif
} gs_out;

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

// `lineWidth` is a material parameter now: it is read here to build the offsets and in the
// fragment stage to feather the edge.
//uniform samplerBuffer edgeTexture;
// surface with edges

void main() 
{

#if defined(WITH_EDGE)
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
        gs_out.edgeEqn[i] = vec4(tmp.x, tmp.y, 0.0, -d);
    }
    vec2 offsets[3];

    offsets[0] = gs_out.edgeEqn[2].xy + gs_out.edgeEqn[0].xy;
    offsets[0] = -0.5*normalize(offsets[0])*lineWidth;
    offsets[0] /= vec2(ubo_ScreenWidth,ubo_ScreenHeight);

    offsets[1] = gs_out.edgeEqn[0].xy + gs_out.edgeEqn[1].xy;
    offsets[1] = -0.5*normalize(offsets[1])*lineWidth;
    offsets[1] /= vec2(ubo_ScreenWidth,ubo_ScreenHeight);

    offsets[2] = gs_out.edgeEqn[1].xy + gs_out.edgeEqn[2].xy;
    offsets[2] = -0.5*normalize(offsets[2])*lineWidth;
    offsets[2] /= vec2(ubo_ScreenWidth,ubo_ScreenHeight);

    
    //float edgeValues = 255.0*texelFetch(edgeTexture, gl_PrimitiveIDIn + PrimitiveIDOffset).r;
    //if (edgeValues < 4.0) edgeEqn[2].z = lineWidth;
    //if (mod(edgeValues, 4.0) < 2.0) edgeEqn[1].z = lineWidth;
    //if (mod(edgeValues, 2.0) < 1.0) edgeEqn[0].z = lineWidth;


#endif
 

    vec3 nor=normalize(cross(gs_in[1].FragPos-gs_in[0].FragPos,gs_in[2].FragPos-gs_in[0].FragPos));      

    for (int i = 0; i < 3; i++)
    {
        gl_PrimitiveID = gl_PrimitiveIDIn;
#if defined(WITH_EDGE)
        gs_out.screenLengthOutput = length(pos[i + 1] - pos[i]);
#endif 
        gs_out.FragPos=gs_in[i].FragPos;
        gs_out.TexCoords=gs_in[i].TexCoords;
        gs_out.DomainId=gs_in[i].DomainId;
        gs_out.Normal=gs_in[i].Normal;
       
        gl_Position = gl_in[i].gl_Position;
#if defined(WITH_EDGE)
        gl_Position.xy = gl_Position.xy + offsets[i]*gl_Position.w;
#else
        gl_Position.xy = gl_Position.xy ;
#endif 

        EmitVertex();
    }
    EndPrimitive();
}
}

fragment {
#include ":Shaders/Lighting/Filament/surface_lighting.glsl"

in GS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 Normal;
    vec2 DomainId;
#if defined(WITH_EDGE)
    noperspective vec4 edgeEqn[3];
    float screenLengthOutput;
#endif
} fs_in;

// surface with edges
// `lineWidth`, `edgeColor` and `edgeOpacity` are material parameters now, so they are declared
// by MoonMatc in front of this block (the same holds for every other uniform that used to be
// declared here - see the parameters list above).

// The shared Filament prelude (surface_common.glsl) provides saturate/pow5 and the standard
// model helpers. There is deliberately no normal mapping here: the CAD face mesh (VertexBVH:
// position, UV, normal, domain id) has no tangent frame, and building one from the screen-space
// derivatives of the UVs is not acceptable - it is undefined on a mesh without UVs (the STEP
// import writes UV = 0 for every vertex) and it is not what Filament does either. The
// interpolated vertex normal is what the shape actually has.

out vec4 FRAGMENT_COLOR;

void main()
{
#if defined(Transparents)
  // Window depth, straight from the depth buffer: no inverse projection, so the camera's
  // projection cannot distort the comparison. The perspective camera here has an infinite far
  // plane, and unprojecting its stored depth makes w approach zero - the reconstruction error
  // explodes, and the same surface passes the test on some pixels and fails on others. That is
  // the flicker the transparent objects showed while the camera orbited, and never in ortho.
  // Reversed-Z: a larger window depth is closer.
  ivec2 coords = ivec2(gl_FragCoord.xy);
  const float prevRaw = texelFetch(DepthPeelTex, coords, 0).r;
  const float opaqueRaw = texelFetch(OpaqueDepthTex, coords, 0).r;
  const float rawEps = 4.0 / 16777216.0;   // a few quanta of a 24-bit depth buffer
  if (gl_FragCoord.z >= prevRaw - rawEps || gl_FragCoord.z <= opaqueRaw + rawEps) discard;
#endif

    if(ubo_enableClip==1){
        float offsetw=dot(ubo_plane.xyz,fs_in.FragPos)+ubo_plane.w;
        if(offsetw>0){
            discard;
        }
    }


    const vec2 texCoords = fs_in.TexCoords;
    vec4 albedoTexel = texture(u_AlbedoMap, texCoords);
    vec4 albedo = albedoTexel * u_Albedo;

    // The albedo map, the picker tint and the domain colour are all sRGB: decode each one and
    // combine in linear. Decoding the product (the old path) darkens any tint that is not white.
    vec3 linearAlbedo = pow(albedoTexel.rgb, vec3(2.2)) * pow(u_Albedo.rgb, vec3(2.2));
#if !defined(DISABLE_DOMAIN_COLOR) 
    int domainIndex=int(round(fs_in.DomainId.x));
    linearAlbedo *= pow(texelFetch(domainColorTex,domainIndex).rgb, vec3(2.2));
#endif

    vec3 normal = normalize(fs_in.Normal);
  
    vec3 VD=normalize(ubo_ViewPos-fs_in.FragPos);
    if(ubo_CameraType==0){
       VD=vec3(ubo_View[0][2],ubo_View[1][2],ubo_View[2][2]);
    }
    const bool backFace = dot(VD, normal) < 0.0f;
    if (backFace) {
        normal = -normal;
    }
    vec3 gnormal = normal;

    const float metallic = texture(u_MetallicMap, texCoords).r * u_Metallic;
    const float roughness = texture(u_RoughnessMap, texCoords).r * u_Roughness;

    const float ao = texture(u_AmbientOcclusionMap, texCoords).r;


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
    float emix0 = clamp(0.5 + 0.5 * lineWidth - edist[0], 0.0, 1.0);
    float emix1 = clamp(0.5 + 0.5 * lineWidth - edist[1], 0.0, 1.0);
    float emix2 = clamp(0.5 + 0.5 * lineWidth - edist[2], 0.0, 1.0);
    float emix = emix1;
    emix = mix(emix, max(emix, emix0), step(fs_in.edgeEqn[0].z, 0.01));
    emix = mix(emix, max(emix, emix2), step(fs_in.edgeEqn[2].z, 0.01));
    float opacityFactor = clamp(fs_in.screenLengthOutput / 50., 0.15, 1.0);
    linearAlbedo = mix(linearAlbedo, pow(edgeColor, vec3(2.2)), emix*opacityFactor*edgeOpacity);
#endif
    // Filament standard model, generated by MoonMatc: this file fills MaterialInputs and the
    // `lit` surface prelude (Lighting/Filament/*) turns them into a colour. Everything the CAD
    // surface adds on top - domain colour, the edge feather, clip plane, depth peel, the ortho
    // view vector, the derivative tangent frame - is already folded into the inputs below.
    //
    // NOTE: unlike the old Lighting/IBL.glsl path this one does not apply ParallaxCorrect or
    // the local reflection-probe OBB mask yet; that is a known difference (see
    // docs/FilamentAlignmentTODO.md, "GeomertySurface 迁移").
    MaterialInputs m;
    initMaterial(m);
    m.baseColor = vec4(linearAlbedo, albedo.a);
    m.metallic = metallic;
    m.roughness = roughness;
    m.reflectance = u_Reflectance;
    m.specularFactor = u_SpecularFactor;
    m.specularColorFactor = u_SpecularColorFactor;
    m.ior = u_RefractionIndex;
    m.thickness = u_Thickness;
    m.clearCoat = u_ClearCoat;
    m.clearCoatRoughness = u_ClearCoatRoughness;
    m.ambientOcclusion = ao;
    m.transmission = u_Transmission;
    m.absorption = u_Absorption;

    // The emissive map is sRGB and the colour comes from the panel's colour picker, which is
    // sRGB too: decode each separately, then scale by the (linear) intensity. Decoding the
    // product instead would darken any tint that is not white or black.
    m.emissive = vec4(
        pow(texture(u_EmissiveMap, texCoords).rgb, vec3(2.2))
        * pow(u_EmissiveColor, vec3(2.2))
        * u_EmissiveIntensity, 1.0);

    // `normal` is the world shading normal (already through the derivative tangent frame when
    // the designer assigned a normal map) and `gnormal` the geometric one, so they are handed
    // over directly instead of writing a tangent-space m.normal.
    const vec4 shaded = evaluateSurface(m, fs_in.FragPos, gnormal, normal, gnormal, VD);
    FRAGMENT_COLOR = vec4(shaded.rgb, albedo.a);

}
}
