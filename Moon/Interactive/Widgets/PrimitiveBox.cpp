#include "Interactive/Widgets/PrimitiveBox.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "renderer/SceneView.h"

#include "core/component/CTopoShape.h"
#include "feature/Feature.h"
#include "TopoShape.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <cmath>

namespace MOON {
	namespace {
		const Eigen::Vector4<uint8_t> kGreen(255, 0, 255, 0);
		const Eigen::Vector4<uint8_t> kGold(255, 0, 215, 255);
		const Eigen::Vector4<uint8_t> kWhite(255, 255, 255, 255);
		const Eigen::Vector4<uint8_t> kBrown(255, 19, 69, 139);
		const Eigen::Vector4<uint8_t> kDeepBrown(255, 82, 111, 108);

		Eigen::Vector2f viewportCursor(Editor::Panels::SceneView* view)
		{
			const auto [mx, my] = view->getInutState().GetMousePosition();
			return Eigen::Vector2f(static_cast<float>(mx), static_cast<float>(my));
		}
		using MathUtilRay = MOON::Ray;
		MathUtilRay currentRay(Editor::Panels::SceneView* view)
		{
			const auto r = view->GetMouseRay();
			return MathUtilRay(
				Eigen::Vector3f(r.origin_.x, r.origin_.y, r.origin_.z),
				Eigen::Vector3f(r.direction_.x, r.direction_.y, r.direction_.z)
			);
		}

		Eigen::Vector3f viewForward(Editor::Panels::SceneView* view)
		{
			const Maths::FVector3 pos = view->GetCamera()->GetPosition();
			return Eigen::Vector3f(pos.x, pos.y, pos.z);
		}
	}  // namespace

	PrimitiveBox::PrimitiveBox(const std::string& name)
		: PrimitiveShape(name)
	{
		translation = Eigen::Vector3f(0, 0, 0);
		rot = Eigen::Matrix3f::Identity();
		scale = Eigen::Vector3f(1, 1, 1);
	}

	PrimitiveBox::~PrimitiveBox() = default;

	Eigen::Vector3f PrimitiveBox::faceCenter(BoxPart part) const
	{
		int axis = 0;
		float sign = 1.0f;
		switch (part) {
		case BoxPart::FacePlusX: axis = 0; sign = 1.0f; break;
		case BoxPart::FaceMinusX: axis = 0; sign = -1.0f; break;
		case BoxPart::FacePlusY: axis = 1; sign = 1.0f; break;
		case BoxPart::FaceMinusY: axis = 1; sign = -1.0f; break;
		case BoxPart::FacePlusZ: axis = 2; sign = 1.0f; break;
		case BoxPart::FaceMinusZ: axis = 2; sign = -1.0f; break;
		default: return translation;
		}
		const Eigen::Vector3f axisDir = rot.col(axis);
		return translation + axisDir * (scale[axis] * sign);
	}

	void PrimitiveBox::onUpdate()
	{
		drawGizmo();
	}

