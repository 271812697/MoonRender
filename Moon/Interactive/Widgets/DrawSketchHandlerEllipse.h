#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/Widgets/DrawSketchDefaultHandler.h"
#include "Interactive/Widgets/CircleEllipseConstructionMethod.h"
namespace MOON
{
	class DrawSketchHandlerEllipse : public DrawSketchDefaultHandler<DrawSketchHandlerEllipse, StateMachines::ThreeSeekEnd, 3, CircleEllipseConstructionMethod>
	{
		using SupperClass = DrawSketchDefaultHandler<DrawSketchHandlerEllipse, StateMachines::ThreeSeekEnd, 3, CircleEllipseConstructionMethod>;
	public:
		DrawSketchHandlerEllipse(const std::string& name);
		virtual ~DrawSketchHandlerEllipse();
		virtual void onUpdate()override;
		virtual void onSetActive(bool flag)override;

		virtual void updateDataAndDrawToPosition(Base::Vector2d onSketchPos)override;
		void executeCommands() override;
		bool canGoToNextMode() override;
		void createShape(bool onlyeditoutline) override;
		void onReset() override;
	private:
		void calculateMajorAxisParameters();
		void calculateThroughPointMinorAxisParameters(const Base::Vector2d& onSketchPos);

		class DrawSketchHandlerEllipseInternal;
		DrawSketchHandlerEllipseInternal* m_internal = nullptr;
	};
}
