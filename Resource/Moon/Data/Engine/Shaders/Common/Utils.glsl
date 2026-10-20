vec3 UnPack(float target)
{
    return vec3 (
        float((uint(target) >> 24) & 0xff) * 0.003921568627451,
        float((uint(target) >> 16) & 0xff) * 0.003921568627451,
        float((uint(target) >> 8) & 0xff) * 0.003921568627451
    );
}

vec2 TileAndOffsetTexCoords(vec2 texCoords, vec2 tiling, vec2 offset)
{
    return texCoords * tiling + offset;
}

bool IsOrthographic(mat4 projectionMatrix)
{
    // In an orthographic projection matrix, the [3][3] element is 1.0
    // In a perspective projection matrix, it's 0.0
    return projectionMatrix[3][3] > 0.5;
}

float DistanceFadeAlpha(vec3 fragPos, vec3 viewPos, float fadeStart, float fadeLength)
{
    const float distanceToFragment = length(fragPos - viewPos);
    const float alphaBasedOnDistance = 1.0 - (distanceToFragment - fadeStart) / fadeLength;
    return alphaBasedOnDistance;
}


mat4 Perspective(const float p_fov, const float p_aspectRatio, const float p_zNear, const float p_zFar)
{

    const float tangent = tan(p_fov / 2.0f * 3.14159265359f/ 180.0f);
	const float h = p_zNear * tangent;
	const float w = h * p_aspectRatio;

    const float maxView = 2.0f * p_zNear;
    const float width = 2.0f* w;
    const float height = 2.0f*h;
    const float zRange = p_zFar - p_zNear;

    mat4 m=mat4(
        maxView / width,  0.0, 0.0, 0.0,
        0.0,  maxView / height, 0.0,  0.0,
        0.0, 0.0, (-p_zFar - p_zNear) / zRange, (-maxView * p_zFar) / zRange,
        0.0, 0.0, -1.0, 0.0
    );
   return transpose(m);

}
float Dithering(float alpha, vec2 fragCoord)
{
    const mat4 kThresholdMatrix = mat4(
        1.0 / 17.0,  9.0 / 17.0,  3.0 / 17.0, 11.0 / 17.0,
        13.0 / 17.0,  5.0 / 17.0, 15.0 / 17.0,  7.0 / 17.0,
        4.0 / 17.0, 12.0 / 17.0,  2.0 / 17.0, 10.0 / 17.0,
        16.0 / 17.0,  8.0 / 17.0, 14.0 / 17.0,  6.0 / 17.0
    );

    return alpha - kThresholdMatrix[int(fragCoord.x) % 4][int(fragCoord.y) % 4];
}

// Expects a height map with values in the range [0, 1].
// 1.0 means the height is at the maximum depth, 0.0 means the height is at the minimum depth.
vec2 ApplyParallaxOcclusionMapping(vec2 texCoords, sampler2D heightMap, vec3 tangentViewPos, vec3 tangentFragPos, float heightScale, int minLayers, int maxLayers)
{
    const vec3 viewDir = normalize(tangentViewPos - tangentFragPos);

    // calculate optimal layer count
    minLayers = max(minLayers, 1); // Ensure minLayers is at least 1
    maxLayers = min(maxLayers, 8192); // Limit maxLayers to a reasonable value (2^13)
    maxLayers = max(maxLayers, minLayers); // Ensure maxLayers is at least minLayers

    // clamp alpha to prevent extrapolating (which could make the fragment very expensive, and potentially result in a crash).
    const float alpha = clamp(abs(dot(vec3(0.0, 0.0, 1.0), viewDir)), 0.0, 1.0);
    const float numLayers = mix(maxLayers, minLayers, alpha);

    // calculate the size of each layer
    const float layerDepth = 1.0 / numLayers;
    // the amount to shift the texture coordinates per layer (from vector P)
    const vec2 P = viewDir.xy / viewDir.z * heightScale; 
    const vec2 deltaTexCoords = P / numLayers;
  
    // get initial values
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = 1.0 - texture(heightMap, currentTexCoords).r;
    float currentLayerDepth = 0.0;
      
    while(currentLayerDepth < currentDepthMapValue)
    {
        // shift texture coordinates along direction of P
        currentTexCoords -= deltaTexCoords;
        // get depthmap value at current texture coordinates
        currentDepthMapValue = 1.0 - texture(heightMap, currentTexCoords).r;
        // get depth of next layer
        currentLayerDepth += layerDepth;
    }
    
    // get texture coordinates before collision (reverse operations)
    const vec2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // get depth after and before collision for linear interpolation
    const float afterDepth  = currentDepthMapValue - currentLayerDepth;
    const float beforeDepth = 1.0 - texture(heightMap, prevTexCoords).r - currentLayerDepth + layerDepth;
 
    // interpolation of texture coordinates
    const float weight = afterDepth / (afterDepth - beforeDepth);
    const vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}

