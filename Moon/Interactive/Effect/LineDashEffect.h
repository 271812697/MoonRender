#pragma once
#include <memory>
#include <vector>

#include <Rendering/Data/Material.h>
#include <Rendering/HAL/Texture.h>

#include "Interactive/Effect/ALineEffect.h"

namespace MOON
{
	/** A line type: the dash pattern a CAD user already knows from DXF/ISO line type tables,
	 * as alternating ink and gap lengths.
	 *
	 * Lengths are in screen pixels (the distance the line effect measures along the line) and
	 * positional: even indices are ink, odd indices are gaps. A zero-length ink run is a dot,
	 * widened to 1.5 px so the pattern has no zero-width segment for the shader to divide by.
	 * Scale multiplies every run (the CAD "LTSCALE" role); phase offsets where the pattern
	 * starts. */
	class ImLinePattern
	{
	public:
		ImLinePattern() = default;
		explicit ImLinePattern(std::vector<float> p_runs, float p_scale = 1.0f, float p_phase = 0.0f);

		void setRuns(std::vector<float> p_runs);
		void setScale(float p_scale);
		void setPhase(float p_phase);

		const std::vector<float>& Runs() const { return m_runs; }
		float Scale() const { return m_scale; }
		float Phase() const { return m_phase; }

		/** The runs the shader samples: scale applied, dots widened. */
		const std::vector<float>& ResolvedRuns() const;
		float Period() const;
		size_t Hash() const;

	private:
		void resolve() const;

		std::vector<float> m_runs;
		float m_scale = 1.0f;
		float m_phase = 0.0f;
		mutable std::vector<float> m_resolved;
		mutable float m_period = 0.0f;
		mutable size_t m_hash = 0;
	};

	/** The line types a style picker offers, numbered like the Visio/Office dash gallery
	 * (01 solid, 02 dash, ... 10 sparse dot). Lengths are in screen pixels at 100%; a pattern
	 * scaled by the caller's style scale keeps its look at other line weights. */
	enum class EImLineStyle
	{
		Solid = 0,
		Dash,
		Dot,
		DashDot,
		DashDotDot,
		LongDash,
		LongDashDot,
		LongDashDotDot,
		SquareDot,
		SparseDot,

		Count
	};

	constexpr int kImLineStyleCount = static_cast<int>(EImLineStyle::Count);

	/** The display name of a style ("Solid", "Dash", ...). */
	const char* ImLineStyleName(EImLineStyle p_style);

	/** The dash pattern of a style. Solid is an empty run list; the effect draws it as a plain
	 * line through the same material. */
	ImLinePattern ImLineStylePattern(EImLineStyle p_style);

	/** A line drawn with a line type, evaluated in the fragment stage.
	 *
	 * All the dashed lines of a frame go into one vertex buffer and one draw call: the pattern
	 * is a table (one row per distinct line type) and each vertex carries which row it uses,
	 * its phase and the period, next to the distance along the line. The effect owns the
	 * vertices, the pattern table and its texture, and the material. */
	class LineDashEffect : public ALineEffect
	{
	public:
		LineDashEffect();
		~LineDashEffect() override;

		/** Submits a polyline with p_pattern. Vertices are built immediately from the frame
		 * context given to BeginFrame. */
		void DrawPolyline(const std::vector<Eigen::Vector3f>& p_points, float p_size,
			Eigen::Vector4<uint8_t> p_color, const ImLinePattern& p_pattern, float p_phase = 0.0f);

		void Draw(::Rendering::Overlay::Im3DGLRenderer& p_gl) override;

	protected:
		void OnBeginFrame() override;

	private:
		/** Row of p_pattern in the pattern table, appended if it is new. */
		int registerPattern(const ImLinePattern& p_pattern);
		/** Uploads the pattern table when it changed. */
		bool ensurePatternTexture();

		Rendering::Data::Material m_material;
		std::vector<uint8_t> m_vertices;

		/** One row per distinct line type; survives the frames (a line type is not per frame). */
		std::vector<ImLinePattern> m_patterns;
		bool m_patternsDirty = true;
		std::unique_ptr<Rendering::HAL::Texture> m_patternTexture;
	};
}
