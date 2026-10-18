#pragma once
#include "Widgets/PropertyQtWidgets.h"
#include "Widgets/numberwidget.h"
#include <Maths/FVector2.h>
#include <QWidget>
#include <QHBoxLayout>

namespace MOON {
Q_DECLARE_METATYPE(Maths::FVector2);

// The two component editor of a float2 material parameter, the same
// NumberWidgets Fvec3/Fvec4 use for their components (x / y).
class Fvec2 : public PropertyQtWidget
{
    Q_OBJECT
public:
    explicit Fvec2(QWidget* parent, WidgetProperty* prop);
    explicit Fvec2(QWidget* parent);
    void setVec2Value(float x, float y);
    void setVec2Value(const Maths::FVector2& vec);
    Maths::FVector2 getVec2Value() const;
    virtual QVariant widgetValue() override;
    virtual void setWidgetValue(const QVariant& value) override;
    float x() const;
    float y() const;

public Q_SLOTS:
    void onValueChange();
private:
    NumberWidget* m_spinX;
    NumberWidget* m_spinY;
};

}
