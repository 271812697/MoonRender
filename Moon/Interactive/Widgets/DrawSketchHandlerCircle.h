#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/Widgets/DrawSketchDefaultHandler.h"
#include "Interactive/Widgets/CircleEllipseConstructionMethod.h"
namespace MOON
{
	class DrawSketchHandlerCircle: public DrawSketchDefaultHandler<DrawSketchHandlerCircle, StateMachines::ThreeSeekEnd,3, CircleEllipseConstructionMethod>
	{
	public:
		DrawSketchHandlerCircle(const std::string& name);
		virtual ~DrawSketchHandlerCircle();
		virtual void onUpdate()override;
		virtual void onSetActive(bool flag)override;
		virtual void onConstructionMethodChanged()override;

		virtual void updateDataAndDrawToPosition(Base::Vector2d onSketchPos)override;
		virtual void onButtonPressed(Base::Vector2d onSketchPos) override;
		bool canGoToNextMode() override;
		void createShape(bool onlyeditoutline) override;

		/** The numbers the circle can be given instead of positions - FreeCAD's on-view
		 * parameters: the centre and the radius, or the three rim points. */
		virtual void enforceControlParameters(Base::Vector2d& onSketchPos) override;
		virtual void adaptParameters(Base::Vector2d onSketchPos) override;
		virtual SelectMode stateOfOnViewParameter(int p_index) const override;
		/** A centre and a radius is a circle: the centre method is done with its second
		 * step, although the machine has a third one for the three rim points. */
		virtual bool isLastControlStep() const override;
	private:
		/** The boxes of the construction method the tool is in (see OnViewParameters). */
		void configureOnViewParameters();
		/** Where the boxes sit on screen: next to the point the step is about. */
		void updateOnViewParameterPositions();
		/** Shows a value in a box that the user has not typed in himself. */
		void setValueIfUnset(int p_index, double p_value);
		/** Builds the geometry again from the cursor's place with the typed values in
		 * charge, the way a mouse move would. */
		void redrawFromControlParameters();

		class DrawSketchHandlerCircleInternal;
		DrawSketchHandlerCircleInternal* m_internal = nullptr;
	};
}
