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
    samplerCube prefilterCube,
    sampler2D dfgLut)
{
    // Filament specularDFG(): with specularFactor/specularColorFactor the DFG result is scaled
    // by specularFactor as well (f0 already carries the color factor).
    const vec3 E = specularDFG(pixel.f0, pixel.dfg) * pixel.specular;
    const vec3 r = getSpecularDominantDirection(n, reflect(-v, n), pixel.perceptualRoughness);
    const float NoV = max(dot(n, v), 1.0e-4);

    vec3 Fr = E * prefilteredRadiance(prefilterCube, r, pixel.perceptualRoughness)
        * pixel.energyCompensation * specularAO(NoV, ao, pixel.perceptualRoughness);
    vec3 Fd = pixel.diffuseColor * diffuseIrradiance(irradianceCube, n) * (1.0 - E) * multiBounceAO(ao, pixel.diffuseColor);

    // Filament isEvaluateClearCoatIBL(): the clear coat is its own specular layer with
    // F0 = 0.04, evaluated at clearCoatPerceptualRoughness (its own DFG lookup), and the
    // base lobes are attenuated by (1 - Fc).
    if (pixel.clearCoat > 0.0)
    {
        const float ccRoughness = pixel.clearCoatRoughness;
        vec2 ccDfg = textureLod(dfgLut, vec2(NoV, ccRoughness), 0.0).rg;
        ccDfg = clamp(ccDfg, 0.0, 1.0);
        const vec3 Ecc = specularDFG(vec3(0.04), ccDfg);
        const vec3 ccEnergy = vec3(1.0) + vec3(0.04)
            * (1.0 / max(ccDfg.x + ccDfg.y, 1.0e-4) - 1.0);
        const float Fc = F_Schlick(0.04, 1.0, NoV) * pixel.clearCoat;
        const vec3 cc = prefilteredRadiance(prefilterCube, r, ccRoughness);
        Fd *= (1.0 - Fc);
        Fr *= (1.0 - Fc);
        Fr += Ecc * cc * ccEnergy * specularAO(NoV, ao, ccRoughness) * pixel.clearCoat;
    }

    vec3 Ft = vec3(0.0);
    if (pixel.transmission > 0.0)
    {
        // Filament's solid-sphere cubemap refraction: bend into the sphere, hit the back
        // interface, bend again, and sample the prefiltered environment along that ray.
        const float etaIR = 1.0 / max(pixel.refractionIndex, 1.0);
        const float etaRI = max(pixel.refractionIndex, 1.0);
        vec3 r = refract(-v, n, etaIR);
        const float NoR = dot(n, r);
        const float d = pixel.thickness * max(-NoR, 0.0);
        const vec3 n1 = normalize(NoR * r - n * 0.5);
        vec3 rayDirection = refract(r, n1, etaRI);
        // Total internal reflection at the second interface: refract() returns 0, which
        // would sample one fixed cubemap texel; Filament keeps the internally reflected ray.
        if (dot(rayDirection, rayDirection) < 1.0e-6)
        {
            rayDirection = reflect(r, n1);
        }
        const float refractionRoughness = mix(pixel.perceptualRoughness, 0.0,
            saturate(etaIR * 3.0 - 2.0));
        // Filament evaluateRefraction(): Beer-Lambert extinction over the distance the ray
        // travels inside the medium, using the same thickness/ray distance as the refraction.
        const vec3 transmittance = min(vec3(1.0), exp(-pixel.absorption * d));
        // Filament's cubemap refraction samples the environment seen through the surface.
        // The preview binds its background here (a dark studio surround), which is why a
        // fully transmissive ball reads as black instead of the bright IBL.
        // Filament samples the same prefiltered IBL the reflections use; there is no
        // separate refraction cube.
        Ft = prefilteredRadiance(prefilterCube, rayDirection, refractionRoughness)
            * pixel.diffuseColor * pixel.transmission * (1.0 - E) * ao * transmittance;
        Fd *= (1.0 - pixel.transmission);
    }

    return (Fd + Fr + Ft) * ubo_ReflectionProbeData.brightness;
}
