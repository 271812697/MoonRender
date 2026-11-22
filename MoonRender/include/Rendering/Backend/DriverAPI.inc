// No include guard: this file is included more than once, with a different definition of
// DECL_DRIVER_COMMAND each time (Driver.h records, OpenGLDriver.h executes).
//
//     DECL_DRIVER_COMMAND(name, params_decl, params)
//
//     name        the executor on OpenGLDriver, without its trailing "R"
//     params_decl the typed parameter list
//     params      the same list, names only, for forwarding
//
// One entry becomes three things: the recording method on Driver (Enqueue<name>), the executor
// method on OpenGLDriver (<name>R), and the command that carries the arguments between them.
// This file is the only place the two halves are declared, so they cannot drift apart.
//
// M1 covered what the 2D overlay sets up around its frame. M2 adds the pipeline state: the whole
// struct travels as one command and the executor diffs it against what is applied, so the state
// machine lives on the render side and its cache cannot go stale against a second copy on the
// recording side. M2 also moves the draw submission itself - a draw is "material, state, bind
// array, draw" - which is the unit the executor replays. What is not here is GL that still runs
// synchronously on the side that owns the context: resource creation (drained at the call, because
// a handle has to be usable by the commands recorded after it) and the few passes that do their own
// GL (HZB, the path tracer - see docs/RenderThreadPlan.md §8.72).

