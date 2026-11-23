#include <Rendering/Overlay/Im2DRender.h>

// The header only forward declares them; the implementation builds the list and the font.
#include <Rendering/Overlay/Im2D.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/Common/ResourceOps.h>
#include <Rendering/Backend/TextureUploadRequest.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Overlay/Im2DFont.h>

// The frame's state setup is recorded on the driver's command stream (see EndFrame).
#include <Core/Context.h>
#include <Core/RenderModuleHost.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/ERenderingCapability.h>

#include <glad/glad.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <Tools/Utils/PathParser.h>

namespace
{
	/** The program is ImGui's renderer program with one addition: the texture is always
	 * sampled. Solid geometry (the draw list's texture id 0) is drawn with a 1x1 white texture,
	 * so there is exactly one code path and no "is this command textured" branch. */
	constexpr const char* kVertexShader = R"(#version 450 core
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 Uv;
layout(location = 2) in vec4 Color;

uniform mat4 ProjMtx;

out vec2 FragUv;
out vec4 FragColor;

void main()
{
	FragUv = Uv;
	FragColor = Color;
	gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}
)";

	constexpr const char* kFragmentShader = R"(#version 450 core
in vec2 FragUv;
in vec4 FragColor;

uniform sampler2D Texture;

layout(location = 0) out vec4 OutColor;

