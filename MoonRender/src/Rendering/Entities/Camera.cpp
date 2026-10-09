#include <cmath>
#include <iostream>
#include "Rendering/Entities/Camera.h"
#include "Maths/FMatrix4.h"
Rendering::Entities::Camera::Camera(Tools::Utils::OptRef<Maths::FTransform> p_transform) :
	Entity{ p_transform },
	m_projectionMode(Settings::EProjectionMode::PERSPECTIVE),
	m_fov(60.0f),
	m_size(5.0f),
	m_near(0.1f),
	m_far(1200.f),
	m_clearColor(0.f, 0.f, 0.f),
	m_clearColorBuffer(true),
	m_clearDepthBuffer(true),
	m_clearStencilBuffer(true),
	m_frustumGeometryCulling(true),
	m_frustumLightCulling(true),
	m_frustum{}
{
}

void Rendering::Entities::Camera::CacheMatrices(uint16_t p_windowWidth, uint16_t p_windowHeight)
{
	CacheProjectionMatrix(p_windowWidth, p_windowHeight);
	CacheViewMatrix();
	CacheFrustum(m_viewMatrix, m_projectionMatrix);
}

void Rendering::Entities::Camera::CacheProjectionMatrix(uint16_t p_windowWidth, uint16_t p_windowHeight)
{
	m_projectionMatrix = CalculateProjectionMatrix(p_windowWidth, p_windowHeight);
}

void Rendering::Entities::Camera::CacheViewMatrix()
{
	m_preViewMatrix = m_viewMatrix;
	m_viewMatrix = CalculateViewMatrix();
}

void Rendering::Entities::Camera::CacheFrustum(const Maths::FMatrix4& p_view, const Maths::FMatrix4& p_projection)
{
	m_frustum.CalculateFrustum(p_projection * p_view);
}

const Maths::FVector3& Rendering::Entities::Camera::GetPosition() const
{
	return transform->GetWorldPosition();
}

const Maths::FQuaternion& Rendering::Entities::Camera::GetRotation() const
{
	return transform->GetWorldRotation();
}

float Rendering::Entities::Camera::GetFov() const
{
	return m_fov;
}

float Rendering::Entities::Camera::GetSize() const
{
    return m_size;
}

float Rendering::Entities::Camera::GetNear() const
{
	return m_near;
}

float Rendering::Entities::Camera::GetFar() const
{
	return m_far;
}

const Maths::FVector3 & Rendering::Entities::Camera::GetClearColor() const
{
	return m_clearColor;
}

bool Rendering::Entities::Camera::IsCameraViewMatrixChange() 
{
	return !Maths::FMatrix4::AreAlmostEquals(m_viewMatrix,m_preViewMatrix);
}

bool Rendering::Entities::Camera::GetClearColorBuffer() const
{
	return m_clearColorBuffer;
}

bool Rendering::Entities::Camera::GetClearDepthBuffer() const
{
	return m_clearDepthBuffer;
}

bool Rendering::Entities::Camera::GetClearStencilBuffer() const
{
	return m_clearStencilBuffer;
}

const Maths::FMatrix4& Rendering::Entities::Camera::GetProjectionMatrix() const
{
	return m_projectionMatrix;
}
Maths::FMatrix4 Rendering::Entities::Camera::GetViewProjectionMatrix() const
{
	return m_projectionMatrix*m_viewMatrix;
}
Maths::FVector2 Rendering::Entities::Camera::WordlToScreen(const Maths::FVector3& worldPos) const
{
	Maths::FMatrix4 viewProj=GetViewProjectionMatrix();
	Maths::FVector3 screenPos=viewProj.MulPoint(worldPos);
	return Maths::FVector2((screenPos.x+1.0)/2.0* m_windowWidth,(1.0-screenPos.y)/2.0*m_windowHeight);
}
Maths::FVector2 Rendering::Entities::Camera::WordlToViewPort(const Maths::FVector3& worldPos) const
{
	Maths::FMatrix4 viewProj = GetViewProjectionMatrix();
	Maths::FVector3 screenPos = viewProj.MulPoint(worldPos);
	return Maths::FVector2((screenPos.x + 1.0) / 2.0 * m_windowWidth, (1.0 + screenPos.y) / 2.0 * m_windowHeight);
}
const Maths::FMatrix4& Rendering::Entities::Camera::GetViewMatrix() const
{
	return m_viewMatrix;
}
Maths::FMatrix4 Rendering::Entities::Camera::GetViewPortMatrix()
{
	return Maths::FMatrix4::Scale(Maths::FMatrix4::Translate(GetViewProjectionMatrix(), { 1.0f,1.0f,0.0f }), { 0.5f * m_windowWidth,0.5f*m_windowHeight,1.0f});
}
const Maths::FTransform& Rendering::Entities::Camera::GetTransform()
{
	return transform;
}
const Rendering::Data::Frustum& Rendering::Entities::Camera::GetFrustum() const
{
	return m_frustum;
}

