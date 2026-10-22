#include ":Shaders/Common/Buffers/LightsSSBO.glsl"
#include ":Shaders/Common/Constants.glsl"
#include ":Shaders/Common/Utils.glsl"
#include ":Shaders/Lighting/Light.glsl"
#include ":Shaders/Lighting/IBL.glsl"

// Whether this material takes part in shadowing. The engine only hands a shadow map
// to a material that receives shadows; one that never gets a map would otherwise read
// whatever texture unit 0 happens to hold and turn up with patches that have nothing
// to do with the geometry in front of it. Off unless the engine turns it on.
uniform bool u_ReceiveShadows = false;

/** A directional light that carries a shadow only casts it on a material the engine
 * handed a shadow map to; for any other material the lookup has nothing to read. */
DirectionalLight GateShadowOnReceiver(DirectionalLight light)
{
    light.hasShadow = light.hasShadow && u_ReceiveShadows;
    return light;
}

//------------------------------------------------------------------------------
// Filament's standard BRDF (surface_brdf.fs). `perceptualRoughness` is the material
// value; the microfacet terms take its square (alpha).
//------------------------------------------------------------------------------

float D_GGX(float NoH, float alpha)
{
    // Filament's form: a = NoH * alpha, k = alpha / (1 - NoH^2 + a^2), D = k^2 / PI.
    // It computes 1 - NoH^2 through the k expression, which keeps precision in the
    // highlight where the direct 1 - NoH^2 form cancels.
    const float a = NoH * alpha;
    const float k = alpha / (1.0 - NoH * NoH + a * a);
    return k * k * (1.0 / PI);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float alpha)
{
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based
    // BRDFs". This is Filament's visibility term: the BRDF is D * V * F, with the
    // 4 * NoL * NoV denominator already folded into V.
    const float a2 = alpha * alpha;
    const float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    const float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1.0e-5);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH)
{
    const float f = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return f0 + (f90 - f0) * f;
}

vec3 F_Schlick(vec3 f0, float VoH)
{
    const float f = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return f + f0 * (1.0 - f);
}

/** Filament's surfaceShading(): a Lambert diffuse lobe and a Cook-Torrance specular
 * lobe, with the multiscatter energy compensation applied to the specular part.
 *
 * The wrapped diffuse this replaces was a readability hack; it made the direct
 * lighting and the IBL disagree about how the surface behaves away from the light.
 */
vec3 CalculateBRDF(
    LightContribution lightContrib,
    vec3 V,
    vec3 N,
    vec3 diffuseColor,
    vec3 f0,
    vec3 energyCompensation,
    float perceptualRoughness)
{
    const vec3 L = lightContrib.L;
    const float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0)
    {
        return vec3(0.0);
    }

    const vec3 H = normalize(V + L);
    const float NoV = max(dot(N, V), 0.0);
    const float NoH = max(dot(N, H), 0.0);
    const float LoH = max(dot(L, H), 0.0);
    const float alpha = perceptualRoughness * perceptualRoughness;

    const float D = D_GGX(NoH, alpha);
    const float Vis = V_SmithGGXCorrelated(NoV, NoL, alpha);
    const vec3 F = F_Schlick(f0, 1.0, LoH);
    const vec3 Fr = (D * Vis) * F;
    const vec3 Fd = diffuseColor * (1.0 / PI);

    return (Fd + Fr * energyCompensation) * lightContrib.radiance * NoL;
}

