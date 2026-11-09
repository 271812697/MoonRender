#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "Interactive/Im3DType.h"

namespace Rendering::Data
{
	class Material;
}
namespace Rendering::Overlay
{
	class Im3DGLRenderer;
}

namespace MOON
{
	/** Which overlay pass an effect draws in.
	 *
	 * The channel is the same notion the built-in primitives already have: Sorted is the depth
	 * tested pass (drawSort), Overlay is drawn over everything without a depth test
	 * (drawUnsort), and Backdrop runs at the very start of the sorted pass - behind the axes and
	 * the geometry, which is where a background belongs. */
	enum class EImChannel
	{
		Backdrop,
		Sorted,
		Overlay
	};

	/** The pipeline state an effect wants. The channel supplies the default; a descriptor can
	 * override it (additive blending for a glow, no depth write for a translucent preview). */
	struct ImPipelineState
	{
		bool blend = true;
		bool depthTest = true;
		bool depthWrite = false;
	};

	/** What an effect looks like to the renderer: where and how its geometry is consumed.
	 * Everything else - the shader, the material, the geometry, the parameters - belongs to the
	 * concrete effect. */
	struct ImEffectDesc
	{
		/** GL_LINES / GL_TRIANGLES / GL_POINTS. Effects that expand their geometry do it in a
		 * geometry stage, so a thick dashed line is still GL_LINES here. */
		unsigned int topology = 0;
		EImChannel channel = EImChannel::Sorted;
		/** Extra floats stored after VertexFormat on every vertex, 0..8. The effect decides what
		 * they mean (line effects use them for the distance along the run and their pattern);
		 * the drawer only exposes them to the shader as attribute 4/5. */
		unsigned int extraFloats = 0;
		ImPipelineState state;
	};

	/** The frame state an effect needs in order to build and draw its geometry. */
	struct ImEffectContext
	{
		/** For building geometry: project a world point the way the overlay does. */
		Eigen::Matrix4f viewProj = Eigen::Matrix4f::Identity();
		/** Transposed, ready for a shader's uViewProjMatrix. */
		Eigen::Matrix4f viewProjT = Eigen::Matrix4f::Identity();
		float viewportWidth = 0.0f;
		float viewportHeight = 0.0f;
	};

	/** Base of every custom overlay effect.
	 *
	 * An effect owns everything about what it draws: its geometry, its material, its settings
	 * and any textures it needs. The renderer holds the effects and calls into them exactly
	 * twice - BeginFrame once per frame (new geometry starts here) and Draw once per frame for
	 * the effects of the channel being drawn. Effect-specific things therefore never appear in
	 * ImRenderer (compare the post-process stack: it knows AEffect, not Bloom). */
	class AImEffect
	{
	public:
		AImEffect(std::string p_id, const ImEffectDesc& p_desc);
		virtual ~AImEffect();

		AImEffect(const AImEffect&) = delete;
		AImEffect& operator=(const AImEffect&) = delete;

		const std::string& Id() const { return m_id; }
		const ImEffectDesc& Desc() const { return m_desc; }

		/** Bytes per vertex this effect consumes: VertexFormat plus its extra floats. */
		unsigned int StrideBytes() const
		{
			return static_cast<unsigned int>(sizeof(VertexFormat)) + m_desc.extraFloats * 4u;
		}
		unsigned int ExtraFloats() const { return m_desc.extraFloats; }

		/** Called by ImRenderer::NewFrame with the frame's camera: stores the context and lets the
		 * effect drop the previous frame's vertices. The overlay draws immediately, so geometry
		 * only lives for one frame; persistent resources (materials, pattern tables) stay. */
		void BeginFrame(const ImEffectContext& p_context)
		{
			m_context = p_context;
			m_drawCalls = 0;
			OnBeginFrame();
		}

		/** Called once per frame for the effects of the channel being drawn: the effect applies
		 * its material and issues its draw calls. Everything it draws was built by its own submit
		 * methods during the frame, using the context from BeginFrame. */
		virtual void Draw(::Rendering::Overlay::Im3DGLRenderer& p_gl) = 0;

		/** How many GL draws the last Draw() issued. Under the batching rule - material state is
		 * per frame, per-item variation lives in the vertices - this is 1 however many times the
		 * submit methods were called; the probe prints it. */
		unsigned int DrawCalls() const { return m_drawCalls; }

	protected:
		/** Drops the vertices of the previous frame; overridden, not called by the renderer. */
		virtual void OnBeginFrame() {}
		const ImEffectContext& Context() const { return m_context; }
		void AddDrawCall() { ++m_drawCalls; }

	private:
		std::string m_id;
		ImEffectDesc m_desc;
		ImEffectContext m_context;
		unsigned int m_drawCalls = 0;
	};

	/** Full path of an overlay shader, resolved the way the renderer resolves its own. */
	std::string ImShaderPath(const char* p_fileName);
}
