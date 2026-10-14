#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/Widgets/DrawSketchDefaultHandler.h"
#include "Interactive/Widgets/CircleEllipseConstructionMethod.h"
namespace MOON
{

	class DrawSketchHandlerArc : public DrawSketchDefaultHandler<DrawSketchHandlerArc, StateMachines::ThreeSeekEnd,3, CircleEllipseConstructionMethod>
	{
	public:
		DrawSketchHandlerArc(const std::string& name);
		virtual ~DrawSketchHandlerArc();
		virtual void onUpdate()override;
		virtual void onSetActive(bool flag)override;
		virtual void onConstructionMethodChanged()override;

		virtual void updateDataAndDrawToPosition(Base::Vector2d onSketchPos)override;
		//virtual void onButtonPressed(Base::Vector2d onSketchPos) override;
		bool canGoToNextMode() override;
		void createShape(bool onlyeditoutline) override;

		/** The numbers this tool can be given instead of positions - FreeCAD's on-view
		 * parameters: the centre and the two angles for the centre method, the three rim
		 * points for the other one. */
		virtual void enforceControlParameters(Base::Vector2d& onSketchPos) override;
		virtual void adaptParameters(Base::Vector2d onSketchPos) override;
		virtual SelectMode stateOfOnViewParameter(int p_index) const override;
	private:
		/** The boxes of the construction method the tool is in, with the label each one
		 * carries (see OnViewParameters). */
		void configureOnViewParameters();
		/** Where the boxes of the current step sit on screen: next to the point the step
		 * is about, stacked so that they do not cover each other. */
		void updateOnViewParameterPositions();
		/** Shows a value in a box that the user has not typed in himself. */
		void setValueIfUnset(int p_index, double p_value);
		/** Builds the geometry again from the cursor's place with the typed values in
		 * charge, the way a mouse move would (typing has to show right away). */
		void redrawFromControlParameters();

		class DrawSketchHandlerArcInternal;
		DrawSketchHandlerArcInternal* m_internal = nullptr;
	};
}
