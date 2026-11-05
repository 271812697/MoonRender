#pragma once
#include "Rendering/Features/ARenderFeature.h"

namespace Rendering::Data { class Material; }

namespace Rendering::Features
{
	/**
	* The ShapeDrawer handles the drawing of basic shapes
	*/
	class DebugShapeRenderFeature : public ARenderFeature
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		DebugShapeRenderFeature(
			Core::CompositeRenderer& p_renderer,
			EFeatureExecutionPolicy p_executionPolicy
		);

		/**
		* Destructor
		*/
		virtual ~DebugShapeRenderFeature();

		/**
		* Draw a line in world space
		* @param p_pso
		* @param p_start
		* @param p_end
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawLine(
			Rendering::Data::PipelineState p_pso,
			const Maths::FVector3& p_start,
			const Maths::FVector3& p_end,
			const Maths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a box in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_size
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawBox(
			Rendering::Data::PipelineState p_pso,
			const Maths::FVector3& p_position,
			const Maths::FQuaternion& p_rotation,
			const Maths::FVector3& p_size,
			const Maths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a sphere in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_radius
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawSphere(
			Rendering::Data::PipelineState p_pso,
			const Maths::FVector3& p_position,
			const Maths::FQuaternion& p_rotation,
			float p_radius,
			const Maths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a capsule in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_radius
		* @param p_height
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawCapsule(
			Rendering::Data::PipelineState p_pso,
			const Maths::FVector3& p_position,
			const Maths::FQuaternion& p_rotation,
			float p_radius,
			float p_height,
			const Maths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

	protected:
		virtual void OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor) override;

	private:
		Rendering::Data::Material* m_lineShader = nullptr;

		std::unique_ptr<Rendering::Resources::Mesh> m_lineMesh;
		std::unique_ptr<Rendering::Data::Material> m_lineMaterial;
	};
}