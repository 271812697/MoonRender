// Section cap: fills the hole the clip plane cuts into a model with a hatched, IBL-shaded cap,
// reconstructing the plane hit from the view ray and stamping its depth for occlusion.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). A full-screen pass (domain :
// postProcess) - the draw site hands it the unit quad and the vertex stage is exactly the
// skeleton MoonMatc generates (clip-space position, texture coordinates), so this file has no
// vertex block at all. The algorithm is the fragment stage's entry point: it only has to fill
// in MaterialInputs (plus gl_FragDepth, which a fragment stage may write from anywhere).
//
// State from SectionCapRenderPass::Draw: backface and frontface culling both off -> none,
// SetDepthWriting(true) -> depthWrite true, no SetBlendable -> opaque.

material {
    name         : SectionCap,
    shadingModel : unlit,
    domain       : postProcess,
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    parameters : [
        // The cap colour and the IBL environment. None of these is set by the draw site any
        // more, so the old declaration-time defaults are carried over: without them the cap
        // would be black and unlit instead of the light grey the section view shows.
        { type : float4,      name : u_Albedo,               default : vec4(1.0) },
        { type : samplerCube, name : _IrradianceCube },
        { type : samplerCube, name : _PrefilterCube },
        { type : sampler2D,   name : _BRDFLut },
        { type : float,       name : u_environmentIntensity, default : 0.5 },
        // Hatching spacing in world units. The pattern is anchored to the section plane, so
        // moving the camera changes only its projection, never its position on the cut.
        { type : float,       name : u_HatchPeriod,  default : 2.0 },
        { type : float,       name : u_HatchBand,    default : 0.5 },
        // 0 = light lines on the fill, 1 = black lines.
        { type : float,       name : u_HatchContrast,  default : 0.80 },
        // Stripe angle around the section normal, in degrees.
        { type : float,       name : u_HatchAngleDeg,  default : 45.0 }
    ]
}

fragment {
    void material(inout MaterialInputs m) {

        // Reconstruct the view ray. Perspective rays start at the camera, while
        // orthographic rays are parallel to the view direction.
        vec2 ndc = TexCoords * 2.0 - 1.0;
        mat4 invViewProj = inverse(ubo_Projection * ubo_View);
        vec3 rayOrigin = ubo_ViewPos;
        vec3 rayDir;
        if (ubo_CameraType == 1)
        {
            // The direction comes from a point on the *near* plane, not the far one: a
            // perspective camera has no far plane any more (its projection takes far -> inf,
            // so the far plane degenerates to (0,0,0,1) and unprojecting z_ndc = 1 divides by
            // w = 0). With the near plane the ray is well defined for either far plane.
            vec4 nearWorld = invViewProj * vec4(ndc, -1.0, 1.0);
            nearWorld /= nearWorld.w;
            rayDir = normalize(nearWorld.xyz - ubo_ViewPos);
        }
        else
        {
            vec4 nearWorld = invViewProj * vec4(ndc, -1.0, 1.0);
            nearWorld /= nearWorld.w;
            vec4 farWorld = invViewProj * vec4(ndc, 1.0, 1.0);
            farWorld /= farWorld.w;
            rayOrigin = nearWorld.xyz;
            rayDir = normalize(farWorld.xyz - nearWorld.xyz);
        }

        // Intersect the clip plane with the view ray.
        float denom = dot(ubo_plane.xyz, rayDir);
        if (abs(denom) < 1e-6) discard;
        float t = -(dot(ubo_plane.xyz, rayOrigin) + ubo_plane.w) / denom;
        if (t < 0.0) discard;
        vec3 hit = rayOrigin + rayDir * t;

        // Write the depth of the plane so the cap is occluded correctly by any
        // kept surface in front of it (the depth buffer itself is left untouched).
        vec4 clipPos = ubo_Projection * ubo_View * vec4(hit, 1.0);
        // Reversed-Z depth range: window depth is 1 at near and 0 at far.
        gl_FragDepth = 0.5 - (clipPos.z / clipPos.w) * 0.5;

        // Hatching stripes, derived from the clip plane alone and anchored in world space.
        vec3 n = normalize(ubo_plane.xyz);
        vec3 ref = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 lineDir = normalize(cross(n, ref));
        float hatchAngle = radians(u_HatchAngleDeg);
        vec3 stripeDir = normalize(cos(hatchAngle) * lineDir
            + sin(hatchAngle) * cross(n, lineDir));
        vec3 planeOrigin = -ubo_plane.w * n;

        // World-space coordinate across the stripes. No screen derivative normalisation: that
        // made the period camera-dependent, so the pattern slid over the section whenever the
        // view moved. fwidth() is only used for one-pixel anti-aliasing.
        float dist = dot(hit - planeOrigin, stripeDir);
        float phase = abs(fract(dist / u_HatchPeriod) - 0.5) * 2.0;   // 0 = line centre
        float halfW = (u_HatchBand * 0.5) / u_HatchPeriod;
        float aa = max(fwidth(dist) / u_HatchPeriod, 1e-6);
        float tStrip = smoothstep(halfW - aa, halfW + aa, phase);

        vec3 lineColor = u_Albedo.rgb * (1.0 - clamp(u_HatchContrast, 0.0, 1.0));
        vec3 albedo = mix(lineColor, u_Albedo.rgb, tStrip);

        // Simple IBL shading with the plane normal facing the camera.
        vec3 VD = normalize(rayOrigin - hit);
        if (dot(VD, n) < 0.0) n = -n;

        // Diffuse-dominant IBL (same as the old SectionFace material): low
        // metallic keeps the cap bright instead of being mostly specular.
        const float metallic = 0.0;
        const float roughness = 1.0;
        const vec3 linearAlbedo = pow(albedo, vec3(2.2));

        vec3 irradiance = texture(_IrradianceCube, n).rgb;
        vec3 diffuse = irradiance * linearAlbedo;
        float NoV = max(dot(n, VD), 0.0);
        vec3 F0 = vec3(0.04);
        vec3 F = F0 + (vec3(1.0) - F0) * pow(1.0 - NoV, 5.0);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        const float MAX_REFLECTION_LOD = float(textureQueryLevels(_PrefilterCube) - 1);
        vec3 R = reflect(-VD, n);
        const float lod = MAX_REFLECTION_LOD * roughness * (2.0 - roughness);
        vec3 prefilteredColor = textureLod(_PrefilterCube, R, lod).rgb;
        vec2 brdf = texture(_BRDFLut, vec2(NoV, roughness)).rg;
        vec3 spec = prefilteredColor * (F * brdf.x + brdf.y);

        vec3 color = (kD * diffuse + spec) * u_environmentIntensity + linearAlbedo * 0.05;
        m.baseColor = vec4(color, 1.0);
    }
}
