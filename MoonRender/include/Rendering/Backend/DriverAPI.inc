#ifndef MOON_EXPAND_ARGS
/* Expands a parenthesised argument list (the `params` column below) into separate
 * arguments, so it can follow something else in a generated call. */
#define MOON_EXPAND_ARGS(...) __VA_ARGS__
#endif

// No include guard: this file is included more than once, with a different definition of each
// macro each time. It is Filament's DriverAPI.inc: the one place a driver method is declared, so
// the recording side, the command and the executor cannot drift apart.
//
//     DECL_DRIVER_COMMAND(name, params_decl, params)
//     DECL_DRIVER_API(name, params_decl, params)
//     DECL_DRIVER_API_RETURN(ret_type, name, params_decl, params)
//
//     name        the method's name, on every side
//     params_decl the typed parameter list
//     params      the same list, names only, for forwarding
//
// DECL_DRIVER_COMMAND is the original entry: it expands to Enqueue<name> on Driver (a method that
// only records), <name> on CommandStream, and the executor on OpenGLDriver.
//
// DECL_DRIVER_API and DECL_DRIVER_API_RETURN are the driver API proper (Filament's names for the
// same two cases): the method on Driver has its final name, forwards to the command stream, and
// the executor on OpenGLDriver is what does the work. The _RETURN form is for the calls that hand
// back something the recording side needs at once - a new handle - so its expansion reserves the
// handle first and then records the command that gives it its backend object
// (createBufferObjectS / createBufferObjectR).
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
DECL_DRIVER_COMMAND(SetClearColor, (float p_red, float p_green, float p_blue, float p_alpha), (p_red, p_green, p_blue, p_alpha))
DECL_DRIVER_COMMAND(Clear, (bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer), (p_colorBuffer, p_depthBuffer, p_stencilBuffer))
// ---- Textures: the driver API (Filament's createTexture / allocateTextureObject /
// update2DImage / generateMipmaps) ----------------------------------------------------------
//
// Same shape as the buffers below: the create hands back the handle the recording side needs at
// once, and every later call is one command carrying that handle and the pixels it uploads
// (Filament's PixelBufferDescriptor owns the bytes, so nothing depends on the caller's memory).
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture>,
	CreateTextureObject,
	(::Rendering::Settings::ETextureType p_type, std::string p_debugName),
	(p_type, std::move(p_debugName)))
// Gives the texture its storage (glTextureStorage2D), discarding what it held. A mutable texture
// takes its initial contents here; the pixels travel with the command.
DECL_DRIVER_API(AllocateTextureObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
		::Rendering::Settings::TextureDesc p_desc,
		::Rendering::Backend::PixelBufferDescriptor p_pixels),
	(p_texture, p_desc, std::move(p_pixels)))
// A sub-rectangle of storage that already exists (Filament's update2DImage).
DECL_DRIVER_API(UpdateTextureObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
		uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
		::Rendering::Backend::PixelBufferDescriptor p_pixels),
	(p_texture, p_x, p_y, p_width, p_height, std::move(p_pixels)))
// A whole image: the executor allocates the storage when the texture has none yet, then uploads.
DECL_DRIVER_API(UploadTextureObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
		::Rendering::Settings::TextureDesc p_desc,
		::Rendering::Backend::PixelBufferDescriptor p_pixels,
		bool p_generateMipmaps),
	(p_texture, p_desc, std::move(p_pixels), p_generateMipmaps))
DECL_DRIVER_API(GenerateTextureMipmapsObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture),
	(p_texture))
DECL_DRIVER_API(SetTextureBorderColorObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture, ::Maths::FVector4 p_color),
	(p_texture, p_color))

// ---- Render targets: Filament's createRenderTarget / bindFramebuffer / setAttachments ---------
//
// A target is a handle and its attachments are handles: the recording side decides what is
// attached and to which point, the executor does glNamedFramebufferTexture and glDrawBuffers.
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer>,
	CreateFramebufferObject,
	(std::string p_debugName),
	(std::move(p_debugName)))
// Binds the target, or the default framebuffer when the handle is null.
DECL_DRIVER_API(BindFramebufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer),
	(p_framebuffer))
DECL_DRIVER_API(AttachTextureObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
		::Rendering::Settings::EFramebufferAttachment p_attachment,
		uint32_t p_index, uint32_t p_level, bool p_hasLayer, uint32_t p_layer),
	(p_framebuffer, p_texture, p_attachment, p_index, p_level, p_hasLayer, p_layer))
DECL_DRIVER_API(AttachRenderbufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer> p_renderbuffer,
		::Rendering::Settings::EFramebufferAttachment p_attachment, uint32_t p_index),
	(p_framebuffer, p_renderbuffer, p_attachment, p_index))
