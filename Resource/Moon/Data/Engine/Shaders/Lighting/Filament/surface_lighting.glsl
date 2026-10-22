// Filament standard model - the lighting loop.
// Ported from filament/shaders/src/surface_lighting.fs + surface_light_directional.fs +
// surface_light_punctual.fs, adapted to the engine's light SSBO (Light.glsl) and to the
// three IBL maps.

#include ":Shaders/Common/Buffers/LightsSSBO.glsl"
#include ":Shaders/Lighting/Light.glsl"
#include ":Shaders/Lighting/Filament/surface_common.glsl"
#include ":Shaders/Lighting/Filament/surface_brdf.glsl"
#include ":Shaders/Lighting/Filament/surface_shading_parameters.glsl"
#include ":Shaders/Lighting/Filament/surface_shading_model_standard.glsl"
#include ":Shaders/Lighting/Filament/surface_light_indirect.glsl"

// Set by ShadowRenderFeature only for materials that receive shadows. A material that is not
// a receiver must not look the shadow map up at all: the sampler would read whatever texture
// unit happens to hold.
uniform bool u_ReceiveShadows = false;

// Filament surface_light_punctual.fs. Kept here, not in Light.glsl, so the engine's Standard
// material keeps its own attenuation curve; FilamentPBR is the material compared against
// Filament.
float FilamentSquareFalloff(float distanceSquare, float falloff)
{
    const float factor = distanceSquare * falloff;
    const float smoothFactor = saturate(1.0 - factor * factor);
    return smoothFactor * smoothFactor;
}

float FilamentDistanceAttenuation(vec3 posToLight, float falloff)
{
    const float distanceSquare = dot(posToLight, posToLight);
    // A punctual light is assumed to occupy a 1 cm sphere, so 1/d^2 stays finite.
    return FilamentSquareFalloff(distanceSquare, falloff) / max(distanceSquare, 1e-4);
}

float FilamentPunctualAttenuation(vec3 posToLight, float constant, float linear,
    float quadratic, float range)
{
    if (range > 0.0)
    {
        // Filament getDistanceAttenuation(): inverse-square with a smooth cutoff at the
        // light's effect range. The engine derives that range from the polynomial coefficients
        // (Light::CalculateEffectRange), so they still decide where the light ends.
        const float invCutoff = 1.0 / range;
        return FilamentDistanceAttenuation(posToLight, invCutoff * invCutoff);
    }
    // No finite effect range: keep the engine's classic inverse-polynomial falloff. The
    // preview's intensity-zero rig uses constant = 1, quadratic = 0 for a distance-independent
    // fill, which Filament's inverse-square model cannot express; this is the one exception.
    const float distance = length(posToLight);
    return 1.0 / (constant + linear * distance + quadratic * distance * distance);
}

LightContribution CalculateFilamentPointLightContribution(PointLight light, vec3 fragPos)
{
    const vec3 posToLight = light.position - fragPos;
    const float attenuation = FilamentPunctualAttenuation(
        posToLight, light.constant, light.linear, light.quadratic, light.range);
    return LightContribution(light.color * light.intensity * attenuation, normalize(posToLight));
}

LightContribution CalculateFilamentSpotLightContribution(SpotLight light, vec3 fragPos)
{
    const vec3 posToLight = light.position - fragPos;
    float attenuation = FilamentPunctualAttenuation(
        posToLight, light.constant, light.linear, light.quadratic, light.range);

    // Filament getAngleAttenuation(): saturate(cosTheta * scale + offset)^2, with
    // scale = 1 / max(1/1024, cosInner - cosOuter) and offset = -cosOuter * scale
    // (see LightManager::setSpotLightCone).
    const float cutOff = cos(radians(light.innerCutoff));
    const float outerCutOff = cos(radians(light.outerCutoff));
    const float scale = 1.0 / max(1.0 / 1024.0, cutOff - outerCutOff);
    const float offset = -outerCutOff * scale;
    const float theta = dot(normalize(posToLight), normalize(-light.direction));
    const float angle = saturate(theta * scale + offset);
    attenuation *= angle * angle;

    return LightContribution(light.color * light.intensity * attenuation, normalize(posToLight));
}

DirectionalLight GateShadowOnReceiver(DirectionalLight light)
{
    light.hasShadow = light.hasShadow && u_ReceiveShadows;
    return light;
}

vec3 evaluateFilamentSurface(
    FilamentMaterialInputs material,
    PixelParams pixel,
    vec3 fragPos,
    vec3 n,
    vec3 v,
    samplerCube irradianceCube,
    samplerCube prefilterCube,
    sampler2D shadowMap,
    mat4 lightSpaceMatrix,
    sampler2D dfgLut)
{
    vec3 color = vec3(0.0);

    for (int i = 0; i < ssbo_Lights.length(); ++i)
    {
        const Light light = ExtractLight(ssbo_Lights[i]);

        switch (light.type)
        {
#if defined(VARIANT_HAS_DYNAMIC_LIGHTING)
            case 0: // Point
            {
                const PointLight pointLight = ExtractPointLight(light);
                const LightContribution contribution =
                    CalculateFilamentPointLightContribution(pointLight, fragPos);
                color += surfaceShading(pixel, n, v, contribution.L, contribution.radiance);
                break;
            }
#endif

#if defined(VARIANT_HAS_DIRECTIONAL_LIGHTING)
            case 1: // Directional
            {
                const DirectionalLight directionalLight = ExtractDirectionalLight(light);
#if defined(VARIANT_HAS_SHADOWING)
                const LightContribution contribution = CalculateDirectionalLightContribution(
                    GateShadowOnReceiver(directionalLight), fragPos, n, shadowMap, lightSpaceMatrix);
                color += surfaceShading(pixel, n, v, contribution.L, contribution.radiance);
#else
                color += surfaceShading(pixel, n, v, -directionalLight.direction,
                    directionalLight.color * directionalLight.intensity);
#endif
                break;
            }
#endif

#if defined(VARIANT_HAS_DYNAMIC_LIGHTING)
            case 2: // Spot
            {
                const SpotLight spotLight = ExtractSpotLight(light);
                const LightContribution contribution =
                    CalculateFilamentSpotLightContribution(spotLight, fragPos);
                color += surfaceShading(pixel, n, v, contribution.L, contribution.radiance);
                break;
            }
#endif

            case 3: // Ambient box
            {
                const AmbientBoxLight boxLight = ExtractAmbientBoxLight(light);
                color += CalculateAmbientBoxLightContribution(boxLight, fragPos) * pixel.diffuseColor;
                break;
            }

            case 4: // Ambient sphere
            {
                const AmbientSphereLight sphereLight = ExtractAmbientSphereLight(light);
                color += CalculateAmbientSphereLightContribution(sphereLight, fragPos) * pixel.diffuseColor;
                break;
            }
        }
    }

    // AO is applied inside evaluateIBL: diffuse by ao, specular by Filament's specularAO().
    color += evaluateIBL(pixel, n, v, material.ambientOcclusion, irradianceCube, prefilterCube, dfgLut);

    return color;
}
