#include "Interactive/Widgets/Gizmo3DWidget.h"
#include "Interactive/Widgets/GizmoGeometry.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "Interactive/ViewData.h"
#include "Interactive/GizmoBehaviour.h"
#include "renderer/SceneView.h"
#include "Core/Global/ServiceLocator.h"
#include <Rendering/Overlay/Im2DRender.h>
#include <Rendering/Overlay/Im2D.h>
#include <Rendering/Overlay/Im2DFont.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace MOON
{
	namespace
	{
		// Triangle/colour builders: Interactive/Widgets/GizmoGeometry.h (shared with Gizmo2DWidget).
		using namespace MOON::GizmoGeometry;

		// -----------------------------------------------------------------------------------
		// Local gizmo units. The axis arrows reach 0.80 and the rings sit at 0.62; how many
		// pixels that is comes from ComputeFrameScale() below.
		// -----------------------------------------------------------------------------------
		/** Same 200-pixels-per-unit convention as the FIXED_SCALE gizmo shader (see the comment
		 * block in ClipPlane.cpp): the axis arrow is 0.80 units long, so 0.6 puts its tip ~96 px
		 * from the pivot - the size the other gizmos in this editor have. */
		constexpr float kScreenScale = 0.6f;
		constexpr float kShaftEnd = 0.62f;
		constexpr float kArrowTip = 0.80f;
		constexpr float kShaftRadius = 0.016f;
		constexpr float kHeadRadius = 0.052f;
		constexpr float kPlaneInner = 0.16f;
		constexpr float kPlaneOuter = 0.40f;
		constexpr float kRingRadius = 0.62f;
		constexpr float kRingHalfThickness = 0.028f;
		constexpr int kTubeSegments = 16;
		constexpr int kRingSegments = 48;

		/** A square patch between two axes (the plane handle). The axes are passed in rather than
		 * derived: a perpendicular basis is arbitrary in sign, which put the patch in the negative
		 * quadrant instead of between the two positive axes it belongs to. */
		void AddPlanePatch(std::vector<Eigen::Vector3f>& p_out, const Eigen::Vector3f& p_u,
			const Eigen::Vector3f& p_v)
		{
			const Eigen::Vector3f a = p_u * kPlaneInner + p_v * kPlaneInner;
			const Eigen::Vector3f b = p_u * kPlaneOuter + p_v * kPlaneInner;
			const Eigen::Vector3f c = p_u * kPlaneOuter + p_v * kPlaneOuter;
			const Eigen::Vector3f d = p_u * kPlaneInner + p_v * kPlaneOuter;
			AddQuad(p_out, a, b, c, d);
		}

		const std::array<const char*, 9>& HandleNames()
		{
			static const std::array<const char*, 9> names = {
				"XArrow", "YArrow", "ZArrow",
				"XPlane", "YPlane", "ZPlane",
				"XRing", "YRing", "ZRing"
			};
			return names;
		}

		/** Idle colours, one per axis: the arrows and their plane/ring keep the same hue so a
		 * glance tells which axis a handle belongs to. */
		Eigen::Vector4<uint8_t> IdleColor(int p_axis)
		{
			switch (p_axis)
			{
			case 0: return Rgba(226, 74, 74);
			case 1: return Rgba(96, 200, 96);
			default: return Rgba(88, 132, 240);
			}
		}
	}

	class Gizmo3DWidget::Internal {
	public:
		Internal(Gizmo3DWidget* p_self) : self(p_self) {}
		~Internal() {}

		friend class Gizmo3DWidget;

		Gizmo3DWidget* self = nullptr;

		/** Where setUpOrigin put the pivot. getTranslate() is measured from here, so a plane drag
		 * that moves the pivot sideways is still reported as a displacement. */
		Eigen::Vector3f origin = Eigen::Vector3f::Zero();
		/** Current pivot: the handles move it. */
		Eigen::Vector3f center = Eigen::Vector3f::Zero();
		/** The basis at setUpOrigin/drag start, so the rotation is always measured from a known
		 * frame instead of being accumulated into a matrix that slowly drifts. */
		Eigen::Matrix3f startBasis = Eigen::Matrix3f::Identity();
		/** Current basis: the columns are the gizmo's x/y/z. */
		Eigen::Matrix3f basis = Eigen::Matrix3f::Identity();

		bool placed = false;
		float userScale = 1.0f;
		/** World units per local unit for the current frame (pivot-constant on screen). */
		float frameScale = 0.0f;

		WidgetViewData viewData;

		GizmoAxisTranslate axisPick;
		GizmoPlaneTranslate planePick;
		GizmoAxisRotate rotatePick;

		/** The axis (or plane normal, or rotation axis) the current drag works on, fixed at press
		 * so the basis cannot chase itself. */
		Eigen::Vector3f dragAxis = Eigen::Vector3f::UnitX();
		/** A direction perpendicular to dragAxis, the rotation's reference arm. */
		Eigen::Vector3f dragRefDir = Eigen::Vector3f::UnitZ();
		/** Basis when the rotation drag started. */
		Eigen::Matrix3f dragStartBasis = Eigen::Matrix3f::Identity();
	};

	Gizmo3DWidget::Gizmo3DWidget(const std::string& name)
		: EventWidget(name), mInternal(new Internal(this)), mState(Stop)
	{
		// Nine handles as plain triangle lists: three arrows, three plane patches, three rings.
		// Drawn and picked through WidgetViewData (see the header for why not the gizmo shader).
		for (int handle = 0; handle < 9; ++handle)
		{
			std::vector<Eigen::Vector3f> triangles;
			const Eigen::Vector3f axis =
				Eigen::Vector3f::Unit(handle % 3);
			if (handle < 3)
			{
				AddTube(triangles, Eigen::Vector3f::Zero(), axis * kShaftEnd,
					kShaftRadius, kShaftRadius, kTubeSegments);
				AddTube(triangles, axis * kShaftEnd, axis * kArrowTip,
					kHeadRadius, 0.0f, kTubeSegments);
			}
			else if (handle < 6)
			{
				// The patch sits between the two positive axes of its plane; the handle's own axis
				// is that plane's normal (X -> Y/Z, Y -> Z/X, Z -> X/Y).
				AddPlanePatch(triangles,
					Eigen::Vector3f::Unit((handle % 3 + 1) % 3),
					Eigen::Vector3f::Unit((handle % 3 + 2) % 3));
			}
			else
			{
				AddRing(triangles, axis, kRingRadius, kRingHalfThickness, kRingSegments);
			}

			// The plane patches are the only translucent handles: the arrows and the rings want to
			// stay readable as lines, the patches read better when the model shows through.
			Eigen::Vector4<uint8_t> color = IdleColor(handle % 3);
			if (handle >= 3 && handle < 6)
			{
				color = Rgba(color[3], color[2], color[1], 190);
			}
			mInternal->viewData.setTriangleFace(HandleNames()[handle], triangles, color);
		}

		// Visible on its own, like AxisTranslationWidget: whatever wants to hide it calls
		// setActive(false) on it by name through the gizmo pass.
		setActive(true);
	}

	Gizmo3DWidget::~Gizmo3DWidget()
	{
		delete mInternal;
	}

	void Gizmo3DWidget::onSetActive(bool flag)
	{
		if (flag)
		{
			// Same idea as ClipPlane (every activation re-centres on the scene), but the actual
			// recentre waits for the first update: setActive() also runs from the constructor, and
			// this widget is built while the SceneView it reads through is still being constructed.
			mInternal->placed = false;
		}
	}

	bool Gizmo3DWidget::TryPlaceAtScene()
	{
		if (m_sceneView == nullptr)
		{
			return false;
		}
		auto* scene = m_sceneView->GetScene();
		if (scene == nullptr)
		{
			return false;
		}
		const auto box = scene->GetSceneBoundingBox();
		if (!box.isValid())
		{
			return false;
		}
		const Maths::FVector3 centre = box.center();
		mInternal->origin = Eigen::Vector3f(centre.x, centre.y, centre.z);
		mInternal->center = mInternal->origin;
		mInternal->placed = true;
		SyncTransform();
		return true;
	}

	void Gizmo3DWidget::SyncTransform()
	{
		Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
		model.block<3, 3>(0, 0) = mInternal->basis;
		model.block<3, 1>(0, 3) = mInternal->center;
		for (TriangleFace& face : mInternal->viewData.getFaces())
		{
			face.model = model;
		}
	}

	float Gizmo3DWidget::ComputeFrameScale() const
	{
		// The same camera-distance ratio the FIXED_SCALE gizmo shader uses (200 px per unit, see
		// the derivation in ClipPlane.cpp), scaled to this gizmo's size. Computed from the current
		// pivot, so the widget keeps its screen size as the camera moves.
		const auto* camera = m_sceneView != nullptr ? m_sceneView->GetCamera() : nullptr;
		float ratio = 1.0f;
		if (camera != nullptr)
		{
			const auto& projection = camera->GetProjectionMatrix();
			const float proj11 = projection(1, 1);
			const float screenHeight = static_cast<float>(
				m_sceneView->GetRenderer().GetFrameDescriptor().renderHeight);
			if (proj11 > 0.0f && screenHeight > 0.0f)
			{
				if (camera->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC)
				{
					ratio = 400.0f / (proj11 * screenHeight);
				}
				else
				{
					const Maths::FVector3 viewPos = camera->GetViewMatrix().MulPoint(
						Maths::FVector3{ mInternal->center.x(), mInternal->center.y(), mInternal->center.z() });
					ratio = 400.0f * std::abs(viewPos.z) / (proj11 * screenHeight);
				}
			}
		}
		return ratio * kScreenScale * mInternal->userScale;
	}

	void Gizmo3DWidget::onUpdate()
	{
		if (!mInternal->placed)
		{
			TryPlaceAtScene();
		}

		// One scale for both drawing and picking, so a handle is where it is drawn.
		mInternal->frameScale = ComputeFrameScale();

		SyncTransform();

		const float scale = mInternal->frameScale;
		Eigen::Matrix4f scaleMat = Eigen::Matrix4f::Identity();
		scaleMat(0, 0) = scaleMat(1, 1) = scaleMat(2, 2) = scale;

		const bool dragging = mState == AxisT || mState == PlaneT || mState == AxisR;
		// The handles are WidgetViewData triangle soups; the face carries the per-vertex normals
		// it computed when the geometry was built, so lighting needs no per-frame work here.
		renderer->pushEnableLit(true);
		for (int handle = 0; handle < 9; ++handle)
		{
			TriangleFace* face = nullptr;
			if (!mInternal->viewData.getTriangleFace(HandleNames()[handle], face) || face == nullptr)
			{
				continue;
			}

			Eigen::Vector4<uint8_t> color = face->color;
			if (handle == mPickHandle && mState != Stop)
			{
				// Hover is the cyan the other widgets use, the active drag yellow.
				color = dragging ? Rgba(255, 226, 64) : Rgba(32, 232, 232);
			}

			renderer->pushMatrix(face->model * scaleMat);
			renderer->drawTriangleList(face->faces, face->normals, 1.0f, color);
			renderer->popMatrix();
		}
		renderer->popEnableLit();

		// While a ring is being dragged: the swept arc and the angle, like ClipPlane's.
		if (mState == AxisR)
		{
			std::vector<Eigen::Vector3f> arc = mInternal->rotatePick.getRotationArc();
			if (arc.size() > 1)
			{
				// getRotationArc() works on the unit reference arm; put it on the ring itself so
				// the arc follows the handle the user grabbed.
				const float radius = kRingRadius * scale;
				for (Eigen::Vector3f& point : arc)
				{
					const Eigen::Vector3f rel = point - mInternal->center;
					if (rel.norm() > 1.0e-6f)
					{
						point = mInternal->center + rel.normalized() * radius;
					}
				}
				renderer->pushColor(Rgba(255, 255, 255));
				for (size_t i = 0; i + 1 < arc.size(); ++i)
				{
					renderer->drawLine(arc[i], arc[i + 1], 3);
				}
				renderer->popColor();
				renderer->pushColor(Rgba(255, 60, 255));
				renderer->drawLine(arc.front(), mInternal->center, 2);
				renderer->drawLine(arc.back(), mInternal->center, 2);
				renderer->popColor();
			}

			// During the drag the helper's own accumulated angle is what the mouse swept (signed,
			// and it keeps growing past half a turn); after a drag the readout falls back to the
			// magnitude derived from the basis.
			const float degrees = mInternal->rotatePick.getRotationAngleDegree();
			std::string text = std::to_string(static_cast<int>(degrees)) + " degree\n";
			const auto pos = renderer->getFrameParam().cursor;
			auto& render2d = Rendering::Overlay::Im2DRender::Instance();
			render2d.GetDrawList().AddText(render2d.GetFont(), 20.0f,
				{ pos.x(), pos.y() - 20 },
				Rendering::Overlay::Im2D::ColorU32(255, 255, 0, 255), text.c_str());
		}
		else if (mState == AxisT || mState == PlaneT)
		{
			// The displacement readout: the same "what did the drag produce" the owner sees
			// through getTranslate(), without having to log it.
			const Eigen::Vector3f delta = getTranslate();
			char text[96] = {};
			std::snprintf(text, sizeof(text), "(%.2f, %.2f, %.2f)  |%.2f|",
				static_cast<double>(delta.x()), static_cast<double>(delta.y()),
				static_cast<double>(delta.z()), static_cast<double>(delta.norm()));
			const auto pos = renderer->getFrameParam().cursor;
			auto& render2d = Rendering::Overlay::Im2DRender::Instance();
			render2d.GetDrawList().AddText(render2d.GetFont(), 18.0f,
				{ pos.x(), pos.y() - 20 },
				Rendering::Overlay::Im2D::ColorU32(255, 255, 0, 255), text);
		}
	}

	int Gizmo3DWidget::HitHandle(const Ray& p_ray)
	{
		if (mInternal->frameScale <= 0.0f)
		{
			// Picking can run before this frame's onUpdate (input is dispatched ahead of drawing).
			mInternal->frameScale = ComputeFrameScale();
			SyncTransform();
		}
		// hitFace() applies the frame scale to the face model itself, which is exactly what the
		// draw path does with face.model * scaleMat.
		const std::string hit = mInternal->viewData.hitFace(p_ray, mInternal->frameScale);
		if (hit.empty())
		{
			return None;
		}
		for (int handle = 0; handle < 9; ++handle)
		{
			if (hit == HandleNames()[handle])
			{
				return handle;
			}
		}
		return None;
	}

	Ray Gizmo3DWidget::MouseRay()
	{
		const ::Rendering::Geometry::Ray ray = m_sceneView->GetMouseRay();
		return Ray(
			Eigen::Vector3f(ray.origin_.x, ray.origin_.y, ray.origin_.z),
			Eigen::Vector3f(ray.direction_.x, ray.direction_.y, ray.direction_.z));
	}

	void Gizmo3DWidget::onMouseMove()
	{
		if (m_sceneView == nullptr)
		{
			return;
		}

		// The handles may have moved since the last frame (a drag, setUpOrigin, a re-centre):
		// refresh the transform before hit-testing, so picking and drawing agree within a frame.
		SyncTransform();

		if (mState == Stop || mState == Hot)
		{
			const int hit = HitHandle(MouseRay());
			if (hit == None)
			{
				mState = Stop;
				mPickHandle = None;
			}
			else
			{
				mState = Hot;
				mPickHandle = hit;
			}
			return;
		}

		auto& param = renderer->getFrameParam();
		Eigen::Vector3f next = mInternal->center;

		if (mState == AxisT)
		{
			mInternal->axisPick.apply(param.rayDirection, param.rayOrigin, next);
		}
		else if (mState == PlaneT)
		{
			mInternal->planePick.apply(param.rayDirection, param.rayOrigin, next);
		}
		else if (mState == AxisR)
		{
			Eigen::Vector3f dir = mInternal->dragRefDir;
			if (mInternal->rotatePick.applyDir(param.rayDirection, param.rayOrigin, dir))
			{
				// The total angle about the axis fixed at press time, applied to the basis the
				// drag started from: the frame stays orthonormal however long the drag runs.
				const float angle = mInternal->rotatePick.getRotationAngle();
				mInternal->basis = Eigen::AngleAxisf(angle, mInternal->dragAxis).toRotationMatrix()
					* mInternal->dragStartBasis;
			}
		}

		if (next != mInternal->center)
		{
			mInternal->center = next;
		}

		SyncTransform();

		if (mImInvoke)
		{
			this->InvokeEvent(Gizmo3DEvent::TransformChange);
		}
	}

	void Gizmo3DWidget::onLeftMousePressed()
	{
		if (mState == Stop)
		{
			// A press can land before the first mouse-move of the frame (a click right after the
			// gizmo appeared): resolve the handle from the current ray instead of ignoring it.
			SyncTransform();
			const int hit = HitHandle(MouseRay());
			if (hit != None)
			{
				mPickHandle = hit;
				mState = Hot;
			}
		}
		if (mState != Hot || mPickHandle == None)
		{
			return;
		}

		const int axisIndex = mPickHandle % 3;
		const Eigen::Vector3f axis = mInternal->basis.col(axisIndex);

		if (mPickHandle < 3)
		{
			mState = AxisT;
			mInternal->dragAxis = axis;
			mInternal->axisPick.startPick(axis, mInternal->center);
		}
		else if (mPickHandle < 6)
		{
			mState = PlaneT;
			mInternal->dragAxis = axis;
			// The plane through the pivot whose normal is this axis: n . p + d = 0 with d = -n . c.
			const Eigen::Vector4f plane(axis.x(), axis.y(), axis.z(), -axis.dot(mInternal->center));
			mInternal->planePick.startPick(plane, mInternal->center);
		}
		else
		{
			mState = AxisR;
			mInternal->dragAxis = axis;
			mInternal->dragStartBasis = mInternal->basis;
			// The reference arm: the next basis axis, so the ring drag starts from a stable
			// direction that is perpendicular to the rotation axis.
			mInternal->dragRefDir = mInternal->basis.col((axisIndex + 1) % 3);
			mInternal->rotatePick.startPick(axis, mInternal->center, mInternal->dragRefDir,
				mInternal->center + mInternal->dragRefDir);
		}
	}

	void Gizmo3DWidget::onLeftMouseReleased()
	{
		if (mState != AxisT && mState != PlaneT && mState != AxisR)
		{
			return;
		}

		mState = mPickHandle == None ? Stop : Hot;

		// Immediate invoke reports every move; otherwise the release is the one report, the same
		// contract AxisTranslationWidget has.
		if (!mImInvoke)
		{
			this->InvokeEvent(Gizmo3DEvent::TransformChange);
		}
	}

	// --- owner-facing API ---------------------------------------------------------------

	void Gizmo3DWidget::setUpOrigin(float x, float y, float z)
	{
		mInternal->origin = Eigen::Vector3f(x, y, z);
		mInternal->center = mInternal->origin;
		mInternal->startBasis = mInternal->basis;
		mInternal->placed = true;
		SyncTransform();
	}

	void Gizmo3DWidget::setUpDir(float x, float y, float z)
	{
		Eigen::Vector3f zAxis(x, y, z);
		if (zAxis.norm() < 1.0e-6f)
		{
			return;
		}
		zAxis.normalize();
		const Eigen::Vector3f up =
			std::abs(zAxis.y()) < 0.9f ? Eigen::Vector3f(0, 1, 0) : Eigen::Vector3f(1, 0, 0);
		const Eigen::Vector3f yAxis = (up - zAxis * up.dot(zAxis)).normalized();
		const Eigen::Vector3f xAxis = yAxis.cross(zAxis).normalized();

		Eigen::Matrix3f basis;
		basis.col(0) = xAxis;
		basis.col(1) = yAxis;
		basis.col(2) = zAxis;
		mInternal->basis = basis;
		mInternal->startBasis = basis;
		SyncTransform();
	}

	void Gizmo3DWidget::setUpScale(float s)
	{
		mInternal->userScale = s > 0.0f ? s : 0.001f;
	}

	Eigen::Vector3f Gizmo3DWidget::getTranslate() const
	{
		return mInternal->center - mInternal->origin;
	}

	Eigen::Quaternionf Gizmo3DWidget::getRotation() const
	{
		Eigen::Quaternionf rotation(mInternal->basis * mInternal->startBasis.transpose());
		rotation.normalize();
		return rotation;
	}

	float Gizmo3DWidget::getRotateAngle() const
	{
		return GetDragAngleDegree();
	}

	Eigen::Vector3f Gizmo3DWidget::getRotateAxis() const
	{
		const Eigen::Quaternionf rotation = getRotation();
		const Eigen::Vector3f axis = rotation.vec();
		return axis.norm() > 1.0e-6f ? Eigen::Vector3f(axis.normalized()) : Eigen::Vector3f(0, 1, 0);
	}

	Eigen::Matrix4f Gizmo3DWidget::getTransform() const
	{
		Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
		transform.block<3, 3>(0, 0) = mInternal->basis;
		transform.block<3, 1>(0, 3) = mInternal->center;
		return transform;
	}

	void Gizmo3DWidget::setTranslate(float x, float y, float z)
	{
		mInternal->center = mInternal->origin + Eigen::Vector3f(x, y, z);
		SyncTransform();
	}

	void Gizmo3DWidget::setRotate(float p_angleDegree, float x, float y, float z)
	{
		Eigen::Vector3f axis(x, y, z);
		if (axis.norm() < 1.0e-6f)
		{
			axis = Eigen::Vector3f(0, 1, 0);
		}
		axis.normalize();
		const float radians = p_angleDegree * kPi / 180.0f;
		mInternal->basis = Eigen::AngleAxisf(radians, axis).toRotationMatrix() * mInternal->startBasis;
		SyncTransform();
	}

	float Gizmo3DWidget::GetDragAngleDegree() const
	{
		const Eigen::Quaternionf rotation = getRotation();
		const float w = std::min(1.0f, std::abs(rotation.w()));
		return 2.0f * std::acos(w) * 180.0f / kPi;
	}
}