// glNamedFramebufferDrawBuffers: which colour attachments the target writes (the gbuffer writes
// two, the sky capture switches the cube face it writes by selecting one).
DECL_DRIVER_API(SetFramebufferDrawBuffersObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		std::vector<uint32_t> p_indices),
	(p_framebuffer, std::move(p_indices)))
// glNamedFramebufferReadBuffer: which attachment a readback reads. Picking selects one colour
// attachment, reads it, then selects another (the id, then the world position).
DECL_DRIVER_API(SetFramebufferReadBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		bool p_hasIndex, uint32_t p_index),
	(p_framebuffer, p_hasIndex, p_index))

// The rest of what a target can be asked for: a blit between two of them, a clear of one
// attachment, and the two readbacks (synchronous into CPU memory, and asynchronous into a
// pixel-pack buffer whose bytes DownloadBuffer fetches later).
//
// The blits carry both sizes: the recording side keeps the attachment list, so it is the side that
// knows how big a target is right now (a resize re-specifies the attachments, and that is a command
// of its own - waiting for the executor to notice would size a blit from a stale copy).
DECL_DRIVER_API(CopyFramebufferColorObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
		uint32_t p_sourceIndex, uint16_t p_sourceWidth, uint16_t p_sourceHeight,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_target,
		uint32_t p_targetIndex, uint16_t p_targetWidth, uint16_t p_targetHeight),
	(p_source, p_sourceIndex, p_sourceWidth, p_sourceHeight,
		p_target, p_targetIndex, p_targetWidth, p_targetHeight))
DECL_DRIVER_API(CopyFramebufferDepthObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
		uint16_t p_sourceWidth, uint16_t p_sourceHeight,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_target,
		uint16_t p_targetWidth, uint16_t p_targetHeight),
	(p_source, p_sourceWidth, p_sourceHeight, p_target, p_targetWidth, p_targetHeight))
// glBlitNamedFramebuffer into the default framebuffer: the present blit, for the paths that draw
// into a target and hand the result to the window.
DECL_DRIVER_API(BlitFramebufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
		uint16_t p_sourceWidth, uint16_t p_sourceHeight,
		uint16_t p_targetWidth, uint16_t p_targetHeight),
	(p_source, p_sourceWidth, p_sourceHeight, p_targetWidth, p_targetHeight))
DECL_DRIVER_API(ClearFramebufferAttachmentObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		::Rendering::Settings::EFramebufferAttachment p_attachment, int32_t p_index),
	(p_framebuffer, p_attachment, p_index))
// glReadPixels into CPU memory. Synchronous on the caller's side - it records this, then waits -
// and the callback runs here, where the bytes are (a diagnostic that prints them).
DECL_DRIVER_API(ReadPixelsObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		std::shared_ptr<std::vector<uint8_t>> p_data,
		uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
		::Rendering::Settings::EPixelDataFormat p_format,
		::Rendering::Settings::EPixelDataType p_type,
		std::function<void()> p_onDone),
	(p_framebuffer, std::move(p_data), p_x, p_y, p_width, p_height, p_format, p_type, std::move(p_onDone)))
// glReadPixels into a pixel-pack buffer: asynchronous, the GPU writes it and an inserted fence says
// when DownloadBuffer may fetch it.
DECL_DRIVER_API(ReadPixelsToBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
		uint64_t p_offset,
		uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
		::Rendering::Settings::EPixelDataFormat p_format,
		::Rendering::Settings::EPixelDataType p_type),
	(p_framebuffer, p_buffer, p_offset, p_x, p_y, p_width, p_height, p_format, p_type))

// ---- Vertex arrays: Filament's createVertexArray / updateVertexArray ---------------------------
//
// A vertex array is a container the recording side assembles out of buffer handles: it names the
// two buffers and the layout, and the executor does the glVertexAttribPointer calls where the
// context is.
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray>,
	CreateVertexArrayObject,
	(std::string p_debugName),
	(std::move(p_debugName)))
DECL_DRIVER_API(SetVertexArrayObjectLayout,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray> p_array,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_vertexBuffer,
		::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_indexBuffer,
		std::shared_ptr<const std::vector<::Rendering::Backend::VertexLayoutAttribute>> p_attributes),
	(p_array, p_vertexBuffer, p_indexBuffer, std::move(p_attributes)))

// ---- Renderbuffers: Filament's createRenderbuffer / allocateRenderbufferStorage ----------------
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer>,
	CreateRenderbufferObject,
	(std::string p_debugName),
	(std::move(p_debugName)))
DECL_DRIVER_API(AllocateRenderbufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer> p_renderbuffer,
		uint16_t p_width, uint16_t p_height,
		::Rendering::Settings::EInternalFormat p_format,
		bool p_multiSample),
	(p_renderbuffer, p_width, p_height, p_format, p_multiSample))