Tools::Utils::OptRef<const Rendering::Data::Frustum> Rendering::Entities::Camera::GetGeometryFrustum() const
{
	if (m_frustumGeometryCulling)
	{
		return m_frustum;
	}

	return std::nullopt;
}

Tools::Utils::OptRef<const Rendering::Data::Frustum> Rendering::Entities::Camera::GetLightFrustum() const
{
	if (m_frustumLightCulling)
	{
		return m_frustum;
	}

	return std::nullopt;
}

bool Rendering::Entities::Camera::HasFrustumGeometryCulling() const
{
	return m_frustumGeometryCulling;
}

bool Rendering::Entities::Camera::HasFrustumLightCulling() const
{
	return m_frustumLightCulling;
}

Rendering::Settings::EProjectionMode Rendering::Entities::Camera::GetProjectionMode() const
{
    return m_projectionMode;
}

void Rendering::Entities::Camera::SetPosition(const Maths::FVector3& p_position)
{
	transform->SetWorldPosition(p_position);
}

void Rendering::Entities::Camera::SetRotation(const Maths::FQuaternion& p_rotation)
{
	transform->SetWorldRotation(p_rotation);
}

void Rendering::Entities::Camera::SetFov(float p_value)
{
	m_fov = p_value;
}

void Rendering::Entities::Camera::SetSize(float p_value)
{
    m_size = p_value;
}

void Rendering::Entities::Camera::SetNear(float p_value)
{
	m_near = p_value;
}

void Rendering::Entities::Camera::SetFar(float p_value)
{
	m_far = p_value;
}

void Rendering::Entities::Camera::SetClearColor(const Maths::FVector3 & p_clearColor)
{
	m_clearColor = p_clearColor;
}

void Rendering::Entities::Camera::SetClearColorBuffer(bool p_value)
{
	m_clearColorBuffer = p_value;
}

void Rendering::Entities::Camera::SetClearDepthBuffer(bool p_value)
{
	m_clearDepthBuffer = p_value;
}

void Rendering::Entities::Camera::SetClearStencilBuffer(bool p_value)
{
	m_clearStencilBuffer = p_value;
}

void Rendering::Entities::Camera::SetFrustumGeometryCulling(bool p_enable)
{
	m_frustumGeometryCulling = p_enable;
}

void Rendering::Entities::Camera::SetFrustumLightCulling(bool p_enable)
{
	m_frustumLightCulling = p_enable;
}

void Rendering::Entities::Camera::SetProjectionMode(Rendering::Settings::EProjectionMode p_projectionMode)
{
    m_projectionMode = p_projectionMode;
}

void Rendering::Entities::Camera::ProjectionFitToSphere(Rendering::Geometry::BoundingSphere& sphere, const Maths::FVector3& dir)
{
	using namespace Maths;
	using namespace Rendering::Settings;
	if (m_projectionMode== EProjectionMode::ORTHOGRAPHIC) {
		m_size = sphere.radius;
		m_far = std::max(sphere.radius * 2.0f + m_near, m_far);
		//transform->LookAt(sphere.position-dir* m_size,sphere.position);

	}
	else if (m_projectionMode == EProjectionMode::PERSPECTIVE) {
		float eff = 3.14159265359f/180.0;
		float distance = sphere.radius / std::sin(eff*m_fov / 2.0f);
		// The far plane must cover the far side of the bounding sphere, so it
		// needs distance + radius (not just 2 * radius). Otherwise large models
		// get clipped as soon as the camera rotates around the fit center.
		m_far = std::max(distance + sphere.radius, m_far);
		//transform->LookAt(sphere.position - dir * distance, sphere.position);
	}
}

void Rendering::Entities::Camera::PersertiveZoom(float delta)
{
	// A step that is not a number would be added to the position and stay there: the
	// camera would be unusable for the rest of the session and the viewport empty with
	// nothing said about it. Callers scale this by the scene size, which is inf when
	// the scene bounds are the default (empty) box, so the check belongs here too.
	if (!std::isfinite(delta)) {
		return;
	}
	constexpr float kUnitsPerScroll = 1.0f;
	SetPosition(GetPosition() + transform->GetWorldForward() * kUnitsPerScroll * delta
	);

}

