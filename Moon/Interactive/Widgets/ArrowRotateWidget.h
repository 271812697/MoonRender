#pragma once
#include "Interactive/EventWidget.h"
namespace MOON
{
	enum ArrowRotateEvent{
		AngleChange=1001,
		ArrowRotateEventNone
	};
	class ArrowRotateWidget: public EventWidget
	{
	public:
		ArrowRotateWidget(const std::string& name);
		virtual ~ArrowRotateWidget();
		virtual void onUpdate()override;
		void setUpRotateCenter(float x,float y,float z );
		void setUpRotateAxis(float x, float y, float z);
		void setUpOriginPos(float x, float y, float z);
		void setUpScale(float s);
		float getAngle();
		void setAngle(float len);
		
		enum IntersectiveState
		{
			Stop,
			Hot,
			Rotate
		};
		virtual void onLeftMousePressed()override;
		virtual void onLeftMouseReleased()override;
		virtual void onMouseMove()override;
		/** Hot or dragging: the cursor belongs to this handle, not to what is behind it (see
		 * EventWidget::IsUsingMouse). */
		bool IsUsingMouse() const override { return mState != Stop; }
	private:
		class Internal;
		Internal* mInternal = nullptr;
		IntersectiveState mState;
	};
}