bool IsParallaxOutOfBounds(vec2 texCoords, vec2 tiling, vec2 offset, mat4 projectionMatrix)
{
    const vec2 adjustedCoords = texCoords - offset;

    return
        !IsOrthographic(projectionMatrix) && // No clipping in orthographic projection (not supported)
        (adjustedCoords.x < 0.0 || adjustedCoords.x > tiling.x || adjustedCoords.y < 0.0 || adjustedCoords.y > tiling.y);
}

// [Deprecated] Kept for backward compatibility. Prefer using `ApplyParallaxOcclusionMapping()` instead.
vec2 ApplyParallaxMapping(vec2 texCoords, sampler2D heightMap, vec3 tangentViewPos, vec3 tangentFragPos, float heightScale)
{
    if (heightScale > 0)
    {
        const vec3 viewDir = normalize(tangentViewPos - tangentFragPos);
        const vec2 parallax = viewDir.xy * heightScale * texture(heightMap, texCoords).r;
        return texCoords - vec2(parallax.x, 1.0 - parallax.y);
    }

    return texCoords;
}

// [Deprecated] Kept for backward compatibility.
bool IsMasked(sampler2D maskMap, vec2 texCoords)
{
    return texture(maskMap, texCoords).r == 0.0;
}

mat3 ConstructTBN(mat4 model, vec3 normal, vec3 tangent, vec3 bitangent)
{
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    return mat3(
        normalize(normalMatrix * tangent),
        normalize(normalMatrix * bitangent),
        normalize(normalMatrix * normal)
    );
}
// Derivatives only exist in a fragment stage: a vertex or geometry stage that carries this
// header (MoonMatc puts the prelude in every stage) would be a compile error, and the spec
// is right - nothing outside a fragment stage can call it. MoonMatc defines the stage, so the
// function simply is not there in the stages that cannot use it. A shader compiled the old way
// (no such #define) keeps it, exactly as before.
#if !defined(MOON_STAGE_VERTEX) && !defined(MOON_STAGE_GEOMETRY)
mat3 ComputeTBN(const vec3 position, const vec3 normal, const vec2 uv) {
    vec3 dpx = dFdxFine(position);
    vec3 dpy = dFdyFine(position);
    vec2 duu = dFdxFine(uv);
    vec2 dvv = dFdyFine(uv);

    vec3 N = normalize(normal);
    vec3 T = normalize(dpx * dvv.t - dpy * duu.t);
    vec3 B = -normalize(cross(N, T));

    return mat3(T, B, N);
}
#endif


vec3 ComputeNormal(vec2 texCoords, vec3 normal, sampler2D normalMap, mat3 TBN)
{
    vec3 sampled = texture(normalMap, texCoords).rgb;
    // The engine binds its white 1x1 texture when the material has no normal map. White
    // decodes to (1,1,1), a normal tilted 45 degrees, which darkens the whole surface; a
    // flat tangent-space normal is (0.5, 0.5, 1).
    if (all(greaterThan(sampled, vec3(0.99))))
    {
        sampled = vec3(0.5, 0.5, 1.0);
    }
    const vec3 tangentNormal = normalize(sampled * 2.0 - 1.0);
    return normalize(TBN * tangentNormal);
}

// Filament's multiBounceAO (Jimenez 2016, "Practical Strategies for Managing Lighting in
// Games"): a plain ao factor darkens cavities too much because it ignores the light that
// bounces back into them. This approximation restores it from the diffuse colour.
vec3 multiBounceAO(float ao, vec3 diffuseColor)
{
    vec3 a = 2.0404 * diffuseColor - 0.3324;
    vec3 b = -4.7951 * diffuseColor + 0.6417;
    vec3 c = 2.7552 * diffuseColor + 0.6903;
    return max(vec3(ao), ((ao * a + b) * ao + c) * ao);
}

// [Deprecated] Kept for backward compatibility. Prefer using `ComputeNormal()` without the `enableNormalMapping` parameter,
// and handle branching using preprocessor directives instead.
vec3 ComputeNormal(bool enableNormalMapping, vec2 texCoords, vec3 normal, sampler2D normalMap, mat3 TBN)
{
    if (enableNormalMapping)
    {
        return ComputeNormal(texCoords, normal, normalMap, TBN);
    }

    return normalize(normal);
}