void main()
{
	OutColor = FragColor * texture(Texture, FragUv);
}
)";

	GLuint CompileShader(GLenum p_type, const char* p_source)
	{
		const GLuint shader = glCreateShader(p_type);
		glShaderSource(shader, 1, &p_source, nullptr);
		glCompileShader(shader);
		GLint ok = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (ok == GL_FALSE)
		{
			char log[2048] = {};
			glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
			std::cerr << "[Im2D] shader compile failed: " << log << std::endl;
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	/** The font file the UI uses, plus fallbacks for the glyphs it does not carry (the editor's
	 * labels are Chinese and the primary face is a Latin one). A missing fallback is not fatal:
	 * the atlas simply has no glyph for those codepoints, and the next face is tried. */
	std::vector<std::string> FontPaths()
	{
		const std::string exeDir = Tools::Utils::PathParser::GetExeDirectory();
		return {
			exeDir + "/Moon/Data/Engine/Fonts/Ruda-Bold.ttf",
			"C:/Windows/Fonts/msyh.ttc",
			"C:/Windows/Fonts/segoeui.ttf",
		};
	}
}

class Rendering::Overlay::Im2DRender::Internal
{
public:
	explicit Internal(Im2DRender* p_owner) : m_owner(p_owner)
	{
	}

	~Internal()
	{
		// Everything left is freed through the backend records: the GL name is queued to the side
		// that owns the context (DeferredDeleter), the record goes now. The atlas and white textures
		// belong to their front-end objects, so dropping them does the same.
		m_fontAtlases.clear();
		m_whiteTexture.reset();
		::Rendering::HAL::ResourceOps::DestroyBuffer(m_vertexBuffer);
		::Rendering::HAL::ResourceOps::DestroyBuffer(m_indexBuffer);
		::Rendering::HAL::ResourceOps::DestroyVertexArray(m_vertexArray);
		::Rendering::Backend::Records::DestroyProgram(m_programHandle);
	}

	/** Creates the overlay's objects (by command) and loads the font.
	 *
	 * Nothing here touches GL: the program, the array and its two buffers are created where the
	 * context is, through ResourceOps and the create-program command, and each of them is drained
	 * before the first frame that names it. */
	void Init(::Rendering::Context::Driver& p_driver)
	{
		if (m_initialised)
		{
			return;
		}
		m_initialised = true;

		using namespace ::Rendering::Backend;
		using namespace ::Rendering::HAL;

		// The program: the two sources travel with the command and are compiled on that side.
		auto stages = std::make_shared<std::vector<ShaderSourceSpec>>();
		stages->push_back(ShaderSourceSpec{
			::Rendering::Settings::EShaderType::VERTEX,
			std::make_shared<const std::string>(kVertexShader) });
		stages->push_back(ShaderSourceSpec{
			::Rendering::Settings::EShaderType::FRAGMENT,
			std::make_shared<const std::string>(kFragmentShader) });

		m_programHandle = GetHandleAllocator().Allocate<HwProgram>();
		CreateProgramRequest programRequest;
		programRequest.programHandleId = m_programHandle.GetId();
		programRequest.stages = std::move(stages);
		p_driver.EnqueueCreateProgram(std::move(programRequest));
		m_programName = Records::ProgramName(m_programHandle);

		// The sampler samples unit 0, which is what a sampler uniform is initialised to: the list's
		// draws all bind their texture on GL_TEXTURE0 (see the DrawIndexedVertexArray command), so
		// no glUniform1i is needed here.

		// The array and its buffers: containers, so they are born where the context is and the
		// front end only holds the handles.
		m_vertexArray = GetHandleAllocator().Allocate<HwVertexArray>();
		m_vertexBuffer = GetHandleAllocator().Allocate<HwBufferObject>();
		m_indexBuffer = GetHandleAllocator().Allocate<HwBufferObject>();

		ResourceOps::EnsureBuffer(m_vertexBuffer);
		ResourceOps::EnsureBuffer(m_indexBuffer);
		ResourceOps::EnsureVertexArray(m_vertexArray);

		// Storage is allocated large enough for the first frames and grown when a list outgrows it
		// (see UploadGeometry): the per-frame path is a sub-data upload, not a reallocation.
		ResourceOps::AllocateBuffer(m_vertexBuffer, kInitialVertexCapacity, ::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);
		ResourceOps::AllocateBuffer(m_indexBuffer, kInitialIndexCapacity, ::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);


		// pos (2 floats), uv (2 floats), colour (4 normalised bytes) - the layout of Im2D::DrawVert,
		// which packs to 20 bytes with no padding.
		const ::Rendering::Settings::VertexAttribute attributes[] = {
			{ ::Rendering::Settings::EDataType::FLOAT, 2, false },
			{ ::Rendering::Settings::EDataType::FLOAT, 2, false },
			{ ::Rendering::Settings::EDataType::UNSIGNED_BYTE, 4, true },
		};
		ResourceOps::SetVertexArrayLayout(m_vertexArray, m_vertexBuffer, m_indexBuffer, attributes);

		// 1x1 white: what untextured geometry samples.
		const uint8_t white[4] = { 255, 255, 255, 255 };
		m_whiteTexture = std::make_unique<::Rendering::Resources::Texture>();
		CreateTextureObject(p_driver, *m_whiteTexture, "im2d white");
		TextureUploadRequest whiteRequest;
		whiteRequest.handleId = m_whiteTexture->GetHandle().GetId();
		whiteRequest.descriptor = ::Rendering::Settings::TextureDesc{
			.width = 1, .height = 1,
			.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
			.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
			.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA8,
			.useMipMaps = false
		};
		whiteRequest.format = ::Rendering::Settings::EFormat::RGBA;
		whiteRequest.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
		whiteRequest.pixels.assign(white, white + 4);

		m_font.Load(FontPaths(), kFontPixelSize);
		if (!m_font.IsLoaded())
		{
			std::cerr << "[Im2D] no font could be loaded; text will be empty" << std::endl;
		}
		// The atlases themselves are created on demand in NewFrame: the font makes a new one
		// whenever a text size appears for the first time.
	}

	/** Grows a buffer when a frame's list needs more room than the last allocation. */
	void ReserveVertexCapacity(uint64_t p_bytes)
	{
		if (p_bytes <= m_vertexCapacity)
		{
			return;
		}
		m_vertexCapacity = std::max<uint64_t>(p_bytes, m_vertexCapacity * 2);
		::Rendering::HAL::ResourceOps::AllocateBuffer(m_vertexBuffer, m_vertexCapacity,
			::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);
	}

	void ReserveIndexCapacity(uint64_t p_bytes)
	{
		if (p_bytes <= m_indexCapacity)
		{
			return;
		}
		m_indexCapacity = std::max<uint64_t>(p_bytes, m_indexCapacity * 2);
		::Rendering::HAL::ResourceOps::AllocateBuffer(m_indexBuffer, m_indexCapacity,
			::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);
	}

	/** Records the creation of a resource texture's object (Filament's createTexture) and waits for
	 * it: the upload that follows names the handle, so the object has to exist on the other side. */
	void CreateTextureObject(::Rendering::Context::Driver& p_driver, ::Rendering::Resources::Texture& p_texture,
		const char* p_debugName)
	{
		if (!p_texture.GetHandle())
		{
			return;
		}

		::Rendering::Backend::CreateTextureRequest request;
		request.handleId = p_texture.GetHandle().GetId();
		request.type = ::Rendering::Settings::ETextureType::TEXTURE_2D;
		request.debugName = p_debugName;
		p_driver.EnqueueCreateTexture(std::move(request));
	}
	/** Makes sure every size atlas the font has so far has a texture, and re-uploads the ones that
	 * gained glyphs. One 1 MB upload per touched size is cheap next to the per-glyph sub-uploads a
	 * dirty-rect scheme would need to track.
	 *
	 * The textures are created and filled by commands named by handle: the list is built on the
	 * recording side, so the GL objects have to be made where the context is (the draw block
	 * resolves the handle when it runs). */
	void SyncFontAtlases(::Rendering::Context::Driver& p_driver)
	{
		if (!m_font.IsLoaded())
		{
			return;
		}

		const auto descriptorOf = [this](int p_index)
		{
			return Rendering::Settings::TextureDesc{
				.width = static_cast<uint32_t>(m_font.GetAtlasWidth(p_index)),
				.height = static_cast<uint32_t>(m_font.GetAtlasHeight(p_index)),
				.minFilter = Rendering::Settings::ETextureFilteringMode::LINEAR,
				.magFilter = Rendering::Settings::ETextureFilteringMode::LINEAR,
				.horizontalWrap = Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.verticalWrap = Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.internalFormat = Rendering::Settings::EInternalFormat::RGBA8,
				.useMipMaps = false
			};
		};

		const int atlasCount = m_font.GetAtlasCount();
		while (static_cast<int>(m_fontAtlases.size()) < atlasCount)
		{
			const int index = static_cast<int>(m_fontAtlases.size());
			auto atlas = std::make_unique<Rendering::Resources::Texture>();
			CreateTextureObject(p_driver, *atlas, "im2d font atlas");
			m_fontAtlases.push_back(std::move(atlas));
			m_font.SetTextureId(index,
				Im2D::TextureIdFromHandle(m_fontAtlases.back()->GetHandle().GetId()));

			// The dirty flag is deliberately NOT consumed here: the loop below has to upload the
			// pixels, and a freshly made atlas always has some (it was created by baking a
			// glyph). Eating the flag here left the texture allocated but never filled.
		}

		for (int i = 0; i < atlasCount; ++i)
		{
			if (!m_font.ConsumeAtlasDirty(i))
			{
				continue;
			}

			const auto& pixels = m_font.GetAtlasPixels(i);
			if (pixels.empty())
			{
				continue;
			}

			::Rendering::Backend::TextureUploadRequest request;
			request.handleId = m_fontAtlases[static_cast<size_t>(i)]->GetHandle().GetId();
			request.descriptor = descriptorOf(i);
			request.format = Rendering::Settings::EFormat::RGBA;
			request.type = Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
			request.pixels = pixels;
			p_driver.UploadTexture(std::move(request));
		}

	}

	/** Pushes the frame's geometry into the two buffers (sub-data uploads into the storage that
	 * Reserve*Capacity keeps large enough). Bytes travel to the side that owns the context. */
	void UploadGeometry(const Im2D::DrawList& p_list)
	{
		const auto& vertices = p_list.GetVertices();
		const auto& indices = p_list.GetIndices();
		if (!vertices.empty())
		{
			const uint64_t bytes = vertices.size() * sizeof(Im2D::DrawVert);
			ReserveVertexCapacity(bytes);
			::Rendering::HAL::ResourceOps::UploadBuffer(m_vertexBuffer, vertices.data(), 0, bytes);
		}
		if (!indices.empty())
		{
			const uint64_t bytes = indices.size() * sizeof(uint32_t);
			ReserveIndexCapacity(bytes);
			::Rendering::HAL::ResourceOps::UploadBuffer(m_indexBuffer, indices.data(), 0, bytes);
		}
	}

	Im2DRender* m_owner = nullptr;
	Im2D::DrawList m_drawList;
	Im2D::Font m_font;
	::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> m_programHandle;
	uint32_t m_programName = 0;
	::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray> m_vertexArray;
	::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> m_vertexBuffer;
	::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> m_indexBuffer;
	std::unique_ptr<::Rendering::Resources::Texture> m_whiteTexture;
	std::vector<std::unique_ptr<Rendering::Resources::Texture>> m_fontAtlases;
	/** What the two buffers can hold; grown when a frame's list outgrows them. */
	uint64_t m_vertexCapacity = 0;
	uint64_t m_indexCapacity = 0;
	bool m_initialised = false;

	bool IsInitialised() const { return m_initialised; }
	static constexpr float kFontPixelSize = 24.0f;
	/** ~14k vertices / 64k indices: a frame of UI, with room to grow (see Reserve*Capacity). */
	static constexpr uint64_t kInitialVertexCapacity = 256 * 1024;
	static constexpr uint64_t kInitialIndexCapacity = 256 * 1024;
};

Rendering::Overlay::Im2DRender::Im2DRender()
	: mInternal(new Internal(this))
{
}

Rendering::Overlay::Im2DRender::~Im2DRender()
{
	delete mInternal;
}

Rendering::Overlay::Im2DRender& Rendering::Overlay::Im2DRender::Instance()
{
	static Im2DRender render2D;
	return render2D;
}

Rendering::Overlay::Im2D::DrawList& Rendering::Overlay::Im2DRender::GetDrawList()
{
	return mInternal->m_drawList;
}

Rendering::Overlay::Im2D::Font& Rendering::Overlay::Im2DRender::GetFont()
{
	return mInternal->m_font;
}

void Rendering::Overlay::Im2DRender::NewFrame()
{
	// The program and the vertex array belong to the module's context, so they are created on the
	// side that owns it: recorded once, then run (and waited for) through the driver. With no
	// driver - a headless test - the work happens inline as before.
	::Rendering::Context::Driver* driver = nullptr;
	if (auto* module = ::Core::RenderModuleHost::Instance())
	{
		if (module->GetContext() != nullptr)
		{
			driver = module->GetContext()->driver.get();
		}
	}

	if (!mInternal->IsInitialised() && driver != nullptr)
	{
		mInternal->Init(*driver);
	}

	mInternal->m_drawList.Reset();
	// Glyphs are baked on demand while the frame is recorded, so the textures are synced here
	// (a size that appears for the first time mid-frame is uploaded on the next one).
	if (driver != nullptr)
	{
		mInternal->SyncFontAtlases(*driver);
	}
}

void Rendering::Overlay::Im2DRender::EndFrame(
	::Rendering::Context::Driver& p_driver, uint16_t p_viewWidth, uint16_t p_viewHeight)
{
	// Closes the run the last primitive opened; without it the tail of the frame is dropped.
	mInternal->m_drawList.AddDrawCommand();

	const auto& commands = mInternal->m_drawList.GetCommands();
	if (commands.empty() || mInternal->m_programName == 0 || p_viewWidth == 0 || p_viewHeight == 0)
	{
		return;
	}
	if (mInternal->m_drawList.GetVertices().empty() || mInternal->m_drawList.GetIndices().empty())
	{
		return;
	}

	// Screen space (origin top-left, y down) to clip space.
	const float left = 0.0f;
	const float right = static_cast<float>(p_viewWidth);
	const float top = 0.0f;
	const float bottom = static_cast<float>(p_viewHeight);
	const std::array<float, 16> projection = {
		2.0f / (right - left), 0.0f,                  0.0f,  0.0f,
		0.0f,                  2.0f / (top - bottom), 0.0f,  0.0f,
		0.0f,                  0.0f,                 -1.0f,  0.0f,
		(right + left) / (left - right), (top + bottom) / (bottom - top), 0.0f, 1.0f,
	};

	// Everything below is a command: the state, the frame's geometry (a sub-data upload into the
	// overlay's buffers), the program with its projection, and one draw per entry of the list. The
	// GL calls they describe run where the context is, so this side needs neither a context nor a
	// state save: the driver's cache is what remembers what GL is in (see the Capability executor,
	// which updates it), and the next pass states what it wants.
	using namespace Rendering::Settings;
	p_driver.EnqueueCapability(ERenderingCapability::BLEND, true);
	p_driver.EnqueueBlendingEquation(EBlendingEquation::FUNC_ADD);
	p_driver.EnqueueBlendingFunction(EBlendingFactor::SRC_ALPHA, EBlendingFactor::ONE_MINUS_SRC_ALPHA);
	p_driver.EnqueueCapability(ERenderingCapability::CULL_FACE, false);
	p_driver.EnqueueCapability(ERenderingCapability::DEPTH_TEST, false);
	p_driver.EnqueueCapability(ERenderingCapability::SCISSOR_TEST, true);
	p_driver.EnqueueViewport(0, 0, p_viewWidth, p_viewHeight);

	mInternal->UploadGeometry(mInternal->m_drawList);

	p_driver.EnqueueBindProgram(mInternal->m_programName);
	::Rendering::Backend::ProgramMatrix4Request projectionRequest;
	projectionRequest.programName = mInternal->m_programName;
	projectionRequest.name = "ProjMtx";
	projectionRequest.value = projection;
	p_driver.EnqueueSetProgramMatrix4(projectionRequest);

	const uint32_t whiteTexture = mInternal->m_whiteTexture != nullptr
		? mInternal->m_whiteTexture->GetHandle().GetId()
		: 0;

	for (const ::Rendering::Overlay::Im2D::DrawCmd& cmd : commands)
	{
		if (cmd.elementCount == 0)
		{
			continue;
		}

		::Rendering::Backend::DrawIndexedVertexArrayRequest request;
		request.vertexArrayHandleId = mInternal->m_vertexArray.GetId();
		request.indexCount = cmd.elementCount;
		request.indexOffset = cmd.indexOffset;
		request.primitiveMode = GL_TRIANGLES;

		// The texture is named one of the two ways the request understands: a front-end handle
		// (resolved where the context is) or a raw name for an object that already lives in the
		// share group - the icon cache's textures are created by the window's context.
		if (::Rendering::Overlay::Im2D::IsHandleTexture(cmd.textureId))
		{
			request.textureHandleId = ::Rendering::Overlay::Im2D::HandleOfTexture(cmd.textureId);
		}
		else if (cmd.textureId != 0)
		{
			request.textureName = static_cast<uint32_t>(cmd.textureId);
		}
		else
		{
			request.textureHandleId = whiteTexture;
		}

		// The scissor rectangle is in pixels from the bottom-left; the draw list clips from the
		// top-left, hence the flip.
		const int32_t x0 = static_cast<int32_t>(cmd.clipRect.x);
		const int32_t y0 = static_cast<int32_t>(static_cast<float>(p_viewHeight) - cmd.clipRect.w);
		const int32_t x1 = static_cast<int32_t>(cmd.clipRect.z);
		const int32_t y1 = static_cast<int32_t>(static_cast<float>(p_viewHeight) - cmd.clipRect.y);
		request.scissor = true;
		request.scissorX = x0;
		request.scissorY = y0;
		request.scissorWidth = x1 - x0;
		request.scissorHeight = y1 - y0;

		p_driver.EnqueueDrawIndexedVertexArray(request);
	}
}