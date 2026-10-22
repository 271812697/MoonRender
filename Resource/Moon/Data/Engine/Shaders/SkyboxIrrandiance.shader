// Skybox: diffuse irradiance. Convolves the sky cubemap into the tiny irradiance cube the IBL
// reads for ambient diffuse light.
//
// Written in the material form MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own
// their main() because the skeleton's fixed transform cannot express them: the vertex stage
// writes gl_Position straight from the cube position on the camera (there is no model matrix
// involved), and the fragment stage emits its own VS_OUT varyings and output declaration.
// Nothing is generated for these stages - the attributes, the VS_OUT block, the output and the
// mains below are the whole stage.

material {
    name         : SkyboxIrrandiance,
    shadingModel : unlit,
    blending     : opaque,
    // No culling: the cube is seen from the inside while a face is captured, and depth writing
    // stays on for that capture (see SkyboxRenderPass::computeSkyTexture).
    culling      : none,
    depthWrite   : true,

    parameters : [
        { type : samplerCube, name : SkyboxCube },
        { type : int,     name : u_CaptureAxis, default : 0 },
        { type : float,   name : u_CaptureSide, default : 1.0 },
        { type : float2,  name : u_CaptureViewport, default : vec2(1024.0) },
        { type : float,   name : intensity, default : 1.0 }
    ]
}

vertex {
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;

    out VS_OUT
    {
        vec3 dir;
    } vs_out;

    void main()
    {
        vs_out.dir = geo_Pos;
        gl_Position = ubo_Projection *ubo_View  * vec4(ubo_ViewPos +  geo_Pos, 1.0);
    }
}

fragment {
    in VS_OUT
    {
        vec3 dir;
    } fs_in;

    out vec4 FRAGMENT_COLOR;
    #define PI         3.14159265358979323
    #define INV_PI     0.31830988618379067
    #define INV_TWO_PI 0.15915494309189533
    void main()
    {

        vec2 captureP = vec2(gl_FragCoord.x / u_CaptureViewport.x * 2.0 - 1.0,
           1.0 - gl_FragCoord.y / u_CaptureViewport.y * 2.0);
       vec3 N;
       if (u_CaptureAxis == 0)      N = vec3(u_CaptureSide, captureP.y, u_CaptureSide * -captureP.x);
       else if (u_CaptureAxis == 1) N = vec3(captureP.x, u_CaptureSide, u_CaptureSide * -captureP.y);
       else                         N = vec3(u_CaptureSide * captureP.x, captureP.y, u_CaptureSide);
       N = normalize(N);

        vec3 irradiance = vec3(0.0);

        // tangent space calculation from origin point
        vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 right = normalize(cross(up, N));
        up         = normalize(cross(N, right));

        float sampleDelta = 0.025;
        float nrSamples = 0.0f;
        for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
        {
            for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
            {
                // spherical to cartesian (in tangent space)
                vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
                // tangent space to world
                vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
                //irradiance += textureLod(SkyboxCube, sampleVec,0).rgb * cos(theta) * sin(theta);
                irradiance += texture(SkyboxCube, sampleVec).rgb * cos(theta) * sin(theta);
                nrSamples++;
            }
        }
        irradiance = PI * irradiance * (1.0 / float(nrSamples));

        FRAGMENT_COLOR = vec4(irradiance * intensity, 1.0);

    }
}