vec3 PBRLighting(
    vec3 albedo,
    float metallic,
    float perceptualRoughness,
    float ao,
    vec3 normal,
    vec3 viewDir,
    vec3 fragPos,
    sampler2D shadowMap,
    mat4 lightSpaceMatrix,
    samplerCube irradianceCube,
    samplerCube prefilterCube,
    sampler2D dfgLut,
    float transmission,
    float refractionIndex
)
{
    metallic = clamp(metallic, 0.0, 1.0);
    perceptualRoughness = clamp(perceptualRoughness, 0.04, 1.0);
    ao = clamp(ao, 0.0, 1.0);
    normal = normalize(normal);

    // `albedo` is linear here. The materials decode their sRGB sources (texture, picker tint,
    // edge/domain colours) separately before calling in, so the direct and the image based
    // lighting both work on the same linear base colour.
    const vec3 N = normalize(normal);
    const vec3 V = normalize(viewDir);
    const float NoV = max(dot(N, V), 1.0e-4);
    const vec3 diffuseColor = albedo * (1.0 - metallic);
    const vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // The DFG term is needed for the direct lights as well: it carries the energy
    // compensation that restores the energy single-scattering GGX loses at high roughness.
    // The LUT is (scale, bias) for F0 * scale + bias, so the value at F0 = 1 is scale + bias;
    // dividing by the bias alone (Filament's layout) made rough materials many times too bright.
    vec2 dfg = textureLod(dfgLut, vec2(NoV, perceptualRoughness), 0.0).rg;
    if (any(isnan(dfg)) || any(isinf(dfg)))
    {
        dfg = vec2(0.0);
    }
    dfg = clamp(dfg, 0.0, 1.0);
    const vec3 energyCompensation = vec3(1.0) + f0 * (1.0 / max(dfg.x + dfg.y, 1.0e-4) - 1.0);

    vec3 Lo = vec3(0.0);
    vec3 ambient = vec3(0.0);

    for (int i = 0; i < ssbo_Lights.length(); ++i)
    {
        const Light light = ExtractLight(ssbo_Lights[i]);

        switch (light.type)
        {
#if defined(VARIANT_HAS_DYNAMIC_LIGHTING)
            case 0: // Point Light
            {
                const PointLight pointLight = ExtractPointLight(light);
                const LightContribution contrib = CalculatePointLightContribution(pointLight, fragPos);
                Lo += CalculateBRDF(contrib, V, N, diffuseColor, f0, energyCompensation, perceptualRoughness);
                break;
            }
#endif

#if defined(VARIANT_HAS_DIRECTIONAL_LIGHTING)
            case 1: // Directional Light
            {
                const DirectionalLight dirLight = ExtractDirectionalLight(light);
#if defined(VARIANT_HAS_SHADOWING)
                const LightContribution contrib = CalculateDirectionalLightContribution(GateShadowOnReceiver(dirLight), fragPos, N, shadowMap, lightSpaceMatrix);
#else
                // The variant has no shadow lookup: make sure the light cannot trigger one.
                DirectionalLight unshadowed = dirLight;
                unshadowed.hasShadow = false;
                const LightContribution contrib = CalculateDirectionalLightContribution(unshadowed, fragPos, N, shadowMap, lightSpaceMatrix);
#endif
                Lo += CalculateBRDF(contrib, V, N, diffuseColor, f0, energyCompensation, perceptualRoughness);
                break;
            }
#endif

#if defined(VARIANT_HAS_DYNAMIC_LIGHTING)
            case 2: // Spot Light
            {
                const SpotLight spotLight = ExtractSpotLight(light);
                const LightContribution contrib = CalculateSpotLightContribution(spotLight, fragPos);
                Lo += CalculateBRDF(contrib, V, N, diffuseColor, f0, energyCompensation, perceptualRoughness);
                break;
            }
#endif

            case 3: // Ambient Box Light
            {
                const AmbientBoxLight boxLight = ExtractAmbientBoxLight(light);
                ambient += CalculateAmbientBoxLightContribution(boxLight, fragPos);
                break;
            }

            case 4: // Ambient Sphere Light
            {
                const AmbientSphereLight sphereLight = ExtractAmbientSphereLight(light);
                ambient += CalculateAmbientSphereLightContribution(sphereLight, fragPos);
                break;
            }
        }
    }

    vec3 imageBasedLighting = vec3(0.0);
#if !defined(DISABLE_IMAGE_BASED_LIGHTING)
    imageBasedLighting = CalculateImageBasedLighting(
        irradianceCube,
        prefilterCube,
        dfgLut,
        fragPos,
        V,
        N,
        perceptualRoughness,
        diffuseColor,
        f0,
        transmission,
        refractionIndex,
        ao
    );
#endif

    // The IBL applies AO per lobe (diffuse by ao, specular by specularAO); the ambient
    // lights (box/sphere) still take the plain ao.
    return ambient * ao + imageBasedLighting + Lo;
}
