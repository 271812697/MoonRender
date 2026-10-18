#include <Core/Rendering/MaterialPreviewRenderer.h>

#include <cmath>
#include <cstring>

#include <glad/glad.h>

#include <Core/Rendering/EngineUBO.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Maths/FMatrix4.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/Material.h>
#include <Rendering/Geometry/Vertex.h>
#include <Rendering/Settings/TextureDesc.h>

namespace
{
	using namespace ::Rendering::Settings;

	/** A UV sphere of radius 1, unit normals and 0..1 texture coordinates: enough to read a
	 * material's shading, and a mesh no model file has to be loaded for. */
	void BuildSphere(std::vector<::Rendering::Geometry::Vertex>& p_vertices,
		std::vector<uint32_t>& p_indices,
		uint32_t p_rings = 24, uint32_t p_segments = 48)
	{
		const float pi = 3.14159265358979323846f;

		for (uint32_t ring = 0; ring <= p_rings; ++ring)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(p_rings);
			const float phi = v * pi;
			for (uint32_t segment = 0; segment <= p_segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(p_segments);
				const float theta = u * 2.0f * pi;

				const float x = std::sin(phi) * std::cos(theta);
				const float y = std::cos(phi);
				const float z = std::sin(phi) * std::sin(theta);

				::Rendering::Geometry::Vertex vertex{};
				vertex.position[0] = x;
				vertex.position[1] = y;
				vertex.position[2] = z;
				vertex.texCoords[0] = u;
				vertex.texCoords[1] = 1.0f - v;
				vertex.normals[0] = x;
				vertex.normals[1] = y;
				vertex.normals[2] = z;
				vertex.tangent[0] = 1.0f;
				vertex.bitangent[2] = 1.0f;
				p_vertices.push_back(vertex);
			}
		}

		const uint32_t stride = p_segments + 1;
		for (uint32_t ring = 0; ring < p_rings; ++ring)
		{
			for (uint32_t segment = 0; segment < p_segments; ++segment)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				p_indices.insert(p_indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
	}

	/** Allocates a 1x1 texture of p_type and uploads p_value as its only texel: the preview has
	 * no texture bound for a material's samplers, and the engine hands the same stand-ins to
	 * every draw (ABaseRenderer keeps one white 2D and one black cube). */
	void MakeSolidTexture(::Rendering::HAL::Texture& p_texture, uint8_t p_value)
	{
		const TextureDesc desc{
			.width = 1,
			.height = 1,
			.minFilter = ETextureFilteringMode::NEAREST,
			.magFilter = ETextureFilteringMode::NEAREST,
			.horizontalWrap = ETextureWrapMode::REPEAT,
			.verticalWrap = ETextureWrapMode::REPEAT,
			.internalFormat = EInternalFormat::RGBA8,
			.useMipMaps = false
		};
		p_texture.Allocate(desc);
		const uint8_t pixel[4] = { p_value, p_value, p_value, 255 };
		p_texture.Upload(pixel, EFormat::RGBA, EPixelDataType::UNSIGNED_BYTE);
	}
}

Core::Rendering::MaterialPreviewRenderer::MaterialPreviewRenderer(::Rendering::Context::Driver& p_driver)
	: m_driver(p_driver)
	, m_white(::Rendering::Settings::ETextureType::TEXTURE_2D)
	, m_blackCube(::Rendering::Settings::ETextureType::TEXTURE_CUBE)
{
	std::vector<::Rendering::Geometry::Vertex> vertices;
	std::vector<uint32_t> indices;
	BuildSphere(vertices, indices);
	m_sphere = std::make_unique<::Rendering::Resources::Mesh>(vertices, indices);

	m_engineBuffer.Allocate(sizeof(EngineUBO), ::Rendering::Settings::EAccessSpecifier::STREAM_DRAW);

	MakeSolidTexture(m_white, 255);
	MakeSolidTexture(m_blackCube, 0);
}

Core::Rendering::MaterialPreviewRenderer::~MaterialPreviewRenderer()
{
}

void Core::Rendering::MaterialPreviewRenderer::EnsureTarget(uint32_t p_width, uint32_t p_height)
{
	if (m_width == p_width && m_height == p_height && m_framebuffer.IsValid())
	{
		return;
	}

	m_width = p_width;
	m_height = p_height;

	const ::Rendering::Settings::TextureDesc desc{
		.width = p_width,
		.height = p_height,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA8,
		.useMipMaps = false
	};
	FramebufferUtil::SetupFramebuffer(m_framebuffer, desc, true, false, false);
	m_framebuffer.Validate();
}

bool Core::Rendering::MaterialPreviewRenderer::Render(
	::Rendering::Data::Material& p_material,
	uint32_t p_width, uint32_t p_height,
	std::vector<uint8_t>& p_pixels,
	std::string& p_error)
{
	if (!p_material.HasShader())
	{
		p_error = "this material has no shader";
		return false;
	}
	EnsureTarget(p_width, p_height);
	if (!m_framebuffer.IsValid())
	{
		p_error = "could not create the preview's framebuffer";
		return false;
	}

	// A camera on +Z looking at the origin, and the sphere as the model.
	EngineUBO ubo{};
	ubo.ubo_Model = Maths::FMatrix4::Identity;
	ubo.ubo_View = Maths::FMatrix4::Transpose(Maths::FMatrix4::Translation({ 0.0f, 0.0f, -3.2f }));
	const float aspect = p_height > 0 ? static_cast<float>(p_width) / static_cast<float>(p_height) : 1.0f;
	ubo.ubo_Projection = Maths::FMatrix4::Transpose(
		Maths::FMatrix4::CreatePerspective(45.0f, aspect, 0.1f, 100.0f));
	ubo.ubo_ViewPos = { 0.0f, 0.0f, 3.2f };
	ubo.ubo_CameraType = 1;
	ubo.ubo_Time = 0.0f;
	ubo.ubo_ScreenWidth = static_cast<int>(p_width);
	ubo.ubo_ScreenHeight = static_cast<int>(p_height);
	ubo.ubo_enableClip = 0;
	ubo.ubo_plane = { 0.0f, 0.0f, 0.0f, 0.0f };
	ubo.ubo_MirroPlaneMatrix = Maths::FMatrix4::Identity;
	ubo.ubo_UserMatrix = Maths::FMatrix4::Identity;

	m_engineBuffer.Upload(&ubo);
	m_engineBuffer.Bind(0);

	m_framebuffer.Bind();
	m_driver.SetViewport(0, 0, p_width, p_height);
	m_driver.Clear(true, true, false, { 0.09f, 0.09f, 0.11f, 1.0f });

	::Rendering::Data::PipelineState pso = m_driver.CreatePipelineState();
	pso.depthTest = true;
	pso.depthWriting = true;
	pso.culling = true;
	pso.cullFace = ::Rendering::Settings::ECullFace::BACK;
	pso.blending = false;
	pso.colorWriting.mask = 0xFF;

	p_material.Bind(&m_white, &m_blackCube, std::nullopt, std::nullopt, std::nullopt);
	p_material.UploadProperties(true, true, &m_white, &m_blackCube);
	m_driver.Draw(pso, *m_sphere, ::Rendering::Settings::EPrimitiveMode::TRIANGLES, 1);
	p_material.Unbind();

	p_pixels.resize(static_cast<size_t>(p_width) * p_height * 4);
	glReadPixels(0, 0, static_cast<GLsizei>(p_width), static_cast<GLsizei>(p_height),
		GL_RGBA, GL_UNSIGNED_BYTE, p_pixels.data());

	m_framebuffer.Unbind();
	m_engineBuffer.Unbind();
	return true;
}