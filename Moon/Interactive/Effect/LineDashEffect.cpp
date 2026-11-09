#include "Interactive/Effect/LineDashEffect.h"

#include <glad/glad.h>

#include <cmath>
#include <cstring>

#include <Rendering/Overlay/Im3DGLRenderer.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>

namespace MOON
{
	namespace
	{
		/** A pattern table has a fixed number of runs per row; longer patterns are truncated. */
		constexpr int kMaxRunsPerPattern = 8;

		/** The style gallery, numbered like the Visio/Office dash picker. Lengths are screen
		 * pixels; 0 for an ink run is a dot (the pattern widens it). */
		struct LineStyleDef
		{
			const char* name;
			std::vector<float> runs;
		};

		const LineStyleDef kLineStyles[] = {
			{ "Solid",             {} },                                      // 01
			{ "Dash",              { 8.0f, 4.0f } },                          // 02
			{ "Dot",               { 0.0f, 4.0f } },                          // 03
			{ "Dash Dot",          { 8.0f, 4.0f, 0.0f, 4.0f } },              // 04
			{ "Dash Dot Dot",      { 8.0f, 4.0f, 0.0f, 4.0f, 0.0f, 4.0f } },  // 05
			{ "Long Dash",         { 16.0f, 4.0f } },                         // 06
			{ "Long Dash Dot",     { 16.0f, 4.0f, 0.0f, 4.0f } },             // 07
			{ "Long Dash Dot Dot", { 16.0f, 4.0f, 0.0f, 4.0f, 0.0f, 4.0f } }, // 08
			{ "Square Dot",        { 2.0f, 4.0f } },                          // 09
			{ "Sparse Dot",        { 0.0f, 10.0f } },                         // 10
		};
		static_assert(sizeof(kLineStyles) / sizeof(kLineStyles[0]) == kImLineStyleCount,
			"the line style table must cover every EImLineStyle");
	}

	const char* ImLineStyleName(EImLineStyle p_style)
	{
		const int index = std::clamp(static_cast<int>(p_style), 0, kImLineStyleCount - 1);
		return kLineStyles[index].name;
	}

	ImLinePattern ImLineStylePattern(EImLineStyle p_style)
	{
		const int index = std::clamp(static_cast<int>(p_style), 0, kImLineStyleCount - 1);
		return ImLinePattern(kLineStyles[index].runs);
	}

	ImLinePattern::ImLinePattern(std::vector<float> p_runs, float p_scale, float p_phase)
		: m_runs(std::move(p_runs)), m_scale(p_scale), m_phase(p_phase)
	{
	}

	void ImLinePattern::setRuns(std::vector<float> p_runs) { m_runs = std::move(p_runs); }
	void ImLinePattern::setScale(float p_scale) { m_scale = p_scale; }
	void ImLinePattern::setPhase(float p_phase) { m_phase = p_phase; }

	void ImLinePattern::resolve() const
	{
		m_resolved.clear();
		const float scale = m_scale > 0.0f ? m_scale : 1.0f;
		for (size_t i = 0; i < m_runs.size(); ++i)
		{
			float length = std::abs(m_runs[i]) * scale;
			const bool ink = (i % 2) == 0;
			// A dot is a zero-length ink run; the shader compares against run lengths, so it has
			// to be positive. Gaps keep a floor too, otherwise a pattern could repeat at the same
			// distance forever.
			if (ink && length < 0.5f)
			{
				length = 1.5f;
			}
			else if (!ink && length < 0.5f)
			{
				length = 0.5f;
			}
			m_resolved.push_back(length);
		}
		// The pattern must end on a gap: an odd run count would otherwise run two ink runs
		// together where the pattern wraps.
		if (m_resolved.size() % 2 != 0)
		{
			m_resolved.push_back(1.0f);
		}

		m_period = 0.0f;
		uint64_t hash = 1469598103934665603ull;
		for (float run : m_resolved)
		{
			m_period += run;
			uint32_t bits = 0;
			std::memcpy(&bits, &run, sizeof(bits));
			hash = (hash ^ bits) * 1099511628211ull;
		}
		m_hash = static_cast<size_t>(hash);
	}

	const std::vector<float>& ImLinePattern::ResolvedRuns() const
	{
		resolve();
		return m_resolved;
	}

	float ImLinePattern::Period() const
	{
		resolve();
		return m_period;
	}

	size_t ImLinePattern::Hash() const
	{
		resolve();
		return m_hash;
	}

