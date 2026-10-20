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
    mat4 lightSpaceMatrix)
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
                    CalculatePointLightContribution(pointLight, fragPos);
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
                    CalculateSpotLightContribution(spotLight, fragPos);
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
    color += evaluateIBL(pixel, n, v, material.ambientOcclusion, irradianceCube, prefilterCube);

    return color;
}
