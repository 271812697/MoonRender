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

// The per-renderable block (filament's set 1): the matrices that change from one drawable to
// the next, uploaded before each draw. GL binding point 3 - the per-material block owns 2 and
// the reflection block owns 1, so the three do not collide.
layout (std140, binding = 3) uniform ObjectUBO
{
    mat4    ubo_Model;
    mat4    ubo_UserMatrix;
};
