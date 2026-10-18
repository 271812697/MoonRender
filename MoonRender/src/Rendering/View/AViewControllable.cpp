#include <Rendering/View/AViewControllable.h>

const Maths::FVector3 kDefaultGridColor{ 1.0f, 1.0f, 1.0f };
const Maths::FVector3 kDefaultClearColor{ 0.0f, 0.0f, 0.0f };
const Maths::FVector3 kDefaultCameraPosition{ -10.0f, 3.0f, 10.0f };
const Maths::FQuaternion kDefaultCameraRotation({ 0.0f, 135.0f, 0.0f });

Editor::Panels::AViewControllable::AViewControllable(
	const std::string& p_title,
	::Rendering::Context::Driver& p_driver) :
	AView(p_title, p_driver),
	m_cameraController(*this, m_camera)
{
	ResetCameraTransform();
	ResetGridColor();
	ResetClearColor();
}

void Editor::Panels::AViewControllable::Update(float p_deltaTime)
{
	m_cameraController.HandleInputs(p_deltaTime);
	AView::Update(p_deltaTime);
}

void Editor::Panels::AViewControllable::InitFrame()
{
	// The grid is an editor feature (its descriptor and pass live there), so the concrete view
	// adds it after this call. What belongs here is only what a controllable view is: the camera
	// and the frame it sets up.
	m_camera.SetFrustumGeometryCulling(false);
	m_camera.SetFrustumLightCulling(false);
	AView::InitFrame();
}

void Editor::Panels::AViewControllable::ResetCameraTransform()
{
	m_camera.transform->SetWorldPosition(kDefaultCameraPosition);
	m_camera.transform->SetWorldRotation(kDefaultCameraRotation);
}

Editor::Core::CameraController& Editor::Panels::AViewControllable::GetCameraController()
{
	return m_cameraController;
}

Rendering::Entities::Camera* Editor::Panels::AViewControllable::GetCamera()
{
	return &m_camera;
}

const Maths::FVector3& Editor::Panels::AViewControllable::GetGridColor() const
{
	return m_gridColor;
}

void Editor::Panels::AViewControllable::SetGridColor(const Maths::FVector3& p_color)
{
	m_gridColor = p_color;
}

void Editor::Panels::AViewControllable::ResetGridColor()
{
	m_gridColor = kDefaultGridColor;
}

void Editor::Panels::AViewControllable::ResetClearColor()
{
	m_camera.SetClearColor(kDefaultClearColor);
}

void Editor::Panels::AViewControllable::setCameraMode(::Rendering::Settings::EProjectionMode mode)
{
	m_camera.SetProjectionMode(mode);
}
