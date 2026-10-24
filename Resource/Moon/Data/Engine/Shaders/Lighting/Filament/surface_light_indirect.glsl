// Filament standard model - image based lighting.
// Ported from filament/shaders/src/surface_light_indirect.fs, adapted to the engine's
// three IBL maps (irradiance cube, prefiltered cube, DFG LUT) and the reflection block.
// The prefiltered cube is baked with Filament's LOD-to-perceptualRoughness mapping (see
// SkyboxRenderPass), so the sample LOD is Filament's p * (2 - p) curve.

#include ":Shaders/Common/Buffers/ReflectionUBO.glsl"

// Filament has no separate irradiance map: it samples the roughness-one level of the prefiltered
// specular cube and averages four taps around `n` (diffuseIrradianceRoughnessOne below). That is
// what Fd uses again. The engine's 32x32 irradiance cube was used instead for a while because the
// preview's prefiltered cube had been measured black at that level; with the bake fixed it is not,
// and the small cube turned out to be the visible defect: 32x32 with no filtering across cube
// faces, so around a sphere's pole its face borders read as a soft square patch. The 4-tap
// roughness-one sample is Filament's path and has no such seam.
vec3 diffuseIrradianceRoughnessOne(samplerCube prefilterCube, vec3 n)
{
    const float maxLod = float(textureQueryLevels(prefilterCube) - 1);
    const ivec2 size = textureSize(prefilterCube, int(maxLod));
    const float du = 1.0 / float(size.x);
    const float dv = 1.0 / float(size.y);
    // Filament's high-quality path averages four taps around n. At the 1x1-per-face level
    // this is what smooths the cube-face seams; a single tap produces visible face bands.
    const vec3 up = abs(n.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    const vec3 m0 = normalize(cross(n, up));
    const vec3 m1 = cross(m0, n);
    const vec3 m0du = m0 * du;
    const vec3 m1dv = m1 * dv;
    vec3 irradiance = textureLod(prefilterCube, n - m0du - m1dv, maxLod).rgb;
    irradiance += textureLod(prefilterCube, n + m0du - m1dv, maxLod).rgb;
    irradiance += textureLod(prefilterCube, n + m0du + m1dv, maxLod).rgb;
    irradiance += textureLod(prefilterCube, n - m0du + m1dv, maxLod).rgb;
    irradiance *= 0.25;
    return irradiance;
}

// The irradiance cube is 32x32 and holds a cosine convolution, so it is a very low frequency
// signal: sampling a blurred level loses nothing. It is sampled at level 2 (8x8 per face) with
// four taps a face-texel apart. Level 0 is too coarse in the other direction - one of its texels
// magnified over a sphere's pole reads as a soft square patch, and a single tap leaves the cube
// face borders visible as a seam that shows up the same way. The taps are sized for the level
// they are taken from, not for level 0.
vec3 diffuseIrradiance(samplerCube irradianceCube, vec3 n)
{
    const float lod = 2.0;
    const ivec2 size = textureSize(irradianceCube, int(lod));
    const float du = 1.0 / float(size.x);
    const float dv = 1.0 / float(size.y);
    const vec3 up = abs(n.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    const vec3 m0 = normalize(cross(n, up));
    const vec3 m1 = cross(n, m0);
    vec3 irradiance = textureLod(irradianceCube, n - m0 * du - m1 * dv, lod).rgb;
    irradiance += textureLod(irradianceCube, n + m0 * du - m1 * dv, lod).rgb;
    irradiance += textureLod(irradianceCube, n + m0 * du + m1 * dv, lod).rgb;
    irradiance += textureLod(irradianceCube, n - m0 * du + m1 * dv, lod).rgb;
    irradiance *= 0.25;
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

vec3 getSpecularDominantDirection(vec3 n, vec3 r, float roughness)
{
    // Filament calls this with pixel.roughness, which is already the linear roughness
    // (perceptualRoughness^2); squaring again gives the p^4 pull used by Filament.
    return mix(r, n, roughness * roughness);
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
    const vec3 r = getSpecularDominantDirection(n, reflect(-v, n), pixel.roughness);
    const float NoV = max(dot(n, v), 1.0e-4);

    // Filament applies singleBounceAO() (= 1 when multi-bounce AO is on) and then
    // multiBounceSpecularAO(), which is the same GTAO polynomial with f0 as the albedo.
    vec3 Fr = E * prefilteredRadiance(prefilterCube, r, pixel.perceptualRoughness)
        * pixel.energyCompensation
        * multiBounceAO(specularAO(NoV, ao, pixel.perceptualRoughness), pixel.f0);
    // Diffuse from the engine's irradiance cube, not from the prefilter's roughness-one mip
    // (see diffuseIrradianceRoughnessOne for why).
    vec3 Fd = pixel.diffuseColor * diffuseIrradiance(irradianceCube, n)
        * (1.0 - E) * multiBounceAO(ao, pixel.diffuseColor);

    // Filament isEvaluateClearCoatIBL(): the clear coat is its own specular layer with
    // F0 = 0.04, evaluated at clearCoatPerceptualRoughness (its own DFG lookup and its own
    // reflection vector), and the base lobes are attenuated by (1 - Fc). With a normal map
    // the coat uses the geometric normal.
    if (pixel.clearCoat > 0.0)
    {
        const float ccRoughness = pixel.clearCoatRoughness;
        const vec3 ccN = pixel.clearCoatNormal;
        const float ccNoV = max(dot(ccN, v), 1.0e-4);
        const float ccAlpha = ccRoughness * ccRoughness;
        const vec3 ccR = getSpecularDominantDirection(ccN, reflect(-v, ccN), ccAlpha);
        vec2 ccDfg = textureLod(dfgLut, vec2(ccNoV, ccRoughness), 0.0).rg;
        ccDfg = clamp(ccDfg, 0.0, 1.0);
        const vec3 Ecc = specularDFG(vec3(0.04), ccDfg);
        const vec3 ccEnergy = vec3(1.0) + vec3(0.04)
            * (1.0 / max(ccDfg.x + ccDfg.y, 1.0e-4) - 1.0);
        const float Fc = F_Schlick(0.04, 1.0, ccNoV) * pixel.clearCoat;
        const vec3 cc = prefilteredRadiance(prefilterCube, ccR, ccRoughness);
        const vec3 ccAO = multiBounceAO(specularAO(ccNoV, ao, ccRoughness), vec3(0.04));
        Fd *= (1.0 - Fc);
        Fr *= (1.0 - Fc);
        Fr += Ecc * cc * ccEnergy * ccAO * pixel.clearCoat;
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
