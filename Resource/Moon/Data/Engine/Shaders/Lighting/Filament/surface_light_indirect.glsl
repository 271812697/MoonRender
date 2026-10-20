// Filament standard model - image based lighting.
// Ported from filament/shaders/src/surface_light_indirect.fs, adapted to the engine's
// three IBL maps (irradiance cube, prefiltered cube, DFG LUT) and the reflection block.
// The prefiltered cube is baked with Filament's LOD-to-perceptualRoughness mapping (see
// SkyboxRenderPass), so the sample LOD is Filament's p * (2 - p) curve.

#include ":Shaders/Common/Buffers/ReflectionUBO.glsl"
#include ":Shaders/Common/Buffers/IrradianceSH.glsl"

vec3 diffuseIrradiance(samplerCube irradianceCube, vec3 n)
{
    vec3 irradiance = HasIrradianceSH() ? EvaluateIrradianceSH(n) : texture(irradianceCube, n).rgb;
    if (any(isnan(irradiance)) || any(isinf(irradiance)))
    {
        irradiance = vec3(0.0);
    }
    return irradiance;
}

vec3 prefilteredRadiance(samplerCube prefilterCube, vec3 r, float perceptualRoughness)
{
    const float maxLod = float(textureQueryLevels(prefilterCube) - 1);
    const float lod = maxLod * perceptualRoughness * (2.0 - perceptualRoughness);
    vec3 radiance = textureLod(prefilterCube, r, lod).rgb;
    if (any(isnan(radiance)) || any(isinf(radiance)))
    {
        radiance = vec3(0.0);
    }
    return radiance;
}

// Filament's specularDFG(), in the layout our LUT stores (see preparePixelParams).
vec3 specularDFG(vec3 f0, vec2 dfg)
{
    return f0 * dfg.x + dfg.y;
}

vec3 getSpecularDominantDirection(vec3 n, vec3 r, float perceptualRoughness)
{
    return mix(r, n, perceptualRoughness * perceptualRoughness);
}

// Filament's specularAO(): ambient occlusion attenuates the specular environment less than
// the diffuse one, because the specular lobe only sees a small cone. Multiplying the whole
// IBL by ao (as the caller used to) darkens reflections too much.
float specularAO(float NoV, float ao, float perceptualRoughness)
{
    const float roughness = perceptualRoughness * perceptualRoughness;
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

vec3 evaluateIBL(
    PixelParams pixel,
    vec3 n,
    vec3 v,
    float ao,
    samplerCube irradianceCube,
    samplerCube prefilterCube)
{
    const vec3 E = specularDFG(pixel.f0, pixel.dfg);
    const vec3 r = getSpecularDominantDirection(n, reflect(-v, n), pixel.perceptualRoughness);
    const float NoV = max(dot(n, v), 1.0e-4);

    const vec3 Fr = E * prefilteredRadiance(prefilterCube, r, pixel.perceptualRoughness)
        * pixel.energyCompensation * specularAO(NoV, ao, pixel.perceptualRoughness);
    vec3 Fd = pixel.diffuseColor * diffuseIrradiance(irradianceCube, n) * (1.0 - E) * multiBounceAO(ao, pixel.diffuseColor);

    vec3 Ft = vec3(0.0);
    if (pixel.transmission > 0.0)
    {
        // Refraction of the environment, tinted by the base colour, as Filament does.
        const vec3 T = refract(-v, n, 1.0 / 1.5);
        Ft = prefilteredRadiance(prefilterCube, T, pixel.perceptualRoughness)
            * pixel.diffuseColor * pixel.transmission * (1.0 - E) * ao;
        Fd *= (1.0 - pixel.transmission);
    }

    return (Fd + Fr + Ft) * ubo_ReflectionProbeData.brightness;
}
