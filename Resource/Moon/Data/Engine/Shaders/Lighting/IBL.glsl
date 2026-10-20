#include ":Shaders/Common/Buffers/ReflectionUBO.glsl"
#include ":Shaders/Common/Buffers/IrradianceSH.glsl"

// Image based lighting, following Filament's standard model (surface_light_indirect.fs):
// a split-sum prefiltered cubemap for the specular term, an irradiance cubemap for the diffuse
// term, and the DFG lookup table both for the specular Fresnel scale/bias and for the
// multiscatter energy compensation. The single environment map with a Frostbite BRDF
// approximation this replaces could not express any of that: its irradiance was the highest
// mip of the reflection cube (a blurry reflection, not an irradiance) and the approximation
// folded a different geometry term into the result than the direct lighting uses.
//
// The irradiance cube is baked with the Lambert kernel including the 1/PI factor (see
// SkyboxIrrandiance.ovfx), so the diffuse term is used straight, as Filament does.

bool IsPointInOBB(vec3 point, vec3 obbCenter, vec3 obbHalfExtents, mat3 obbOrientation)
{
    vec3 localPoint = obbOrientation * (point - obbCenter);

    return abs(localPoint.x) <= obbHalfExtents.x &&
           abs(localPoint.y) <= obbHalfExtents.y &&
           abs(localPoint.z) <= obbHalfExtents.z;
}

// https://seblagarde.wordpress.com/wp-content/uploads/2012/08/parallax_corrected_cubemap-siggraph2012.pdf
vec3 BoxProjectionOBB(vec3 reflectionVector, vec3 worldPos, vec3 probePos, vec3 obbCenter, vec3 obbHalfExtents, mat3 obbOrientation)
{
    vec3 localPos = obbOrientation * (worldPos - obbCenter);
    vec3 localReflection = obbOrientation * reflectionVector;
    vec3 localProbePos = obbOrientation * (probePos - obbCenter);

    vec3 ndr = normalize(localReflection);
    vec3 rbmax = (obbHalfExtents - localPos) / ndr;
    vec3 rbmin = (-obbHalfExtents - localPos) / ndr;

    vec3 rbminmax;
    rbminmax.x = (ndr.x > 0.0) ? rbmax.x : rbmin.x;
    rbminmax.y = (ndr.y > 0.0) ? rbmax.y : rbmin.y;
    rbminmax.z = (ndr.z > 0.0) ? rbmax.z : rbmin.z;
    float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);

    vec3 localIntersection = localPos + ndr * fa;
    vec3 worldIntersection = transpose(obbOrientation) * localIntersection + obbCenter;

    return normalize(worldIntersection - probePos);
}

vec3 ParallaxCorrect(vec3 reflectionDir, vec3 worldPos)
{
    if (ubo_ReflectionProbeData.boxProjection)
    {
        return BoxProjectionOBB(
            reflectionDir,
            worldPos,
            ubo_ReflectionProbeData.position.xyz,
            ubo_ReflectionProbeData.boxCenter.xyz,
            ubo_ReflectionProbeData.boxHalfExtents.xyz,
            mat3(ubo_ReflectionProbeData.rotation)
        );
    }

    return reflectionDir;
}

/** The split-sum Fresnel term. The LUT stores the *scale* in R and the *bias* in G, i.e. the
 * Fresnel response is `F0 * A + B` (the standard LearnOpenGL layout, which SectionCap also
 * reads). Filament's `mix(dfg.x, dfg.y, f0)` assumes the opposite layout - x = value at F0 = 0,
 * y = value at F0 = 1 - so it cannot be used with this LUT. */
vec3 SpecularDFG(vec3 f0, vec2 dfg)
{
    return f0 * dfg.x + dfg.y;
}

/** Filament's `energyCompensation`: the single-scattering GGX lobe loses energy at high
 * roughness, and this restores it from the DFG table. Filament divides by the value of the
 * DFG term at F0 = 1, which for this LUT's layout is `A + B` (not the bias alone - dividing by
 * the bias is what turned rough metals into white). */