void Rendering::Entities::Camera::OrthZoom(float delta, int x, int y)
{
	float decreaseRatio = 0.1 * delta * m_size;
	m_size -= decreaseRatio;
	float xRatio = (x / static_cast<float>(m_windowWidth))-0.5f;
	float yRatio = (1.0f-y / static_cast<float>(m_windowHeight)) - 0.5f;
	
	auto offset = GetPosition() - transform->GetWorldRight() *2* decreaseRatio * m_ratio * xRatio +transform->GetWorldUp() *2* decreaseRatio * yRatio;
	SetPosition(offset);
	
}

void Rendering::Entities::Camera::HandleCameraPanning(const Maths::FVector2& p_mouseOffset, float p_speed, float p_sceneSize)
{
	if (m_projectionMode== Rendering::Settings::EProjectionMode::PERSPECTIVE) {
		// Scale the pan by the model size so dragging feels proportional to the
		// scene: a full-viewport drag moves roughly one scene length, instead of
		// a fixed pixel factor that feels too slow when the model is large.
		const float worldPerPixel = p_sceneSize > 0.0f
			? p_sceneSize / static_cast<float>(m_windowHeight)
			: 1.0f;
		const float dx = p_mouseOffset.x * worldPerPixel * m_ratio * p_speed;
		const float dy = p_mouseOffset.y * worldPerPixel * p_speed;
		SetPosition(GetPosition() + transform->GetWorldRight() * dx - transform->GetWorldUp() * dy);
	}
	else
	{
		// The window size is what turns the mouse offset into world units, and it is
		// zero until the view has been resized once.
		if (m_windowWidth <= 0 || m_windowHeight <= 0) {
			return;
		}
		float dx=2 * m_ratio * m_size * p_mouseOffset.x / m_windowWidth;
		float dy=2* m_size*p_mouseOffset.y / m_windowHeight;
		SetPosition(GetPosition() + transform->GetWorldRight() * dx - transform->GetWorldUp() * dy);
	}
}

::Rendering::Geometry::Ray Rendering::Entities::Camera::GetMouseRay(int x, int y)
{
	float u = (x / static_cast<float>(m_windowWidth))*2.0f - 1.0f;
	float v = (1.0f - y / static_cast<float>(m_windowHeight))*2.0f - 1.0f;
	const Maths::FVector3& up = transform->GetWorldUp();
	const Maths::FVector3& right = transform->GetWorldRight();
	const Maths::FVector3& forward = transform->GetWorldForward();
	const Maths::FVector3& position = transform->GetWorldPosition();
	if (m_projectionMode== Rendering::Settings::EProjectionMode::PERSPECTIVE) {
		float fovRad = m_fov/2.0f * (3.14159265359f / 180.0f);
		float height = v*m_near*std::tan(fovRad);
		float width = u*m_ratio * m_near * std::tan(fovRad);
		Maths::FVector3 dir= forward * m_near + height * up - width * right;
		return Geometry::Ray(position,dir);
	}
	const float height = v * m_size;
	const float width = u * m_size * m_ratio;
	Maths::FVector3 screenPos = position + height * up - width * right;
	return Geometry::Ray(screenPos, forward);

}

float Rendering::Entities::Camera::GetRatio()
{
	return m_ratio;
}

Maths::FMatrix4 Rendering::Entities::Camera::CalculateProjectionMatrix(uint16_t p_windowWidth, uint16_t p_windowHeight) 
{
    using namespace Maths;
    using namespace Rendering::Settings;
	m_windowWidth = p_windowWidth;
	m_windowHeight = p_windowHeight;
	m_ratio = p_windowWidth / static_cast<float>(p_windowHeight);

    switch (m_projectionMode)
    {
    case EProjectionMode::ORTHOGRAPHIC:
        return FMatrix4::CreateOrthographic(m_size, m_ratio, m_near, m_far);

    case EProjectionMode::PERSPECTIVE: 
        return FMatrix4::CreatePerspective(m_fov, m_ratio, m_near, m_far);

    default:
        return FMatrix4::Identity;
    }
}

Maths::FMatrix4 Rendering::Entities::Camera::CalculateViewMatrix() const
{
	const Maths::FVector3& position = transform->GetWorldPosition();
	const Maths::FQuaternion& rotation = transform->GetWorldRotation();
	const Maths::FVector3& up = transform->GetWorldUp();
	const Maths::FVector3& forward = transform->GetWorldForward();

	return Maths::FMatrix4::CreateView(
		position.x, position.y, position.z,
		position.x + forward.x, position.y + forward.y, position.z + forward.z,
		up.x, up.y, up.z
	);
}
