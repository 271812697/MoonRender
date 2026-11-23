// The per-view block (filament's set 0): what the camera and the frame decide, uploaded once
// per view by EngineBufferRenderFeature.
layout (std140, binding = 0) uniform EngineUBO
{
    mat4    ubo_View;
    mat4    ubo_Projection;
    vec3    ubo_ViewPos;
    int     ubo_CameraType; //0 orth,1 pers
    float   ubo_Time;
    int     ubo_ScreenWidth;
    int     ubo_ScreenHeight;
    int     ubo_enableClip; //1 enable 0 disable
    vec4    ubo_plane;
    mat4    ubo_MirroPlaneMatrix;
};
