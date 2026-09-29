#pragma once
#include "Widgets/Property.h"
#include "Widgets/sliderwidget.h"
namespace MOON {
	class SliderIntProperty :public WidgetProperty {
	public:
		SliderIntProperty(const QString& n, PropertyComponent* comp);
		~SliderIntProperty();
		void setMinMax(int a, int b);
		void setIncrement(int value);
		virtual PropertyQtWidget* createEditorWidget(QWidget* parent = nullptr)override;
	private:
		int minA = -10;
		int maxB = 10;
		int increment = 1;
	};

}