	void PrimitiveBox::drawGizmo()
	{
		const Eigen::Vector3f xAxis = rot.col(0);
		const Eigen::Vector3f yAxis = rot.col(1);
		const Eigen::Vector3f zAxis = rot.col(2);
		const BoxPart faces[] = {
			BoxPart::FacePlusX, BoxPart::FaceMinusX,
			BoxPart::FacePlusY, BoxPart::FaceMinusY,
			BoxPart::FacePlusZ, BoxPart::FaceMinusZ
		};
		const Eigen::Vector3f quads[6][4] = {
			{ Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, 1, -1),
			  Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(1, -1, 1) },
			{ Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(-1, 1, -1),
			  Eigen::Vector3f(-1, 1, 1), Eigen::Vector3f(-1, -1, 1) },
			{ Eigen::Vector3f(-1, 1, -1), Eigen::Vector3f(1, 1, -1),
			  Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(-1, 1, 1) },
			{ Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1),
			  Eigen::Vector3f(1, -1, 1), Eigen::Vector3f(-1, -1, 1) },
			{ Eigen::Vector3f(-1, -1, 1), Eigen::Vector3f(1, -1, 1),
			  Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(-1, 1, 1) },
			{ Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1),
			  Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(-1, 1, -1) }
		};
		const bool centerActive = m_dragOp == DragOp::Translate
			&& m_dragPart == BoxPart::Center;
		const bool boxActive = m_dragOp == DragOp::Rotate;
		const bool outlineHot = centerActive || boxActive;

		// Box outline and axis lines (they turn green while dragging the
		// centre/rotating, matching boxEdit).
		renderer->pushMatrix(Coord3(translation, rot, scale));
		renderer->pushSize(2.0f);
		renderer->pushColor(outlineHot ? kGreen : kWhite);
		renderer->drawLine(Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(1, 0, 0));
		renderer->drawLine(Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 1, 0));
		renderer->drawLine(Eigen::Vector3f(0, 0, -1), Eigen::Vector3f(0, 0, 1));
		renderer->drawAlignedBox(Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1));
		renderer->popColor();
		renderer->popSize();

		// Hovered / dragged face gets the translucent brown highlight.
		for (int i = 0; i < 6; ++i) {
			const bool faceHot = (m_hover == faces[i]) || (m_dragPart == faces[i]);
			if (faceHot) {
				renderer->pushAlpha(0.3f);
				renderer->pushColor(kBrown);
				renderer->drawQuadFilled(quads[i][0], quads[i][1], quads[i][2], quads[i][3]);
				renderer->popColor();
				renderer->popAlpha();
			}
		}

		// While rotating, show the translucent volume like boxEdit does.
		if (boxActive) {
			renderer->pushAlpha(0.4f);
			renderer->pushColor(kDeepBrown);
			renderer->drawAlignedBoxFilled(
				Eigen::Vector3f(-1, -1, -1),
				Eigen::Vector3f(1, 1, 1)
			);
			renderer->popColor();
			renderer->popAlpha();
		}
		renderer->popMatrix();

		// Face handle points.
		for (const BoxPart f : faces) {
			const Eigen::Vector3f pos = faceCenter(f);
			const bool hot = (m_hover == f) || (m_dragPart == f);
			renderer->drawPoint(pos, 10.0f, hot ? kGold : kGreen);
		}
		const bool centerHot = (m_hover == BoxPart::Center) || (m_dragPart == BoxPart::Center);
		renderer->drawPoint(translation, 10.0f, centerHot ? kGold : kGreen);

		// Small axis stubs help identify orientation.
		renderer->pushSize(1.0f);
		renderer->pushColor(outlineHot ? kGold : kWhite);
		const float hs = renderer->pixelsToWorldSize(translation, 12.0f) * 1.5f;
		renderer->drawLine(translation, translation + xAxis * hs);
		renderer->drawLine(translation, translation + yAxis * hs);
		renderer->drawLine(translation, translation + zAxis * hs);
		renderer->popColor();
		renderer->popSize();

		// Rotation axis is drawn while rotating, like boxEdit's gold line.
		if (m_dragOp == DragOp::Rotate && m_rotateAxis.norm() > 1.0e-6f) {
			const float len = renderer->pixelsToWorldSize(translation, 12.0f) * 40.0f;
			renderer->pushSize(3.0f);
			renderer->pushColor(kGold);
			renderer->drawLine(translation - m_rotateAxis * len, translation + m_rotateAxis * len);
			renderer->popColor();
			renderer->popSize();
		}
	}

	void PrimitiveBox::onMouseMove()
	{
		if (m_dragOp != DragOp::None) {
			updateDrag();
		}
		else {
			updateHover();
		}
	}

	void PrimitiveBox::onLeftMousePressed()
	{
		updateHover();
		if (m_hover != BoxPart::None) {
			beginDrag();
		}
	}

	void PrimitiveBox::onLeftMouseReleased()
	{
		m_dragOp = DragOp::None;
		m_dragPart = BoxPart::None;
		m_rotateAxis = Eigen::Vector3f(0, 0, 0);
	}

	void PrimitiveBox::updateHover()
	{
		m_hover = BoxPart::None;
		const Eigen::Vector2f cursor = viewportCursor(m_sceneView);

		const BoxPart faces[] = {
			BoxPart::FacePlusX, BoxPart::FaceMinusX,
			BoxPart::FacePlusY, BoxPart::FaceMinusY,
			BoxPart::FacePlusZ, BoxPart::FaceMinusZ
		};
		float best = 1e9f;
		for (const BoxPart f : faces) {
			const Eigen::Vector2f s = renderer->worldToScreen(faceCenter(f));
			const float d = (s - cursor).norm();
			if (d < 10.0f && d < best) {
				best = d;
				m_hover = f;
			}
		}
		const Eigen::Vector2f c = renderer->worldToScreen(translation);
		const float cd = (c - cursor).norm();
		if (cd < 10.0f && cd < best) {
			best = cd;
			m_hover = BoxPart::Center;
		}

		if (m_hover == BoxPart::None) {
			// Rotate by clicking the body: transform the mouse ray into the
			// unit box space and test against [-1,1]^3.
			const MathUtilRay world = currentRay(m_sceneView);
			Eigen::Matrix4f mat = Coord3(translation, rot, scale);
			const Eigen::Matrix4f inv = mat.inverse();
			const Eigen::Vector3f localO = MatrixMulPoint(inv, world.m_origin);
			const Eigen::Vector4f localD = inv * Eigen::Vector4f(
				world.m_direction.x(), world.m_direction.y(), world.m_direction.z(), 0.0f);
			const Eigen::Vector3f localDir = localD.head<3>().normalized();
			float tr = 0.0f;
			if (IntersectBox(
					MathUtilRay(localO, localDir),
					Eigen::Vector3f(-1, -1, -1),
					Eigen::Vector3f(1, 1, 1),
					tr)) {
				m_hover = BoxPart::Body;
			}
		}
	}

	void PrimitiveBox::beginDrag()
	{
		m_dragPart = m_hover;
		m_startTranslation = translation;
		m_startRotation = rot;
		m_startScale = scale;

		if (m_hover == BoxPart::Center) {
			m_dragOp = DragOp::Translate;
			const Eigen::Vector3f eye = viewForward(m_sceneView);
			const Eigen::Vector3f normal = (eye - translation).normalized();
			const Plane plane(normal, translation);
			const MathUtilRay ray = currentRay(m_sceneView);
			float tr = 0.0f;
			if (Intersect(ray, plane, tr)) {
				m_planeHitStart = ray.m_origin + ray.m_direction * tr;
			}
			return;
		}

		if (m_hover == BoxPart::Body) {
			m_dragOp = DragOp::Rotate;
			m_rotateAxis = Eigen::Vector3f(0, 0, 0);
			m_cursorDown = viewportCursor(m_sceneView);  // pixel space, like boxEdit
			return;
		}

		// One of the face handles: scale along the face normal.
		m_dragOp = DragOp::ScaleFace;
		switch (m_hover) {
		case BoxPart::FacePlusX: case BoxPart::FaceMinusX: m_dragAxis = 0; break;
		case BoxPart::FacePlusY: case BoxPart::FaceMinusY: m_dragAxis = 1; break;
		case BoxPart::FacePlusZ: case BoxPart::FaceMinusZ: m_dragAxis = 2; break;
		default: break;
		}
		m_dragSign = (m_hover == BoxPart::FacePlusX || m_hover == BoxPart::FacePlusY
			|| m_hover == BoxPart::FacePlusZ) ? 1.0f : -1.0f;
		m_dragNormal = rot.col(m_dragAxis) * m_dragSign;
		const Eigen::Vector3f facePos = faceCenter(m_hover);
		const Line axisLine(facePos, m_dragNormal);
		const MathUtilRay ray = currentRay(m_sceneView);
		float tr = 0.0f;
		Nearest(ray, axisLine, tr, m_startAxisT);
	}

	void PrimitiveBox::updateDrag()
	{
		if (m_dragOp == DragOp::Translate) {
			const Eigen::Vector3f eye = viewForward(m_sceneView);
			const Eigen::Vector3f normal = (eye - m_startTranslation).normalized();
			const Plane plane(normal, m_startTranslation);
			const MathUtilRay ray = currentRay(m_sceneView);
			float tr = 0.0f;
			if (Intersect(ray, plane, tr)) {
				const Eigen::Vector3f hit = ray.m_origin + ray.m_direction * tr;
				translation = m_startTranslation + (hit - m_planeHitStart);
			}
			return;
		}

		if (m_dragOp == DragOp::Rotate) {
			const Eigen::Vector2f cur = viewportCursor(m_sceneView);
			Eigen::Vector2f offset = 0.5f * (cur - m_cursorDown) / 180.0f * 3.14159265f;
			offset.y() *= -1.0f;

			const Maths::FMatrix4& vm = m_sceneView->GetCamera()->GetViewMatrix();
			Eigen::Matrix4f view;
			std::memcpy(view.data(), vm.data, 16 * sizeof(float));
			view.transposeInPlace();
			const Eigen::Vector3f axis = (
				view.block<1, 3>(1, 0).transpose() * offset.x()
				- view.block<1, 3>(0, 0).transpose() * offset.y()
			).normalized();
			m_rotateAxis = axis;
			rot = RotationMatrix(axis, offset.norm()) * m_startRotation;
			return;
		}

		if (m_dragOp == DragOp::ScaleFace) {
			const Eigen::Vector3f faceStart = m_startTranslation
				+ m_dragNormal * m_startScale[m_dragAxis];
			const Line axisLine(faceStart, m_dragNormal);
			const MathUtilRay ray = currentRay(m_sceneView);
			float tr = 0.0f;
			float tl = 0.0f;
			Nearest(ray, axisLine, tr, tl);
			const float delta = tl - m_startAxisT;
			translation = m_startTranslation
				+ m_dragNormal * (delta * 0.5f);
			scale = m_startScale;
			scale[m_dragAxis] = m_startScale[m_dragAxis] + delta * 0.5f;
			if (scale[m_dragAxis] < 0.01f) {
				scale[m_dragAxis] = 0.01f;
			}
		}
	}

	void PrimitiveBox::createTopoShape()
	{
		BRepPrimAPI_MakeBox mkBox(2.0 * scale.x(), 2.0 * scale.y(), 2.0 * scale.z());
		TopoDS_Shape resultShape = mkBox.Shape();
		auto* topoActor = new Feature3D("BoxFeature", "Box");
		const auto& topoComp = topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();
		topo.setShape(resultShape, false);

		Eigen::Matrix4f mat1 = Coord3(-scale, Eigen::Matrix3f::Identity(), Eigen::Vector3f(1, 1, 1));
		Eigen::Matrix4f mat2 = Coord3(Eigen::Vector3f(0, 0, 0), rot, Eigen::Vector3f(1, 1, 1));
		Eigen::Matrix4f mat3 = Coord3(translation, Eigen::Matrix3f::Identity(), Eigen::Vector3f(1, 1, 1));
		Eigen::Matrix4f mat = mat3 * mat2 * mat1;
		Base::Matrix4D mm(
			mat(0, 0), mat(0, 1), mat(0, 2), mat(0, 3),
			mat(1, 0), mat(1, 1), mat(1, 2), mat(1, 3),
			mat(2, 0), mat(2, 1), mat(2, 2), mat(2, 3),
			mat(3, 0), mat(3, 1), mat(3, 2), mat(3, 3)
		);
		topo.setTransform(mm);
		topoActor->makeDone();
	}
}