vec3 EnergyCompensation(vec3 f0, vec2 dfg)
{
    return vec3(1.0) + f0 * (1.0 / max(dfg.x + dfg.y, 1.0e-4) - 1.0);
}

// Filament's specularAO(): ambient occlusion attenuates the specular environment less than the
// diffuse one, because the specular lobe only covers a small cone.
float specularAO(float NoV, float ao, float perceptualRoughness)
{
    const float roughness = perceptualRoughness * perceptualRoughness;
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

vec3 CalculateImageBasedLighting(
    samplerCube irradianceCube,
    samplerCube prefilterCube,
    sampler2D dfgLut,
    vec3 fragPos,
    vec3 V,
    vec3 N,
    float perceptualRoughness,
    vec3 diffuseColor,
    vec3 f0,
    float transmission,
    float refractionIndex,
    float ao
)
{
    // A local probe only lights what is inside its box.
    if (ubo_ReflectionProbeData.local && !IsPointInOBB(
        fragPos,
        ubo_ReflectionProbeData.boxCenter.xyz,
        ubo_ReflectionProbeData.boxHalfExtents.xyz,
        mat3(ubo_ReflectionProbeData.rotation)))
    {
        return vec3(0.0);
    }

    const vec3 n = normalize(N);
    const vec3 v = normalize(V);
    const float NoV = max(dot(n, v), 1.0e-4);
    const float maxLod = float(textureQueryLevels(prefilterCube) - 1);
    // Filament's perceptualRoughnessToLod(): the prefilter cube bakes each mip with the
    // inverse of this curve, so sampling here lands on the requested perceptual roughness.
    const float lod = maxLod * perceptualRoughness * (2.0 - perceptualRoughness);

    // The DFG LUT is indexed by (NdotV, perceptualRoughness) and stores scale in R, bias in G.
    // A NaN texel would make the energy compensation below infinite and the pixel black.
    vec2 dfg = textureLod(dfgLut, vec2(NoV, perceptualRoughness), 0.0).rg;
    if (any(isnan(dfg)) || any(isinf(dfg)))
    {
        dfg = vec2(0.0);
    }
    dfg = clamp(dfg, 0.0, 1.0);
    const vec3 E = SpecularDFG(f0, dfg);
    const vec3 energyCompensation = EnergyCompensation(f0, dfg);

    // Filament's `getSpecularDominantDirection`: pull the reflected direction toward the normal
    // as the surface gets rougher, then sample the roughness level.
    vec3 R = ParallaxCorrect(reflect(-v, n), fragPos);
    R = normalize(mix(R, n, perceptualRoughness * perceptualRoughness));
    vec3 prefiltered = textureLod(prefilterCube, R, lod).rgb;
    if (any(isnan(prefiltered)) || any(isinf(prefiltered)))
    {
        prefiltered = vec3(0.0);
    }
    const vec3 Fr = E * prefiltered * energyCompensation * specularAO(NoV, ao, perceptualRoughness);

    vec3 irradiance = HasIrradianceSH() ? EvaluateIrradianceSH(n) : texture(irradianceCube, n).rgb;
    if (any(isnan(irradiance)) || any(isinf(irradiance)))
    {
        irradiance = vec3(0.0);
    }
    vec3 Fd = diffuseColor * irradiance * (1.0 - E) * multiBounceAO(ao, diffuseColor);

    vec3 Ft = vec3(0.0);
    if (transmission > 0.0)
    {
        // Filament's refraction term: the environment seen through the surface, tinted by the
        // base colour and attenuated by transmission; the diffuse part stops where the surface
        // transmits.
        vec3 T = ParallaxCorrect(refract(-v, n, 1.0 / max(refractionIndex, 1.0)), fragPos);
        const vec3 refracted = textureLod(prefilterCube, T, lod).rgb;
        Ft = refracted * diffuseColor * transmission * (1.0 - E) * ao;
        Fd *= (1.0 - transmission);
    }

    return (Fd + Fr + Ft) * ubo_ReflectionProbeData.brightness;
}
