#include "Widgets/SliderFloatProperty.h"
#include "Widgets/PropertyComponent.h"
#include "Widgets/sliderwidget.h"
namespace MOON {
	SliderFloatProperty::SliderFloatProperty(const QString& n, PropertyComponent* comp) :WidgetProperty(n, comp) {

	}
	SliderFloatProperty::SliderFloatProperty(const QString& n, PropertyComponent* comp, float a, float b):WidgetProperty(n, comp),minA(a),maxB(b)
	{
	}
	SliderFloatProperty::~SliderFloatProperty() {

	}
	void SliderFloatProperty::setMinMax(float a, float b)
	{	
		minA = a;
		maxB = b;
		if (mWidget) {

			auto widget = dynamic_cast<FloatSliderWidgetQt*>(mWidget);
			widget->setMinValue(a);
			widget->setMaxValue(b);
		}
	}
	void SliderFloatProperty::setStep(float step)
	{
		if (mWidget) {
			this->step = step;
			auto widget = dynamic_cast<FloatSliderWidgetQt*>(mWidget);
			widget->setIncrement(step);
		}
	}
	PropertyQtWidget* SliderFloatProperty::createEditorWidget(QWidget* parent) {
		if (mWidget == nullptr) {
			auto widget = new FloatSliderWidgetQt(parent);
			mWidget = widget;
			widget->setProp(this);
			// The range has to be in place before the value: the number box starts
			// out with Qt's default range (0 .. 99.99) and clamps whatever it is
			// given, so a negative value set first came out as 0 and stayed 0 when
			// the range was applied afterwards.
			widget->setMinValue(minA);
			widget->setMaxValue(maxB);
			widget->setIncrement(step);
			widget->setValue(owner->getPropertyValue(mName).toFloat());
			
		}
		return mWidget;
	}
}
