#pragma once
#include <Maths/FVector3.h>
#include <Maths/FMatrix4.h>
#include <Maths/FTransform.h>

#include <Rendering/Data/FrameDescriptor.h>
#include <Rendering/Entities/Camera.h>
#include <Rendering/Entities/Entity.h>
#include <Rendering/Data/Framebuffer.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Settings/ELightType.h>

namespace Rendering::Entities
{
	/**
	* Data structure that can represent any type of light
	*/
	struct Light : public Entity
	{
		Maths::FVector3 color{ 1.f, 1.f, 1.f };
		float intensity = 1.f;
		float constant = 0.0f;
		float linear = 0.0f;
		float quadratic = 1.0f;
		float cutoff = 12.f;
		float outerCutoff = 15.f;
		Settings::ELightType type = Settings::ELightType::POINT;

		bool castShadows = false;
		float shadowAreaSize = 50.0f;
		bool shadowFollowCamera = true;
		int16_t shadowMapResolution = 8192;

		std::unique_ptr<Rendering::Data::Framebuffer> shadowBuffer;
		std::optional<Rendering::Entities::Camera> shadowCamera;
		std::optional<Maths::FMatrix4> lightSpaceMatrix;

		/**
		* Generate and cache light space matrix for the light
		*/
		void PrepareForShadowRendering(const Rendering::Data::FrameDescriptor& p_frameDescriptor);

		/**
		* Returns true if the light is setup for shadow rendering
		*/
		bool IsSetupForShadowRendering() const;

		/**
		* Generate the light matrix, ready to send to the GPU
		*/
		Maths::FMatrix4 GenerateMatrix() const;

		/**
		* Calculate the light effect range from the quadratic falloff equation
		*/
		float CalculateEffectRange() const;
	};
}
