// The per-renderable block (filament's set 1): the matrices that change from one drawable to
// the next, uploaded before each draw. Generated into the shader as part of set 1; the runtime
// binds it at the engine-fixed slot 2.
layout (std140, binding = 2) uniform ObjectUBO
{
    mat4    ubo_Model;
    mat4    ubo_UserMatrix;
};
