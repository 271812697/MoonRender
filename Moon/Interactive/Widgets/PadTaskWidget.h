#pragma once
#include "Interactive/EventWidget.h"
namespace MOON
{
	enum PadTaskEvent{
		LengthChange=1001,
		AngleChange,
		// Not "None": SelectEvent (core/SelectionManager.h) already has one of those in this
		// namespace, and the two headers meet in the same translation unit.
		NoEvent
	};
	class PadTaskWidget: public EventWidget
	{
	public:
		PadTaskWidget(const std::string& name);
		virtual ~PadTaskWidget();
		virtual void onUpdate()override;
		void setUpOrigin(float x,float y,float z );
		void setUpDir(float x, float y, float z);
		void setUpXAxis(float x,float y,float z);
		void setUpYAxis(float x, float y, float z);
		float getLength();
		float getAngle();
		void setLength(float len);
		void setAngle(float degree);
		enum IntersectiveState
		{
			Stop,
			Hot,
			AxisT,
			Rotate
		};
		virtual void onLeftMousePressed()override;
		virtual void onLeftMouseReleased()override;
		virtual void onMouseMove()override;
		/** Hot or dragging: the cursor belongs to this handle, not to the face behind it. */
		bool IsUsingMouse() const override { return mState != Stop; }
	private:
		class Internal;
		Internal* mInternal = nullptr;
		IntersectiveState mState;
	};
}