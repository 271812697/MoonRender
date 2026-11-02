#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/MathUtil/MathUtil.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace MOON
{
	/** What the gizmo tells its owner while/after a drag. The immediate-vs-release choice is the
	 * inherited ImmediateInvoke flag (see AxisTranslationWidget, which uses the same pattern). */
	enum Gizmo3DEvent
	{
		TransformChange = 1101,
		Gizmo3DEventNone
	};

	/** A full 3D transform gizmo: three axis arrows, three plane handles and three rotation rings.
	 *
	 * The interaction is ClipPlane's, handle for handle: hover a handle, press to start a drag,
	 * move to drive it, release to go back to hover. What changes is the two lower layers, both
	 * taken from AxisTranslationWidget:
	 *   - drawing: WidgetViewData triangle lists through drawTriangleList (no gizmo shader, no
	 *     PolygonMesh), so the handles are plain triangles owned by this widget;
	 *   - picking: the camera ray against those triangles (WidgetViewData::hitFace), no GPU
	 *     polygon selection.
	 *
	 * Externally it exposes what the drags produced, in the same style as AxisTranslationWidget:
	 * setUpOrigin / getTranslate for the displacement, getRotation / getRotateAngle + axis for the
	 * rotation, and a TransformChange event for both. */
	class Gizmo3DWidget : public EventWidget
	{
	public:
		/** Same handles (and order) as ClipPlane's table, so the two read the same way. */
		enum HandleId
		{
			XArrow,
			YArrow,
			ZArrow,
			XPlane,
			YPlane,
			ZPlane,
			XRing,
			YRing,
			ZRing,
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

		Gizmo3DWidget(const std::string& name);
		virtual ~Gizmo3DWidget();

		virtual void onUpdate() override;
		virtual void onSetActive(bool flag) override;
		virtual void onLeftMousePressed() override;
		virtual void onLeftMouseReleased() override;
		virtual void onMouseMove() override;

		// --- what the owner sets up (AxisTranslationWidget's setUpOrigin/setUpDir/setUpScale) ---

		/** Places the pivot and makes it the zero of getTranslate(). */
		void setUpOrigin(float x, float y, float z);
		/** Orients the gizmo's local +Z along the given direction (X/Y are rebuilt orthonormally). */
		void setUpDir(float x, float y, float z);
		/** Extra multiplier on top of the screen-constant size (1 is the default). */
		void setUpScale(float s);

		// --- what the owner reads back / drives ---

		/** Displacement the handles applied since setUpOrigin, in world space. */
		Eigen::Vector3f getTranslate() const;
		/** Rotation the rings applied since setUpOrigin. */
		Eigen::Quaternionf getRotation() const;
		/** The same rotation split into degrees + unit axis (what the on-screen readout shows). */
		float getRotateAngle() const;
		Eigen::Vector3f getRotateAxis() const;
		/** Pivot + basis: what a host pushes onto the object the gizmo drives. */
		Eigen::Matrix4f getTransform() const;

		void setTranslate(float x, float y, float z);
		void setRotate(float p_angleDegree, float x, float y, float z);

		IntersectiveState GetState() const { return mState; }
		int GetHoveredHandle() const { return mPickHandle; }

	private:
		/** Writes pivot + basis into every handle's model matrix, so drawing and picking see the
		 * same transform (hitFace multiplies it by the frame scale, the draw path pushes it). */
		void SyncTransform();
		/** Tries to put the gizmo at the scene bounds centre; called once per activation. */
		bool TryPlaceAtScene();
		/** World units per local unit for this frame: the camera-distance ratio the FIXED_SCALE
		 * gizmo shader uses, times the gizmo's own size. */
		float ComputeFrameScale() const;
		/** The camera ray at the cursor, as the ViewData hit test wants it. */
		Ray MouseRay();
		/** Handle index under the ray, or None. */
		int HitHandle(const Ray& p_ray);
		/** Current rotation magnitude in degrees (0..180), derived from basis vs startBasis. */
		float GetDragAngleDegree() const;

		class Internal;
		Internal* mInternal = nullptr;
		IntersectiveState mState;
		int mPickHandle = None;
	};
}