DECL_DRIVER_COMMAND(Viewport, (int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height), (p_x, p_y, p_width, p_height))
DECL_DRIVER_COMMAND(Scissor, (int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height), (p_x, p_y, p_width, p_height))
DECL_DRIVER_COMMAND(Capability, (::Rendering::Settings::ERenderingCapability p_capability, bool p_enabled), (p_capability, p_enabled))
DECL_DRIVER_COMMAND(BlendingEquation, (::Rendering::Settings::EBlendingEquation p_equation), (p_equation))
DECL_DRIVER_COMMAND(BlendingFunction, (::Rendering::Settings::EBlendingFactor p_source, ::Rendering::Settings::EBlendingFactor p_destination), (p_source, p_destination))
DECL_DRIVER_COMMAND(PipelineState, (::Rendering::Data::PipelineState p_state), (p_state))
DECL_DRIVER_COMMAND(DrawMesh, (const ::Rendering::Backend::DrawMeshRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(BindFramebuffer, (const ::Rendering::Backend::BindFramebufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(SetClearColor, (float p_red, float p_green, float p_blue, float p_alpha), (p_red, p_green, p_blue, p_alpha))
DECL_DRIVER_COMMAND(Clear, (bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer), (p_colorBuffer, p_depthBuffer, p_stencilBuffer))
// Resource work, as a command like everything else: the request carries the descriptor and the
// pixels (a copy, so the recording side's buffer can be rewritten right away), and the executor
// allocates + uploads on the side that owns the context. Recorded before the draws that sample the
// texture, so the ring order is what makes the upload land first (Filament's update2DImage).
DECL_DRIVER_COMMAND(CreateTexture, (::Rendering::Backend::CreateTextureRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(UpdateTexture, (::Rendering::Backend::UpdateTextureRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(UploadTexture, (::Rendering::Backend::TextureUploadRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(SetTextureBorderColor, (const ::Rendering::Backend::SetTextureBorderColorRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(GenerateTextureMipmaps, (const ::Rendering::Backend::GenerateTextureMipmapsRequest& p_request), (p_request))
// Buffers and vertex arrays are container objects: they are not shared between contexts, so they
// too are born on the side that owns one (see Backend::Records in BackendResources.h).
DECL_DRIVER_COMMAND(AllocateBuffer, (::Rendering::Backend::AllocateBufferRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(BindBuffer, (const ::Rendering::Backend::BindBufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(CreateBuffer, (::Rendering::Backend::CreateBufferRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(CreateVertexArray, (uint32_t p_handleId), (p_handleId))
DECL_DRIVER_COMMAND(CreateFramebuffer, (uint32_t p_handleId), (p_handleId))
DECL_DRIVER_COMMAND(CreateRenderbuffer, (uint32_t p_handleId), (p_handleId))
DECL_DRIVER_COMMAND(ResizeFramebuffer, (const ::Rendering::Backend::ResizeFramebufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(CopyFramebufferColor, (const ::Rendering::Backend::CopyFramebufferColorRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(SetupFramebuffer, (const ::Rendering::Backend::SetupFramebufferRequest& p_request), (p_request))
// Attaching a texture its owner already has, and choosing which colour attachments are written:
// both are GL on the target object, so both belong to the side that owns the context.
DECL_DRIVER_COMMAND(AttachTexture, (const ::Rendering::Backend::AttachTextureRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(SetFramebufferDrawBuffers, (const ::Rendering::Backend::SetFramebufferDrawBuffersRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(AttachRenderbuffer, (const ::Rendering::Backend::AttachRenderbufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(AllocateRenderbuffer, (const ::Rendering::Backend::AllocateRenderbufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(CopyFramebufferDepth, (const ::Rendering::Backend::CopyFramebufferDepthRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(UpdateBuffer, (const ::Rendering::Backend::UpdateBufferRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(BindDescriptor, (const ::Rendering::Backend::BindDescriptorRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(SetVertexArrayLayout, (const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(ReadPixels, (const ::Rendering::Backend::ReadPixelsRequest& p_request), (p_request))
DECL_DRIVER_COMMAND(CreateProgram, (::Rendering::Backend::CreateProgramRequest p_request), (std::move(p_request)))
// Filament's bindProgram: glUseProgram belongs to the side that owns the context, and the program
// is named by the id its create command produced.
DECL_DRIVER_COMMAND(BindProgram, (uint32_t p_programId), (p_programId))
// The same bind, named by the handle the create command produced: the recording side does not
// know the GL name yet when it records (the program is built on the executor), and passing 0 there
// left the previous program bound - one program for a whole pass of different materials.
DECL_DRIVER_COMMAND(BindProgramHandle, (uint32_t p_programHandleId), (p_programHandleId))
DECL_DRIVER_COMMAND(DeclareProgramBindings, (::Rendering::Backend::DeclareProgramBindingsRequest p_request), (std::move(p_request)))
// The overlay's own geometry (Im3D): it is a vertex stream built per frame, uploaded into the
// overlay's buffer and drawn straight from it - no material pass, no mesh, just the array.
DECL_DRIVER_COMMAND(UploadOverlayVertices, (::Rendering::Backend::UploadOverlayVerticesRequest p_request), (std::move(p_request)))
DECL_DRIVER_COMMAND(DrawVertexArray, (const ::Rendering::Backend::DrawVertexArrayRequest& p_request), (p_request))
// Reading a texture back, for diagnostics: `glGetTextureImage` needs the object, so it runs here.
// Output goes to stderr, which is what a bisect wants to see.
DECL_DRIVER_COMMAND(PrintTextureBytes, (uint32_t p_textureHandleId, uint32_t p_byteCount, const char* p_label), (p_textureHandleId, p_byteCount, p_label))
// The 2D overlay's list: an indexed range, its texture and its clip rectangle - the last piece of
// what `Im2DRender::EndFrame` used to do inside the escape hatch's lambda. One command per entry of
// the list, in list order.
DECL_DRIVER_COMMAND(DrawIndexedVertexArray, (const ::Rendering::Backend::DrawIndexedVertexArrayRequest& p_request), (p_request))
// A loose mat4 uniform on a raw program (the overlay's projection): the material path is per-draw
// state and blocks, this is one program with one matrix.
DECL_DRIVER_COMMAND(SetProgramMatrix4, (const ::Rendering::Backend::ProgramMatrix4Request& p_request), (p_request))