	// ---- the effect -----------------------------------------------------------------------

	LineDashEffect::LineDashEffect()
		: ALineEffect("LineDash", ImEffectDesc{
			.topology = GL_LINES,
			// The construction geometry of a sketch is drawn without a depth test, so the scene
			// never hides what is being edited; this effect serves that pass.
			.channel = EImChannel::Overlay,
			// x = distance, y = pattern row, z = phase, w = period
			.extraFloats = 4 })
	{
		m_material.SetMaterial(Rendering::Resources::Loaders::ShaderLoader::Create(
			ImShaderPath("LineDash.shader")));
	}

	LineDashEffect::~LineDashEffect() = default;

	int LineDashEffect::registerPattern(const ImLinePattern& p_pattern)
	{
		const size_t hash = p_pattern.Hash();
		for (size_t i = 0; i < m_patterns.size(); ++i)
		{
			if (m_patterns[i].Hash() == hash)
			{
				return static_cast<int>(i);
			}
		}
		m_patterns.push_back(p_pattern);
		m_patternsDirty = true;
		return static_cast<int>(m_patterns.size() - 1);
	}

	bool LineDashEffect::ensurePatternTexture()
	{
		if (!m_patternsDirty && m_patternTexture != nullptr)
		{
			return true;
		}
		if (m_patterns.empty())
		{
			return false;
		}

		// One row per line type: R = run length in pixels, G = 1 for ink and 0 for a gap. A zero
		// length marks the end of a row's runs; the shader stops there.
		std::vector<float> data(static_cast<size_t>(kMaxRunsPerPattern) * m_patterns.size() * 4, 0.0f);
		for (size_t row = 0; row < m_patterns.size(); ++row)
		{
			const std::vector<float>& runs = m_patterns[row].ResolvedRuns();
			for (size_t i = 0; i < runs.size() && i < static_cast<size_t>(kMaxRunsPerPattern); ++i)
			{
				float* texel = &data[(row * kMaxRunsPerPattern + i) * 4];
				texel[0] = runs[i];
				texel[1] = (i % 2) == 0 ? 1.0f : 0.0f;
			}
		}

		if (m_patternTexture == nullptr)
		{
			m_patternTexture = std::make_unique<Rendering::HAL::Texture>(
				Rendering::Settings::ETextureType::TEXTURE_2D);
		}
		m_patternTexture->Allocate(Rendering::Settings::TextureDesc{
			.width = kMaxRunsPerPattern,
			.height = static_cast<uint32_t>(m_patterns.size()),
			.minFilter = Rendering::Settings::ETextureFilteringMode::NEAREST,
			.magFilter = Rendering::Settings::ETextureFilteringMode::NEAREST,
			.horizontalWrap = Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.verticalWrap = Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.internalFormat = Rendering::Settings::EInternalFormat::RGBA32F,
			.useMipMaps = false
		});
		m_patternTexture->Upload(data.data(), Rendering::Settings::EFormat::RGBA,
			Rendering::Settings::EPixelDataType::FLOAT);
		m_patternsDirty = false;
		return true;
	}

	void LineDashEffect::DrawPolyline(const std::vector<Eigen::Vector3f>& p_points, float p_size,
		Eigen::Vector4<uint8_t> p_color, const ImLinePattern& p_pattern, float p_phase)
	{
		if (p_points.size() < 2)
		{
			return;
		}
		const int row = registerPattern(p_pattern);
		const float phase = p_phase + p_pattern.Phase();
		const float period = p_pattern.Period();

		AppendPolyline(m_vertices, p_points, p_size, p_color,
			[&](size_t, float p_distance, float, float* p_extra)
			{
				p_extra[0] = p_distance;
				p_extra[1] = static_cast<float>(row);
				p_extra[2] = phase;
				p_extra[3] = period;
			});
	}

	void LineDashEffect::OnBeginFrame()
	{
		m_vertices.clear();
		// The pattern table itself is persistent: a line type is not per frame.
	}

	void LineDashEffect::Draw(::Rendering::Overlay::Im3DGLRenderer& p_gl)
	{
		if (m_vertices.empty() || !ensurePatternTexture())
		{
			return;
		}
		m_material.SetProperty("uPatternTex",
			static_cast<Rendering::HAL::TextureHandle*>(m_patternTexture.get()));
		m_material.SetProperty("uPatternRows", static_cast<float>(m_patterns.size()));
		DrawBuffer(p_gl, m_material, m_vertices);
	}
}
