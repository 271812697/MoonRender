#include "Widgets/SliderIntProperty.h"
#include "Widgets/PropertyComponent.h"
namespace MOON {
	SliderIntProperty::SliderIntProperty(const QString& n, PropertyComponent* comp) :WidgetProperty(n, comp) {

	}
	SliderIntProperty::~SliderIntProperty() {

	}
	void SliderIntProperty::setMinMax(int a, int b) {
		minA = a;
		maxB = b;
	}
	void SliderIntProperty::setIncrement(int value) {
		increment = value;
	}
	PropertyQtWidget* SliderIntProperty::createEditorWidget(QWidget* parent ) {
		if (mWidget == nullptr) {
			auto widget = new IntSliderWidgetQt(parent);
			mWidget = widget;
			widget->setProp(this);
			// Range before value - see the note in SliderFloatProperty: a number box
			// clamps to its range as it is given the value, and out-of-range values
			// were being cut down by Qt's default range.
			widget->setMinValue(minA);
			widget->setMaxValue(maxB);
			widget->setIncrement(increment);
			widget->setValue(owner->getPropertyValue(mName).toInt());
		}
		return mWidget;
	}
}
