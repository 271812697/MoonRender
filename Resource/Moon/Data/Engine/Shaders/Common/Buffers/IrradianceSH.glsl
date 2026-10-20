// The diffuse irradiance as spherical harmonics, Filament's IndirectLight SH9. The
// coefficients are projected from the sky on the CPU (SkyboxRenderPass) and uploaded to
// binding point 4; when the buffer has not been filled yet its .a flag is zero and the
// shaders keep sampling the baked irradiance cube.
layout (std140, binding = 4) uniform IrradianceSH
{
    vec4 ubo_IrradianceSH[9];
};

bool HasIrradianceSH()
{
    return ubo_IrradianceSH[0].a > 0.5;
}

/** Irradiance (already divided by PI) for a normal, Ramamoorthi's SH9 basis. */
vec3 EvaluateIrradianceSH(vec3 n)
{
    const float Y00 = 0.282095;
    const float Y1 = 0.488603;
    const float Y2a = 1.092548;
    const float Y2b = 0.315392;
    const float Y2c = 0.546274;

    const vec3 c0 = ubo_IrradianceSH[0].rgb;
    const vec3 c1 = ubo_IrradianceSH[1].rgb;
    const vec3 c2 = ubo_IrradianceSH[2].rgb;
    const vec3 c3 = ubo_IrradianceSH[3].rgb;
    const vec3 c4 = ubo_IrradianceSH[4].rgb;
    const vec3 c5 = ubo_IrradianceSH[5].rgb;
    const vec3 c6 = ubo_IrradianceSH[6].rgb;
    const vec3 c7 = ubo_IrradianceSH[7].rgb;
    const vec3 c8 = ubo_IrradianceSH[8].rgb;

    const vec3 result =
        c0 * Y00 +
        c1 * (Y1 * n.y) +
        c2 * (Y1 * n.z) +
        c3 * (Y1 * n.x) +
        c4 * (Y2a * n.x * n.y) +
        c5 * (Y2a * n.y * n.z) +
        c6 * (Y2b * (3.0 * n.z * n.z - 1.0)) +
        c7 * (Y2a * n.x * n.z) +
        c8 * (Y2c * (n.x * n.x - n.y * n.y));

    return max(result, vec3(0.0));
}