// ---- Programs: Filament's createProgram / bindProgram -----------------------------------------
//
// The stages' sources travel with the create command: the recording side never compiles (that
// needs the context, which may be the executor's), it hands the sources over and gets the handle
// back - the link itself runs where the GL is.
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram>,
	CreateProgramObject,
	(std::shared_ptr<const std::vector<::Rendering::Backend::ShaderSourceSpec>> p_stages,
		std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> p_bindings),
	(std::move(p_stages), std::move(p_bindings)))
DECL_DRIVER_API(BindProgramObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> p_program),
	(p_program))
DECL_DRIVER_API(DeclareProgramBindingsObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> p_program,
		std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> p_bindings),
	(p_program, std::move(p_bindings)))

// ---- Buffer objects: the driver API (Filament's createBufferObject / allocateBufferObject /
// updateBufferObject / bindBuffer / unmapBuffer / insertFence) ------------------------------------
//
// The typed handle travels as a command argument: it is a four byte value, so the command that
// carries it says exactly which object the executor is to touch, and the executor is where it is
// resolved (the backend object - and its GL name - only exists on that side).

// A new handle: the recording side reserves it (the allocator is thread safe) and the executor
// makes the object in that slot. Filament's DECL_DRIVER_API_RETURN(Handle<HwBufferObject>,
// createBufferObject, ...): the caller needs the handle before the command has run.
DECL_DRIVER_API_RETURN(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>,
	CreateBufferObject,
	(::Rendering::Settings::EBufferType p_type),
	(p_type))
DECL_DRIVER_API(AllocateBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer, uint64_t p_size, ::Rendering::Settings::EAccessSpecifier p_usage),
	(p_buffer, p_size, p_usage))
// The bytes are owned by the descriptor the caller hands over (Filament's BufferDescriptor), so
// the upload never depends on the caller's memory still being there when the batch runs.
DECL_DRIVER_API(UpdateBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer, ::Rendering::Backend::BufferDescriptor p_data, uint64_t p_offset),
	(p_buffer, std::move(p_data), p_offset))
DECL_DRIVER_API(BindBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer, ::Rendering::Settings::EBufferType p_type, std::optional<uint32_t> p_index),
	(p_buffer, p_type, p_index))
// Binds nothing to a target: the recording side asks for it with no handle.
DECL_DRIVER_API(UnbindBufferObject,
	(::Rendering::Settings::EBufferType p_type),
	(p_type))
// A byte range at an indexed binding point: a uniform/storage block inside a shared buffer.
DECL_DRIVER_API(BindBufferObjectRange,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer, uint32_t p_index, uint64_t p_offset, uint64_t p_size),
	(p_buffer, p_index, p_offset, p_size))
// Which attachment a readback reads (glNamedFramebufferReadBuffer), and a readback into a
// pixel-pack buffer: the two recorded halves of an asynchronous readback (picking, HZB).
// The buffer's own GL state that the recording side used to touch directly - a mapped range, the
// fence that says the GPU finished writing it - and therefore the commands that do it where the
// context is. Named by handle, resolved by the executor (Filament's unmapBuffer/insertFence).
DECL_DRIVER_API(UnmapBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer),
	(p_buffer))
DECL_DRIVER_API(InsertBufferFence,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer),
	(p_buffer))
DECL_DRIVER_API(ClearBufferFence,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer),
	(p_buffer))
// Copy a buffer range into CPU memory, but only once the buffer's fence says the GPU is done
// writing it: the fetch half of the HZB readback (the GPU wrote the bytes, the CPU asks for them a
// frame or two later). This is the only buffer readback there is, and it never waits: p_onDone runs
// where the context is, with false when the fence is not signalled yet, and the caller asks again
// next frame (Filament's readPixels + getFenceStatus, folded into one command).
DECL_DRIVER_API(DownloadBufferObject,
	(::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
		uint64_t p_offset,
		uint64_t p_size,
		std::shared_ptr<std::vector<uint8_t>> p_data,
		std::function<void(bool)> p_onDone),
	(p_buffer, p_offset, p_size, std::move(p_data), std::move(p_onDone)))
// Attaching a texture its owner already has, and choosing which colour attachments are written:
// both are GL on the target object, so both belong to the side that owns the context.
DECL_DRIVER_COMMAND(BindDescriptor, (const ::Rendering::Backend::BindDescriptorRequest& p_request), (p_request))
// Filament's bindProgram: glUseProgram belongs to the side that owns the context, and the program
// is named by the id its create command produced.
// The same bind, named by the handle the create command produced: the recording side does not
// know the GL name yet when it records (the program is built on the executor), and passing 0 there
// left the previous program bound - one program for a whole pass of different materials.
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
