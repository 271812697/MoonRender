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
        // Hatching spacing in *pixels* (see the stripe block below): 8 px apart with a 1 px
        // dark line, so the density and the contrast no longer change with the zoom.
        { type : float,       name : u_HatchPeriodPx,  default : 20.0 },
        { type : float,       name : u_HatchBandPx,    default : 5.0 },
        // 0 = light lines on the fill, 1 = black lines.
        { type : float,       name : u_HatchContrast,  default : 0.80 },
        // Stripe angle on screen, in degrees.
        { type : float,       name : u_HatchAngleDeg,  default : 45.0 },
        // 1 = the angle is fixed in screen space, 0 = the stripes follow the plane's projection.
        { type : float,       name : u_HatchScreenFixed, default : 1.0 }
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

        // Hatching stripes, derived from the clip plane alone.
        vec3 n = normalize(ubo_plane.xyz);
        vec3 ref = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 lineDir = normalize(cross(n, ref));
        vec3 planeOrigin = -ubo_plane.w * n;

        // Hatching with a *screen-space* period: the stripe coordinate is `dist / |gradient|`,
        // i.e. the world-space coordinate across the stripes counted in pixels, so dividing it
        // by u_HatchPeriodPx gives stripes exactly that many pixels apart at any zoom. World
        // stripes used to fall below the Nyquist limit when zoomed out and average into a flat
        // grey.
        //
        // The direction is the screen angle u_HatchAngleDeg by default (it reads as hatching
        // whatever the section's orientation is); u_HatchScreenFixed = 0 follows the plane's
        // own stripe direction instead, which keeps the pattern anchored to the model.
        float dist;
        float angle = radians(u_HatchAngleDeg);
        if (u_HatchScreenFixed > 0.5)
        {
            vec2 stripeNormal = vec2(-sin(angle), cos(angle));   // across the stripes
            dist = dot(gl_FragCoord.xy, stripeNormal);
        }
        else
        {
            dist = dot(hit - planeOrigin, lineDir) / max(length(vec2(dFdx(dot(hit - planeOrigin, lineDir)),
                dFdy(dot(hit - planeOrigin, lineDir)))), 1e-9);
        }
        // One unit of `dist` is one pixel, so the pattern below is purely in pixels and can no
        // longer alias when the camera pulls back.
        float phase = abs(fract(dist / u_HatchPeriodPx) - 0.5) * 2.0;   // 0 = line centre
        float halfW = (u_HatchBandPx * 0.5) / u_HatchPeriodPx;
        float aa = 1.0 / u_HatchPeriodPx;                               // one pixel of phase
        float tStrip = smoothstep(halfW - aa, halfW + aa, phase);

        // No level of detail: the spacing is 8 px and the line is 1 px at every zoom, so the
        // pattern can never become denser than its own period - there is nothing to fade.
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
