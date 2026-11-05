#include "Interactive/Widgets/Gizmo2DWidget.h"
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

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace MOON
{
	namespace
	{
		// Triangle/colour builders: Interactive/Widgets/GizmoGeometry.h (shared with Gizmo3DWidget).
		using namespace MOON::GizmoGeometry;

		// Local gizmo units, sized like Gizmo3DWidget's so the two read the same on screen.
		constexpr float kScreenScale = 0.6f;
		constexpr float kShaftEnd = 0.62f;
		constexpr float kArrowTip = 0.80f;
		constexpr float kShaftRadius = 0.016f;
		constexpr float kHeadRadius = 0.052f;
		constexpr float kRectInner = 0.16f;
		constexpr float kRectOuter = 0.42f;
		constexpr float kRingRadius = 0.62f;
		constexpr float kRingHalfThickness = 0.028f;
		constexpr int kTubeSegments = 16;
		constexpr int kRingSegments = 48;

		const std::array<const char*, 4>& HandleNames()
		{
			static const std::array<const char*, 4> names = { "XArrow", "YArrow", "PlaneRect", "Ring" };
			return names;
		}

		/** The rectangular plane handle: the square between +u and +v, starting a little away from
		 * the pivot so the arrows stay clear. */
		void AddPlaneRect(std::vector<Eigen::Vector3f>& p_out)
		{
			const Eigen::Vector3f a(kRectInner, kRectInner, 0.0f);
			const Eigen::Vector3f b(kRectOuter, kRectInner, 0.0f);
			const Eigen::Vector3f c(kRectOuter, kRectOuter, 0.0f);
			const Eigen::Vector3f d(kRectInner, kRectOuter, 0.0f);
			AddQuad(p_out, a, b, c, d);
		}

		Eigen::Vector4<uint8_t> IdleColor(int p_handle)
		{
			switch (p_handle)
			{
			case 0: return Rgba(226, 74, 74);           // u
			case 1: return Rgba(96, 200, 96);           // v
			case 2: return Rgba(88, 132, 240, 190);     // the rectangle, translucent
			default: return Rgba(208, 150, 230);        // the rotation ring
			}
		}
	}

	class Gizmo2DWidget::Internal {
	public:
		Internal(Gizmo2DWidget* p_self) : self(p_self) {}
		~Internal() {}

		friend class Gizmo2DWidget;

		Gizmo2DWidget* self = nullptr;

		/** Where setUpOrigin put the pivot: the zero of the reported translation and rotation. */
		Eigen::Vector3f origin = Eigen::Vector3f::Zero();
		Eigen::Vector3f center = Eigen::Vector3f::Zero();
		/** The plane frame at setUpOrigin (column 0 u, 1 v, 2 normal). */
		Eigen::Matrix3f startBasis = Eigen::Matrix3f::Identity();
		/** The current frame: the ring drag rotates it about startBasis' normal. */
		Eigen::Matrix3f basis = Eigen::Matrix3f::Identity();

		bool placed = false;
		/** True once the owner supplied its own frame (setUpDir/setUpAxis), so activation must not
		 * re-centre or re-orient it behind the owner's back. */
		bool ownerFramed = false;
		float userScale = 1.0f;
		float frameScale = 0.0f;

		WidgetViewData viewData;

		GizmoAxisTranslate axisPick;
		GizmoPlaneTranslate planePick;
		GizmoAxisRotate rotatePick;

		/** The normal the current rotation drag turns about, fixed at press. */
		Eigen::Vector3f dragNormal = Eigen::Vector3f::UnitZ();
		Eigen::Vector3f dragRefDir = Eigen::Vector3f::UnitX();
		Eigen::Matrix3f dragStartBasis = Eigen::Matrix3f::Identity();
	};

	Gizmo2DWidget::Gizmo2DWidget(const std::string& name)
		: EventWidget(name), mInternal(new Internal(this)), mState(Stop)
	{
		// Local frame: +X is u, +Y is v, +Z is the plane normal. The pivot and basis are baked into
		// each face's model matrix, so every handle below is built on the unit axes.
		{
			std::vector<Eigen::Vector3f> triangles;
			AddTube(triangles, Eigen::Vector3f::Zero(), Eigen::Vector3f(kShaftEnd, 0, 0),
				kShaftRadius, kShaftRadius, kTubeSegments);
			AddTube(triangles, Eigen::Vector3f(kShaftEnd, 0, 0), Eigen::Vector3f(kArrowTip, 0, 0),
				kHeadRadius, 0.0f, kTubeSegments);
			mInternal->viewData.setTriangleFace(HandleNames()[XArrow], triangles, IdleColor(XArrow));
		}
		{
			std::vector<Eigen::Vector3f> triangles;
			AddTube(triangles, Eigen::Vector3f::Zero(), Eigen::Vector3f(0, kShaftEnd, 0),
				kShaftRadius, kShaftRadius, kTubeSegments);
			AddTube(triangles, Eigen::Vector3f(0, kShaftEnd, 0), Eigen::Vector3f(0, kArrowTip, 0),
				kHeadRadius, 0.0f, kTubeSegments);
			mInternal->viewData.setTriangleFace(HandleNames()[YArrow], triangles, IdleColor(YArrow));
		}
		{
			std::vector<Eigen::Vector3f> triangles;
			AddPlaneRect(triangles);
			mInternal->viewData.setTriangleFace(HandleNames()[PlaneRect], triangles, IdleColor(PlaneRect));
		}
		{
			std::vector<Eigen::Vector3f> triangles;
			AddRing(triangles, Eigen::Vector3f::UnitZ(), kRingRadius, kRingHalfThickness, kRingSegments);
			mInternal->viewData.setTriangleFace(HandleNames()[Ring], triangles, IdleColor(Ring));
		}

		setActive(true);
	}

	Gizmo2DWidget::~Gizmo2DWidget()
	{
		delete mInternal;
	}

	void Gizmo2DWidget::onSetActive(bool flag)
	{
		if (flag && !mInternal->ownerFramed)
		{
			// Re-centre on the next update rather than here: setActive() also runs from the
			// constructor, and the viewport this reads through is still being built then.
			mInternal->placed = false;
		}
	}

	bool Gizmo2DWidget::TryPlaceAtScene()
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

		// Default plane: facing the camera at the moment the widget appears, so an unowned gizmo
		// is usable from wherever the view happens to be. setUpDir/setUpAxis replace it.
		Eigen::Vector3f normal = Eigen::Vector3f::UnitZ();
		if (const auto* camera = m_sceneView->GetCamera())
		{
			const Maths::FVector3 eye = camera->GetPosition();
			const Eigen::Vector3f toEye(
				eye.x - mInternal->center.x(), eye.y - mInternal->center.y(), eye.z - mInternal->center.z());
			if (toEye.norm() > 1.0e-6f)
			{
				normal = toEye.normalized();
			}
		}
		BuildBasisFromNormal(normal);
		mInternal->placed = true;
		SyncTransform();
		return true;
	}

	void Gizmo2DWidget::BuildBasisFromNormal(const Eigen::Vector3f& p_normal)
	{
		Eigen::Vector3f normal = p_normal;
		if (normal.norm() < 1.0e-6f)
		{
			normal = Eigen::Vector3f::UnitZ();
		}
		normal.normalize();
		// Up vector for the u axis: the world Z unless the plane is (nearly) horizontal, where Y is
		// used instead so a +Z normal comes out as u = +X, v = +Y.
		const Eigen::Vector3f up =
			std::abs(normal.z()) < 0.9f ? Eigen::Vector3f(0, 0, 1) : Eigen::Vector3f(0, 1, 0);
		const Eigen::Vector3f u = up.cross(normal).normalized();
		const Eigen::Vector3f v = normal.cross(u).normalized();

		Eigen::Matrix3f basis;
		basis.col(0) = u;
		basis.col(1) = v;
		basis.col(2) = normal;
		mInternal->basis = basis;
		mInternal->startBasis = basis;
	}

	void Gizmo2DWidget::SyncTransform()
	{
		Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
		model.block<3, 3>(0, 0) = mInternal->basis;
		model.block<3, 1>(0, 3) = mInternal->center;
		for (TriangleFace& face : mInternal->viewData.getFaces())
		{
			face.model = model;
		}
	}

	float Gizmo2DWidget::ComputeFrameScale() const
	{
		// The FIXED_SCALE gizmo convention (200 px per local unit, see ClipPlane.cpp), scaled to
		// this widget's size: one scale for drawing and picking, so a handle sits where it is drawn.
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

	void Gizmo2DWidget::onUpdate()
	{
		if (!mInternal->placed)
		{
			TryPlaceAtScene();
		}

		mInternal->frameScale = ComputeFrameScale();
		SyncTransform();

		const float scale = mInternal->frameScale;
		Eigen::Matrix4f scaleMat = Eigen::Matrix4f::Identity();
		scaleMat(0, 0) = scaleMat(1, 1) = scaleMat(2, 2) = scale;

		const bool dragging = mState == AxisT || mState == PlaneT || mState == AxisR;
		for (int handle = 0; handle < 4; ++handle)
		{
			TriangleFace* face = nullptr;
			if (!mInternal->viewData.getTriangleFace(HandleNames()[handle], face) || face == nullptr)
			{
				continue;
			}

			Eigen::Vector4<uint8_t> color = face->color;
			if (handle == mPickHandle && mState != Stop)
			{
				// Hover cyan, active yellow - the colours the other gizmos use.
				color = dragging ? Rgba(255, 226, 64) : Rgba(32, 232, 232);
			}

			renderer->pushMatrix(face->model * scaleMat);
			renderer->drawTriangleList(face->faces, 1.0f, color);
			renderer->popMatrix();
		}

		if (mState == AxisR)
		{
			std::vector<Eigen::Vector3f> arc = mInternal->rotatePick.getRotationArc();
			if (arc.size() > 1)
			{
				// getRotationArc() sweeps the unit reference arm; put it on the ring itself.
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
			// The in-plane displacement readout: what getTranslate() reports to the owner.
			const Eigen::Vector2f delta = getTranslate();
			char text[64] = {};
			std::snprintf(text, sizeof(text), "(%.2f, %.2f)  |%.2f|",
				static_cast<double>(delta.x()), static_cast<double>(delta.y()),
				static_cast<double>(delta.norm()));
			const auto pos = renderer->getFrameParam().cursor;
			auto& render2d = Rendering::Overlay::Im2DRender::Instance();
			render2d.GetDrawList().AddText(render2d.GetFont(), 18.0f,
				{ pos.x(), pos.y() - 20 },
				Rendering::Overlay::Im2D::ColorU32(255, 255, 0, 255), text);
		}
	}

	Ray Gizmo2DWidget::MouseRay()
	{
		const ::Rendering::Geometry::Ray ray = m_sceneView->GetMouseRay();
		return Ray(
			Eigen::Vector3f(ray.origin_.x, ray.origin_.y, ray.origin_.z),
			Eigen::Vector3f(ray.direction_.x, ray.direction_.y, ray.direction_.z));
	}

	int Gizmo2DWidget::HitHandle(const Ray& p_ray)
	{
		if (mInternal->frameScale <= 0.0f)
		{
			// Input is dispatched ahead of drawing: resolve the scale here on the first frame.
			mInternal->frameScale = ComputeFrameScale();
			SyncTransform();
		}
		const std::string hit = mInternal->viewData.hitFace(p_ray, mInternal->frameScale);
		for (int handle = 0; handle < 4; ++handle)
		{
			if (hit == HandleNames()[handle])
			{
				return handle;
			}
		}
		return None;
	}

	void Gizmo2DWidget::onMouseMove()
	{
		if (m_sceneView == nullptr)
		{
			return;
		}

		// The handles may have moved since the last frame: refresh before hit-testing, so picking
		// and drawing agree within a frame.
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
				// The whole basis turns about the normal fixed at press time, applied to the basis
				// the drag started from: the frame stays orthonormal however long the drag runs.
				const float angle = mInternal->rotatePick.getRotationAngle();
				mInternal->basis = Eigen::AngleAxisf(angle, mInternal->dragNormal).toRotationMatrix()
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
			this->InvokeEvent(Gizmo2DEvent::TransformChange);
		}
	}

	void Gizmo2DWidget::onLeftMousePressed()
	{
		if (mState == Stop)
		{
			// A press can land before the first mouse-move (a click right after the widget
			// appeared): resolve the handle from the current ray instead of ignoring it.
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

		const Eigen::Vector3f u = mInternal->basis.col(0);
		const Eigen::Vector3f v = mInternal->basis.col(1);
		const Eigen::Vector3f normal = mInternal->basis.col(2);

		if (mPickHandle == XArrow || mPickHandle == YArrow)
		{
			mState = AxisT;
			mInternal->axisPick.startPick(mPickHandle == XArrow ? u : v, mInternal->center);
		}
		else if (mPickHandle == PlaneRect)
		{
			mState = PlaneT;
			const Eigen::Vector4f plane(normal.x(), normal.y(), normal.z(), -normal.dot(mInternal->center));
			mInternal->planePick.startPick(plane, mInternal->center);
		}
		else
		{
			mState = AxisR;
			mInternal->dragNormal = normal;
			mInternal->dragStartBasis = mInternal->basis;
			mInternal->dragRefDir = u;
			mInternal->rotatePick.startPick(normal, mInternal->center, mInternal->dragRefDir,
				mInternal->center + mInternal->dragRefDir);
		}
	}

	void Gizmo2DWidget::onLeftMouseReleased()
	{
		if (mState != AxisT && mState != PlaneT && mState != AxisR)
		{
			return;
		}

		mState = mPickHandle == None ? Stop : Hot;

		// Immediate invoke reports every move; otherwise the release is the one report (the same
		// contract AxisTranslationWidget has).
		if (!mImInvoke)
		{
			this->InvokeEvent(Gizmo2DEvent::TransformChange);
		}
	}

	// --- owner-facing API ---------------------------------------------------------------

	void Gizmo2DWidget::setUpOrigin(float x, float y, float z)
	{
		mInternal->origin = Eigen::Vector3f(x, y, z);
		mInternal->center = mInternal->origin;
		mInternal->startBasis = mInternal->basis;
		mInternal->placed = true;
		mInternal->ownerFramed = true;
		SyncTransform();
	}

	void Gizmo2DWidget::setUpDir(float x, float y, float z)
	{
		BuildBasisFromNormal(Eigen::Vector3f(x, y, z));
		mInternal->ownerFramed = true;
		SyncTransform();
	}

	void Gizmo2DWidget::setUpAxis(float x, float y, float z)
	{
		Eigen::Vector3f normal = mInternal->basis.col(2);
		Eigen::Vector3f u(x, y, z);
		u -= normal * u.dot(normal);
		if (u.norm() < 1.0e-6f)
		{
			return;
		}
		u.normalize();
		const Eigen::Vector3f v = normal.cross(u).normalized();

		Eigen::Matrix3f basis;
		basis.col(0) = u;
		basis.col(1) = v;
		basis.col(2) = normal;
		mInternal->basis = basis;
		mInternal->startBasis = basis;
		mInternal->ownerFramed = true;
		SyncTransform();
	}

	void Gizmo2DWidget::setUpScale(float s)
	{
		mInternal->userScale = s > 0.0f ? s : 0.001f;
	}

	Eigen::Vector2f Gizmo2DWidget::getTranslate() const
	{
		// Measured in the frame the owner set up: rotating the gizmo must not turn the numbers the
		// host already read.
		const Eigen::Vector3f delta = mInternal->center - mInternal->origin;
		return Eigen::Vector2f(
			delta.dot(mInternal->startBasis.col(0)),
			delta.dot(mInternal->startBasis.col(1)));
	}

	float Gizmo2DWidget::getTranslateLength() const
	{
		return getTranslate().norm();
	}

	float Gizmo2DWidget::getRotateAngle() const
	{
		// Signed angle about the (fixed) plane normal, from the start u to the current u.
		const Eigen::Vector3f u0 = mInternal->startBasis.col(0);
		const Eigen::Vector3f u = mInternal->basis.col(0);
		const Eigen::Vector3f normal = mInternal->startBasis.col(2);
		return std::atan2(normal.dot(u0.cross(u)), u0.dot(u)) * 180.0f / kPi;
	}

	Eigen::Matrix4f Gizmo2DWidget::getTransform() const
	{
		Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
		transform.block<3, 3>(0, 0) = mInternal->basis;
		transform.block<3, 1>(0, 3) = mInternal->center;
		return transform;
	}

	void Gizmo2DWidget::setTranslate(float u, float v)
	{
		mInternal->center = mInternal->origin
			+ mInternal->startBasis.col(0) * u
			+ mInternal->startBasis.col(1) * v;
		SyncTransform();
	}

	void Gizmo2DWidget::setRotate(float p_angleDegree)
	{
		const float radians = p_angleDegree * kPi / 180.0f;
		mInternal->basis = Eigen::AngleAxisf(radians, mInternal->startBasis.col(2)).toRotationMatrix()
			* mInternal->startBasis;
		SyncTransform();
	}
}
