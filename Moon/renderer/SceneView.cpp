#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/SceneSystem/Intersection.h>
#include "DebugSceneRenderer.h"
#include "PickingRenderPass.h"
#include "Core/Global/ServiceLocator.h"
#include "SceneView.h"
#include "Settings/DebugSetting.h"
#include "renderer/GizmoRenderPass.h"
#include "Interactive/Widgets/ClipPlane.h"
#include "core/component/CTopoShape.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <QMouseEvent>
#include <tracy/Tracy.hpp>

static Maths::FVector3 GetSpherePosition(float a, float b, float radius) {
	float elevation = a / 180.0 * 3.14159265;
	float azimuth = b / 180.0 * 3.14159265;
	return Maths::FVector3(cos(elevation) * sin(azimuth), sin(elevation), cos(elevation) * cos(azimuth)) * radius;
}
namespace
{
	/** Orientation that looks along p_dir with the world up, falling back to +X
	 * when looking straight up or down, where "up" would be degenerate. This is
	 * how a direction based fit (a click on a view cube face) gets its roll. */
	Maths::FQuaternion PoseForDirection(const Maths::FVector3& p_dir)
	{
		const float pi = 3.14159265359f;
		const float angle = Maths::FVector3::AngleBetween(p_dir, { 0, 1, 0 });
		const Maths::FVector3 up =
			(angle < FLT_EPSILON || std::abs(angle - pi) < FLT_EPSILON) ?
			Maths::FVector3(1, 0, 0) : Maths::FVector3(0, 1, 0);
		return Maths::FQuaternion::LookAt(p_dir, up);
	}

	Tools::Utils::OptRef<Core::ECS::Actor> GetActorFromPickingResult(
		Editor::Rendering::PickingRenderPass::PickingResult p_result
	)
	{
		if (p_result)
		{
			if (const auto actor = std::get_if<Tools::Utils::OptRef<Core::ECS::Actor>>(&p_result.value()))
			{
				return *actor;
			}
		}

		return std::nullopt;
	}
}

