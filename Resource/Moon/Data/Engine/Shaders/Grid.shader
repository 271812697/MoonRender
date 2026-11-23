// Grid: the mirror-plane grid the viewport draws under the scene - the mirrored scene itself,
// tinted, with the grid lines and its shadow blended on top.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md), with both stages written by this
// file. Neither stage fits the generated skeleton: the vertex stage turns the world position
// into plane-space texture coordinates (the inverse of u_PlaneTransform, so the grid follows
// the mirror plane) and builds its normal with a proper normal matrix instead of mat3(model),
// and the fragment stage samples the mirrored frame by screen position and writes the grid,
// shadow and horizon fade from ubo_ViewPos. MoonMatc generates nothing for these stages beyond
// the prelude, the variant #defines and the parameters - the attributes, the VS_OUT block, the
// output and the mains below are the whole stage.
//
// State from GridRenderPass::Draw: SetBlendable(true) -> transparent, SetBackfaceCulling(true)
// -> back, SetDepthWriting(false) -> depthWrite false (it is the ground, drawn over the scene).

material {
    name         : Grid,
    shadingModel : unlit,
    blending     : transparent,
    culling      : back,
    depthWrite   : false,

    vertexAttributes : [POSITION, UV0, NORMAL],

    parameters : [
        // The plane's model matrix (GridRenderPass::Draw sets it every frame); the vertex and
        // the fragment stage both invert it.
        { type : mat4,      name : u_PlaneTransform },
        // The grid tint, set per draw from the viewport's grid colour.
        { type : float3,    name : u_Color },
        // The mirrored frame and the blurred shadow map, sampled by screen position.
        { type : sampler2D, name : u_MirrorTex },
        { type : sampler2D, name : u_MirrorShadowTex },
        // Carried as declaration-time defaults: the draw site never sets them, so dropping the
        // initialisers would silently change the look of the grid.
        { type : float,     name : u_GridSize,     default : 0.01 },
        { type : float,     name : u_GridOpacity,  default : 0.7 },
        { type : float,     name : u_FadeDistance, default : 50.0 }
    ]
}

vertex {

    out VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;

    } vs_out;

    void main()
    {
        vec4 worldPos = ubo_Model * vec4(geo_Pos, 1.0);
        vs_out.FragPos = worldPos.xyz;

        mat4 worldToPlane = inverse(u_PlaneTransform);
        vec4 localPos = worldToPlane * worldPos;

        vs_out.TexCoords = localPos.xz;

        vs_out.Normal = normalize(mat3(transpose(inverse(ubo_Model))) * geo_Normal);


        gl_Position = ubo_Projection * ubo_View * worldPos;
    }
}

fragment {
    out vec4 FRAGMENT_COLOR;

    in VS_OUT
    {
        vec3 FragPos;
        vec2 TexCoords;
        vec3 Normal;

    } fs_in;

    float MAG(float lineWidth, float spacing, vec2 texCoords)
    {
        const vec2 coord = texCoords / spacing;
        const vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
        const float line = min(grid.x, grid.y);
        const float lineResult = lineWidth - min(line, lineWidth);
        return max(lineResult,0.2);
    }

    void main()
    {
        mat4 worldToPlane = inverse(u_PlaneTransform);
        vec4 localCameraPos = worldToPlane * vec4(ubo_ViewPos, 1.0);
        vec4 localFragPos = worldToPlane * vec4(fs_in.FragPos, 1.0);

        float height = abs(localCameraPos.y - localFragPos.y);

        vec2 texCoords = fs_in.TexCoords;

        float spacing1 = u_GridSize;           // 0.5m
        float spacing2 = u_GridSize * 4.0f;    // 2m
        float spacing3 = u_GridSize * 16.0f;   // 8m


        float grid1 = MAG(1.4f, spacing1, texCoords);
        float grid2 = MAG(1.5f, spacing2, texCoords);
        float grid3 = MAG(1.6f, spacing3, texCoords);


        float fadeFactor = 1.0f ;//- clamp(height / u_FadeDistance, 0.0f, 1.0f);


        float grid = 0.0f;
        if (height < 20.0f)
            grid = grid1 * 0.8f + grid2 * 0.2f;
        else if (height < 50.0f)
            grid = grid2 * 0.7f + grid3 * 0.3f;
        else
            grid = grid3 * fadeFactor;
        vec3 viewDir = normalize(fs_in.FragPos - ubo_ViewPos);
        vec3 planeNormal = normalize(mat3(u_PlaneTransform)[1]);
        float angleFactor = abs(dot(viewDir, planeNormal));
        float horizonFade = 0.1f + 0.9f * smoothstep(0.0f, 0.3f, angleFactor);





        vec2 reflectUV = gl_FragCoord.xy / textureSize(u_MirrorTex, 0);
        vec4 mirror = texture(u_MirrorTex, reflectUV);
        float shadow=texture(u_MirrorShadowTex,reflectUV).r;

        // The plane only exists where the mirror holds something. The mirror pass clears its
        // alpha to 0 and every object writes its own alpha where it draws, so that channel is
        // the coverage mask: the floor used to be painted as a whole plate, and the pixels with
        // no reflection in them read as a floor that differs from the background. Where there
        // is neither a reflection nor a contact shadow there is nothing to draw at all.
        if (mirror.a <= 0.0f) {
            if (shadow <= 0.02f) {
                discard;
            }
            // Only the contact shadow reaches this pixel: darken what is already behind it
            // instead of laying a floor colour over it (the pass blends with straight alpha).
            float shadowAlpha = clamp(shadow * 0.8f, 0.0f, 1.0f);
            FRAGMENT_COLOR = vec4(vec3(0.10f), shadowAlpha);
            return;
        }

        vec3 reflectColor = mirror.rgb;
        reflectColor *= mix(0.2, 1.0, angleFactor) ;
        vec3 finalColor = reflectColor;
        finalColor = mix(reflectColor, u_Color, 0.3);
        finalColor = mix(finalColor,vec3(0.10,0.10,0.10),(shadow*0.8));
        FRAGMENT_COLOR = vec4(finalColor, 1.0);

    }
}
