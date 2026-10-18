#include "Widgets/Vec2Property.h"
#include "Widgets/PropertyComponent.h"
namespace MOON {
    Vec2Property::Vec2Property(const QString& n, PropertyComponent* comp) : WidgetProperty(n, comp) {
    }

    Vec2Property::~Vec2Property() {
    }

    PropertyQtWidget* Vec2Property::createEditorWidget(QWidget* parent) {
        if (mWidget == nullptr) {
            mWidget = new Fvec2(parent, this);
            updateWidgetValue(owner->getPropertyValue(mName));
        }
        return mWidget;
    }
}
