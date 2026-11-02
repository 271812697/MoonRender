#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/MathUtil/MathUtil.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace MOON
{
	/** What a Gizmo2DWidget drag produced. The immediate-vs-release contract is the inherited
	 * ImmediateInvoke flag, the same one AxisTranslationWidget uses. */
	enum Gizmo2DEvent
	{
		TransformChange = 1102,
		Gizmo2DEventNone
	};

	/** A gizmo that lives in one plane: two axis arrows (the plane's u and v), a rectangular
	 * handle that drags inside the plane, and a ring that rotates about the plane normal.
	 *
	 * The interaction is ClipPlane's state machine; drawing and picking are
	 * AxisTranslationWidget's lower layers - WidgetViewData triangle lists and the camera ray
	 * against them - shared with Gizmo3DWidget through GizmoGeometry.h.
	 *
	 * The plane itself comes from the owner: setUpDir is the normal, setUpAxis the in-plane X.
	 * With neither called, an activation puts the plane at the scene centre facing the camera, so
	 * the widget is usable (and testable) without a host. */
	class Gizmo2DWidget : public EventWidget
	{
	public:
		enum HandleId
		{
			XArrow,
			YArrow,
			PlaneRect,
			Ring,
			None
		};

		enum IntersectiveState
		{
			Stop,
			Hot,
			AxisT,
			PlaneT,
			AxisR
		};

		Gizmo2DWidget(const std::string& name);
		virtual ~Gizmo2DWidget();

		virtual void onUpdate() override;
		virtual void onSetActive(bool flag) override;
		virtual void onLeftMousePressed() override;
		virtual void onLeftMouseReleased() override;
		virtual void onMouseMove() override;

		// --- what the owner sets up (AxisTranslationWidget's setUpOrigin/setUpDir/setUpScale) ---

		/** Places the pivot; the zero of getTranslate() and getRotateAngle(). */
		void setUpOrigin(float x, float y, float z);
		/** The plane's normal (the rotation axis and the plane-drag normal). */
		void setUpDir(float x, float y, float z);
		/** The in-plane X direction; orthogonalized against the normal. Without it, u is derived
		 * from the normal and the world up. */
		void setUpAxis(float x, float y, float z);
		/** Extra multiplier on top of the screen-constant size (1 is the default). */
		void setUpScale(float s);

		// --- what the owner reads back / drives ---

		/** Displacement in the plane, in the frame setUpOrigin/setUpAxis established. */
		Eigen::Vector2f getTranslate() const;
		float getTranslateLength() const;
		/** Rotation about the plane normal, in degrees, signed (right-handed about the normal). */
		float getRotateAngle() const;
		/** Pivot + basis: column 0 is u, 1 is v, 2 is the normal. */
		Eigen::Matrix4f getTransform() const;

		void setTranslate(float u, float v);
		void setRotate(float p_angleDegree);

		IntersectiveState GetState() const { return mState; }
		int GetHoveredHandle() const { return mPickHandle; }

	private:
		/** Writes pivot + basis into every handle's model matrix, so drawing and picking see the
		 * same transform (hitFace multiplies it by the frame scale, the draw path pushes it). */
		void SyncTransform();
		/** Puts the pivot at the scene bounds centre and the plane facing the camera; called once
		 * per activation while no owner has set a frame. */
		bool TryPlaceAtScene();
		/** World units per local unit this frame (the FIXED_SCALE camera ratio times the size). */
		float ComputeFrameScale() const;
		Ray MouseRay();
		int HitHandle(const Ray& p_ray);
		/** u/v/n from a normal, picking an up vector that is not parallel to it. */
		void BuildBasisFromNormal(const Eigen::Vector3f& p_normal);

		class Internal;
		Internal* mInternal = nullptr;
		IntersectiveState mState;
		int mPickHandle = None;
	};
}
