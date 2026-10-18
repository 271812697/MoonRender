#pragma once
#include <Rendering/View/AView.h>
#include <Rendering/View/CameraController.h>

namespace Editor {
	namespace Panels
	{
		class AViewControllable : public Editor::Panels::AView
		{
		public:
			AViewControllable(
				const std::string& p_title,
				::Rendering::Context::Driver& p_driver);
			virtual void Update(float p_deltaTime) override;
			virtual void InitFrame() override;
			virtual void ResetCameraTransform();
			Editor::Core::CameraController& GetCameraController();
			virtual ::Rendering::Entities::Camera* GetCamera();
			const Maths::FVector3& GetGridColor() const;
			void SetGridColor(const Maths::FVector3& p_color);
			void ResetGridColor();
			void ResetClearColor();
			void setCameraMode(::Rendering::Settings::EProjectionMode mode);
		protected:
			Maths::FVector3 m_gridColor;
			::Rendering::Entities::Camera m_camera;
			Editor::Core::CameraController m_cameraController;
		};
	}
}