Editor::Panels::SceneView::SceneView
(
	const std::string& p_title)
	: AViewControllable(p_title),
	m_sceneManager(::Core::Global::ServiceLocator::Get<Editor::Core::Context>().sceneManager)
{
	RegService(Editor::Panels::SceneView, *this);
	m_renderer = std::make_unique<Editor::Rendering::DebugSceneRenderer>(*::Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver);
	m_camera.SetFar(5000.0f);
	m_fallbackMaterial.SetShader(::Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Unlit.ovfx"]);
	m_fallbackMaterial.SetProperty("u_Diffuse", Maths::FVector4{ 1.f, 0.f, 1.f, 1.0f });
	m_fallbackMaterial.SetProperty("u_DiffuseMap", static_cast<::Rendering::Resources::Texture*>(nullptr));
	::Core::ECS::Actor::DestroyedEvent += [this](const ::Core::ECS::Actor& actor)
		{
			if (m_highlightedActor.has_value() && m_highlightedActor.value().GetID() == actor.GetID())
			{
				m_highlightedActor.reset();
			}
		};
	
}

void Editor::Panels::SceneView::Update(float p_deltaTime)
{
	AViewControllable::Update(p_deltaTime);
	GetScene()->Update(p_deltaTime);
	auto headLight = GetScene()->FindActorByName("HeadLight");
	if (!headLight) {
		return;
	}
	headLight->transform.SetWorldPosition(m_camera.GetPosition());
	if (IsSelectActor()) {
		auto ac = GetSelectedActor();
		auto name=ac->GetName();
		if (name== "PointLight1"|| name == "PointLight2"|| name == "PointLight3"|| name == "PointLight4") {
			return;
		}
		float pi = 3.14159265359f;
		auto target = ac->transform.GetWorldPosition();
		auto cp = m_camera.GetPosition();
		float radius = Maths::FVector3::Length(target - cp) ;
		Maths::FMatrix4 transMat = Maths::FMatrix4::Translation(target - cp);
		Maths::FVector3 forward = Maths::FVector3::Normalize(cp - target);
		float angle = Maths::FVector3::AngleBetween(forward, { 0,1,0 });
		Maths::FVector3 worldUp = (angle < FLT_EPSILON || abs(angle - pi) < FLT_EPSILON) ? Maths::FVector3(1, 0, 0) : Maths::FVector3(0, 1, 0);
		Maths::FMatrix4 view = Maths::FMatrix4::CreateCameraView(cp, target, worldUp);
		view = Maths::FMatrix4::Inverse(view);

		Maths::FMatrix4 mat = transMat * view;

		auto p1 = Maths::FMatrix4::MulPoint(mat, GetSpherePosition(50, 10, radius));
		auto p2 = Maths::FMatrix4::MulPoint(mat, GetSpherePosition(-75, 10, radius));
		auto p3 = Maths::FMatrix4::MulPoint(mat, GetSpherePosition(0, 110, radius));
		auto p4 = Maths::FMatrix4::MulPoint(mat, GetSpherePosition(0, -110, radius));
		GetScene()->FindActorByName("PointLight1")->transform.SetWorldPosition(p1);
		GetScene()->FindActorByName("PointLight2")->transform.SetWorldPosition(p2);
		GetScene()->FindActorByName("PointLight3")->transform.SetWorldPosition(p3);
		GetScene()->FindActorByName("PointLight4")->transform.SetWorldPosition(p4);
	}
}

void Editor::Panels::SceneView::InitFrame()
{
	
	AViewControllable::InitFrame();

	Tools::Utils::OptRef<::Core::ECS::Actor> selectedActor;

	if (IsSelectActor()) {
		selectedActor = GetSelectedActor();
	}

	m_renderer->AddDescriptor<Rendering::DebugSceneRenderer::DebugSceneDescriptor>({
		m_currentOperation,
		m_highlightedActor,
		selectedActor,
		m_highlightedGizmoDirection
		});
	// Enable picking pass only when the scene view is hered, not picking, and not operating the camera
	auto& pickingPass = m_renderer->GetPass<Editor::Rendering::PickingRenderPass>("Picking");
	pickingPass.SetEnabled(
		!m_gizmoOperations.IsPicking() &&
		!m_cameraController.IsOperating()
	);
}

Core::SceneSystem::Scene* Editor::Panels::SceneView::GetScene()
{
	return m_sceneManager.GetCurrentScene();
}

void Editor::Panels::SceneView::FitToSelectedActor(const Maths::FVector3& dir)
{
	::Rendering::Geometry::BoundingSphere sphere;
	if (!GetSelectionSphere(sphere))
	{
		return;
	}

	// Look from the requested direction with the world up, so a direction based
	// fit always comes out level (see PoseForDirection below).
	ApplyFitPose(sphere, dir, PoseForDirection(dir));
}

void Editor::Panels::SceneView::LookAt(const Maths::FVector3& pivot, const Maths::FVector3& dir, float radius)
{
	float pi = 3.14159265359f;
	Maths::FVector3 forward = dir;
	float angle = Maths::FVector3::AngleBetween(forward, { 0,1,0 });
	Maths::FVector3 up = (angle < FLT_EPSILON || abs(angle - pi) < FLT_EPSILON) ? Maths::FVector3(1, 0, 0) : Maths::FVector3(0, 1, 0);
	Maths::FQuaternion quat = Maths::FQuaternion::LookAt(forward, up);

	if (m_camera.GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
		this->GetCameraController().MoveToPose(pivot - dir * radius, quat);
	}
	else
	{
		this->GetCameraController().MoveToPose(pivot - dir * radius, quat);
	}
}

Maths::FVector2 Editor::Panels::SceneView::worldToScreen(const Maths::FVector3& worldPos)
{
	return this->GetCamera()->WordlToScreen(worldPos);
}


void Editor::Panels::SceneView::FitToScene(const Maths::FVector3& dir)
{
	::Rendering::Geometry::BoundingSphere sphere;
	if (!GetSceneSphere(sphere))
	{
		return;
	}

	ApplyFitPose(sphere, dir, PoseForDirection(dir));
}

bool Editor::Panels::SceneView::GetSelectionSphere(
	::Rendering::Geometry::BoundingSphere& p_outSphere)
{
	using namespace ::Core::ECS::Components;

	if (!IsSelectActor())
	{
		return false;
	}
	auto* actor = GetSelectedActor();
	if (actor == nullptr)
	{
		return false;
	}
	auto* modelRenderer = actor->GetComponent<CModelRenderer>();
	if (modelRenderer == nullptr)
	{
		return false;
	}
	auto model = modelRenderer->GetModel();
	if (model == nullptr)
	{
		return false;
	}
	auto* transform = actor->GetComponent<CTransform>();
	if (transform == nullptr)
	{
		return false;
	}

	p_outSphere = model->GetBoundingSphere();
	p_outSphere.position = Maths::FMatrix4::MulPoint(
		transform->GetWorldMatrix(),
		p_outSphere.position);
	Maths::FVector3 worldScale = transform->GetWorldScale();
	p_outSphere.radius *= worldScale.Max();
	return p_outSphere.radius > 0.0f;
}

bool Editor::Panels::SceneView::GetSceneSphere(
	::Rendering::Geometry::BoundingSphere& p_outSphere)
{
	using namespace ::Core::ECS::Components;

	// Merge every active model's bounds, transformed to world space, so the fit
	// covers the whole scene instead of the models' local spheres.
	bool hasBounds = false;
	for (auto* modelRenderer : GetScene()->GetFastAccessComponents().modelRenderers)
	{
		if (modelRenderer == nullptr) continue;

		auto& owner = modelRenderer->owner;
		if (!owner.IsActive()) continue;

		auto model = modelRenderer->GetModel();
		if (model == nullptr) continue;

		auto modelSphere = model->GetBoundingSphere();
		if (modelSphere.radius <= 0.0f) continue;

		const auto transform = owner.GetComponent<CTransform>();
		if (transform == nullptr) continue;

		modelSphere.position = Maths::FMatrix4::MulPoint(
			transform->GetWorldMatrix(),
			modelSphere.position
		);
		Maths::FVector3 worldScale = transform->GetWorldScale();
		modelSphere.radius *= worldScale.Max();

		if (!hasBounds)
		{
			p_outSphere = modelSphere;
			hasBounds = true;
		}
		else
		{
			p_outSphere.merge(modelSphere);
		}
	}
	return hasBounds;
}

bool Editor::Panels::SceneView::GetFocusSphere(
	::Rendering::Geometry::BoundingSphere& p_outSphere)
{
	// The selection when there is one, the whole scene otherwise: the same choice
	// the view cube faces make.
	return GetSelectionSphere(p_outSphere) || GetSceneSphere(p_outSphere);
}

void Editor::Panels::SceneView::ApplyFitPose(
	::Rendering::Geometry::BoundingSphere& p_sphere,
	const Maths::FVector3& p_forward,
	const Maths::FQuaternion& p_rotation)
{
	// Adjust the projection first, then place the camera on the fit axis at a
	// distance that contains the bounding sphere. The orientation is the one the
	// caller asked for, so its forward is p_forward and the camera looks straight
	// at the center of the sphere.
	m_camera.ProjectionFitToSphere(p_sphere, p_forward);

	const float pi = 3.14159265359f;
	const float eff = pi / 180.0f;
	if (m_camera.GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC)
	{
		GetCameraController().MoveToPose(
			p_sphere.position - p_forward * p_sphere.radius,
			p_rotation);
	}
	else
	{
		const float distance = p_sphere.radius / std::sin(eff * m_camera.GetFov() / 2.0f);
		GetCameraController().MoveToPose(
			p_sphere.position - p_forward * distance,
			p_rotation);
	}
}

void Editor::Panels::SceneView::FitToFocusWithRotation(const Maths::FQuaternion& p_rotation)
{
	::Rendering::Geometry::BoundingSphere sphere;
	if (!GetFocusSphere(sphere))
	{
		return;
	}
	ApplyFitPose(sphere, p_rotation * Maths::FVector3::Forward, p_rotation);
}

void Editor::Panels::SceneView::BuildBvh()
{
	auto& scene = *GetScene();

	//scene.BuildBVH();
}

void Editor::Panels::SceneView::SetGizmoOperation(Editor::Core::EGizmoOperation p_operation)
{
	m_currentOperation = p_operation;
}

Editor::Core::EGizmoOperation Editor::Panels::SceneView::GetGizmoOperation() const
{
	return m_currentOperation;
}

void Editor::Panels::SceneView::ReceiveEvent(QEvent* e)
{
	if (e == nullptr)
		return;
	input.ReceiveEvent(e);
	const QEvent::Type t = e->type();
    if (t == QEvent::MouseButtonPress) {
		QMouseEvent* e2 = static_cast<QMouseEvent*>(e);
		if(e2->button()== Qt::RightButton)
		{ 
			MOON::EventWidget* gizmoWidget=GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").getGizmoWidget("ClipPlane");
			
			bool clipFlag = gizmoWidget->isActived();
			if (clipFlag) {
				MOON::ClipPlane* clipPlaneWidget = dynamic_cast<MOON::ClipPlane*>(gizmoWidget);
				Maths::FVector4 plane=clipPlaneWidget->getClipPlane();
				MouseClipHit(m_roaterCenter,plane);
			}
			else
			{
				MouseHit(m_roaterCenter);
			}

		}
	}
	if (!m_cameraController.IsRightMousePressed()) {
		if (t == QEvent::KeyPress) {
			QKeyEvent* e2 = static_cast<QKeyEvent*>(e);
			Qt::Key key = static_cast<Qt::Key>(e2->key());
			if (key == Qt::Key_W) {
				m_currentOperation = Editor::Core::EGizmoOperation::TRANSLATE;
			}
			else if (key == Qt::Key_E) {
				m_currentOperation = Editor::Core::EGizmoOperation::ROTATE;
			}
			else if (key == Qt::Key_R) {
				m_currentOperation = Editor::Core::EGizmoOperation::SCALE;
			}
		}
	}
}

bool Editor::Panels::SceneView::MouseHit(Maths::FVector3& out)
{
	auto[x,y]=input.GetMousePosition();
	auto ray = GetCamera()->GetMouseRay(x, y);
	::Core::SceneSystem::HitRes res;
	bool flag=MOON::DebugSettings::instance().getOrDefault<bool>("BvhRayHit",false);
	if (!flag) { 
		if (GetScene()->RayIteratorHit(ray, res)){
			out=res.hitPoint;
			return true;
		}
		return false;
	}
	if (GetScene()->RayHit(ray, res)) {
		out = res.hitPoint;
		return true;
	}
	return false;
}

bool Editor::Panels::SceneView::MouseClipHit(Maths::FVector3& out, const Maths::FVector4& clipPlane)
{
	auto [x, y] = input.GetMousePosition();
	auto ray = GetCamera()->GetMouseRay(x, y);
	::Core::SceneSystem::HitRes res;
	
	if (GetScene()->ClipRayHit(ray,clipPlane,res)) {
		out = res.hitPoint;
		return true;
	}
	return false;
}

Editor::Rendering::PickingRenderPass::PickingResult Editor::Panels::SceneView::GetPickResult()
{
	return pickingResult;
}

::Rendering::Geometry::Ray Editor::Panels::SceneView::GetMouseRay()
{
	
	auto [x, y] = input.GetMousePosition();
	return GetCamera()->GetMouseRay(x, y);
}

::Core::ECS::Actor* Editor::Panels::SceneView::GetSelectedActor()
{
	return GetScene()->FindActorByID(mTargetActorId);
}

void Editor::Panels::SceneView::SelectActor(::Core::ECS::Actor& actor)
{
	mTargetActorId = actor.GetID();
}

void Editor::Panels::SceneView::UnselectActor()
{
	mTargetActorId = -1;
}

bool Editor::Panels::SceneView::IsSelectActor()
{
	return GetScene()->FindActorByID(mTargetActorId);
}


Core::Rendering::SceneRenderer::SceneDescriptor Editor::Panels::SceneView::CreateSceneDescriptor()
{
	auto descriptor = AViewControllable::CreateSceneDescriptor();
	descriptor.fallbackMaterial = m_fallbackMaterial;

	if (false)
	{
		auto& scene = *GetScene();

		if (auto mainCameraComponent = scene.FindMainCamera())
		{
			auto& sceneCamera = mainCameraComponent->GetCamera();
			m_camera.SetFrustumGeometryCulling(sceneCamera.HasFrustumGeometryCulling());
			m_camera.SetFrustumLightCulling(sceneCamera.HasFrustumLightCulling());
			descriptor.frustumerride = sceneCamera.GetFrustum();
		}
	}

	return descriptor;
}

void Editor::Panels::SceneView::DrawFrame()
{
	HandleActorPicking();
	Editor::Panels::AViewControllable::DrawFrame();
	// The picking target for this frame has just been rendered (only when a pick
	// was requested), so the click can be resolved against it exactly.
	ResolvePendingClick();
}

bool IsResizing()
{
	return false;
}

void Editor::Panels::SceneView::HandleActorPicking()
{
	ZoneScoped;

	if (m_gizmoOperations.IsPicking() && input.IsMouseButtonReleased(MouseButton::MOUSE_BUTTON_LEFT))
	{
		m_gizmoOperations.StopPicking();
		//GetScene()->BuildSceneBvh();
	}

	// While a gizmo drag is active there is nothing to pick, and the picking
	// readback is a synchronous glReadPixels, so skip it entirely.
	if (m_gizmoOperations.IsPicking())
	{
		return;
	}

	const auto [viewWidth, viewHeight] = GetSafeSize();
	if (viewWidth == 0 || viewHeight == 0)
	{
		return;
	}

	const auto mousePos = input.GetMousePosition();

	// Screen widgets (navigation cube, HUD buttons, ...) own the cursor while it
	// is over them: skip scene picking so a click there does not also select the
	// actor behind the widget, and hovering it does not highlight the scene.
	if (MOON::ScreenOverlayRegistry::Instance().BlocksSceneCursor(
		static_cast<float>(mousePos.first),
		static_cast<float>(mousePos.second)))
	{
		m_highlightedActor = {};
		m_highlightedGizmoDirection = {};
		return;
	}

	// Clamp to the picking target: a drag can move the cursor outside the
	// viewport, and reading outside the framebuffer is undefined.
	const int mouseX = std::clamp(
		static_cast<int>(mousePos.first),
		0,
		static_cast<int>(viewWidth) - 1
	);
	const int mouseY = std::clamp(
		static_cast<int>(viewHeight - mousePos.second - 1.0),
		0,
		static_cast<int>(viewHeight) - 1
	);

	auto& scene = *GetScene();
	auto& actorPickingPass = m_renderer->GetPass<Rendering::PickingRenderPass>("Picking");

	// The picking pass is a full second scene render, so it is only drawn when
	// something actually asks for a pick. Requests are throttled: hovering and
	// camera motion refresh the target at most every kPickThrottleMs ms, while a
	// click always requests one exact frame.
	constexpr qint64 kPickThrottleMs = 33; // ~30 Hz for hover
	if (!m_pickRequestTimer.isValid())
	{
		m_pickRequestTimer.start();
	}

	const bool mouseMoved = mousePos != m_lastPickingMouse;
	const bool leftPressed = input.IsMouseButtonPressed(MouseButton::MOUSE_BUTTON_LEFT);
	const bool leftDown = input.IsMouseButtonDown(MouseButton::MOUSE_BUTTON_LEFT);
	// Right / middle are camera navigation (orbit / pan): the hover highlight is
	// not useful while navigating, so the picking target is left untouched.
	const bool cameraNavigation =
		input.IsMouseButtonDown(MouseButton::MOUSE_BUTTON_MIDDLE)
		|| input.IsMouseButtonDown(MouseButton::MOUSE_BUTTON_RIGHT);

	const Maths::FMatrix4 viewProjection = m_camera.GetViewProjectionMatrix();
	bool cameraMoved = false;
	if (m_hasLastPickViewProjection)
	{
		for (int i = 0; i < 16 && !cameraMoved; ++i)
		{
			cameraMoved = std::abs(viewProjection.data[i] - m_lastPickViewProjection.data[i]) > 1e-6f;
		}
	}

	if (leftPressed)
	{
		// Selection has to be pixel exact: force this frame's target and resolve
		// it after the frame has been drawn (see ResolvePendingClick).
		actorPickingPass.RequestPick(static_cast<uint32_t>(mouseX), static_cast<uint32_t>(mouseY));
		m_clickPickPending = true;
		m_clickPickX = mouseX;
		m_clickPickY = mouseY;
		m_lastPickingMouse = mousePos;
		m_pickRequestTimer.restart();
	}
	else if ((mouseMoved || cameraMoved) && !cameraNavigation)
	{
		if (!leftDown && m_pickRequestTimer.elapsed() >= kPickThrottleMs)
		{
			actorPickingPass.RequestPick(static_cast<uint32_t>(mouseX), static_cast<uint32_t>(mouseY));
			m_lastPickingMouse = mousePos;
			m_pickRequestTimer.restart();
		}
	}

	m_lastPickViewProjection = viewProjection;
	m_hasLastPickViewProjection = true;

	// Hovering consumes the asynchronous result: the CPU never waits for the GPU.
	Rendering::PickingRenderPass::PickingResult finishedPick;
	uint32_t pickX = 0;
	uint32_t pickY = 0;
	if (actorPickingPass.TryConsumePick(scene, finishedPick, pickX, pickY))
	{
		pickingResult = finishedPick;
		ApplyPickResult(pickingResult);
	}
}

void Editor::Panels::SceneView::ApplyPickResult(
	const Rendering::PickingRenderPass::PickingResult& p_result
)
{
	m_highlightedActor = {};
	m_highlightedGizmoDirection = {};

	if (p_result.has_value())
	{
		if (const auto pval = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&p_result.value()))
		{
			m_highlightedActor = *pval;
		}
		else if (const auto pval = std::get_if<Editor::Core::GizmoBehaviour::EDirection>(&p_result.value()))
		{
			m_highlightedGizmoDirection = *pval;
		}
	}
}

void Editor::Panels::SceneView::ResolvePendingClick()
{
	if (!m_clickPickPending)
	{
		return;
	}
	m_clickPickPending = false;

	auto& scene = *GetScene();
	auto& actorPickingPass = m_renderer->GetPass<Rendering::PickingRenderPass>("Picking");

	bool isSeletedSomething = false;
	pickingResult = actorPickingPass.ReadbackPickingResult(
		scene,
		static_cast<uint32_t>(m_clickPickX),
		static_cast<uint32_t>(m_clickPickY),
		isSeletedSomething);
	ApplyPickResult(pickingResult);

	if (m_highlightedGizmoDirection)
	{
		m_gizmoOperations.StartPicking(
			*GetSelectedActor(),
			m_camera.GetPosition(),
			m_currentOperation,
			m_highlightedGizmoDirection.value());
	}
	else if (m_highlightedActor)
	{
		SelectActor(m_highlightedActor.value());
	}
	else if (!isSeletedSomething)
	{
		UnselectActor();
	}
}


