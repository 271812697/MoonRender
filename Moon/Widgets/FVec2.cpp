#include "Widgets/FVec2.h"
#include "Widgets/Property.h"
namespace MOON {
Fvec2::Fvec2(QWidget* parent, WidgetProperty* prop) : PropertyQtWidget(parent, prop) {
    m_spinX = new NumberWidget(this);
    m_spinY = new NumberWidget(this);

    for (auto* spin : {m_spinX, m_spinY}) {
        spin->setRange(-9999.99, 9999.99);
        spin->setIncrement(0.1);
        QSizePolicy sp = spin->sizePolicy();
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        spin->setSizePolicy(sp);
    }
    m_spinX->setPrefix("x");
    m_spinY->setPrefix("y");

    QHBoxLayout* hLayout = new QHBoxLayout(this);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(2);
    hLayout->addWidget(m_spinX);
    hLayout->addWidget(m_spinY);

    QObject::connect(m_spinX, &NumberWidget::valueChanged, this, &Fvec2::onValueChange);
    QObject::connect(m_spinY, &NumberWidget::valueChanged, this, &Fvec2::onValueChange);
}

Fvec2::Fvec2(QWidget* parent) : Fvec2(parent, nullptr) {
}

void Fvec2::setVec2Value(float x, float y) {
    m_spinX->initValue(x);
    m_spinY->initValue(y);
}

void Fvec2::setVec2Value(const Maths::FVector2& vec) {
    setVec2Value(vec.x, vec.y);
}

void Fvec2::setWidgetValue(const QVariant& value) {
    setVec2Value(value.value<Maths::FVector2>());
}

QVariant Fvec2::widgetValue() {
    return QVariant::fromValue(getVec2Value());
}

Maths::FVector2 Fvec2::getVec2Value() const {
    return Maths::FVector2(static_cast<float>(m_spinX->getValue()),
                           static_cast<float>(m_spinY->getValue()));
}

float Fvec2::x() const {
    return static_cast<float>(m_spinX->getValue());
}

float Fvec2::y() const {
    return static_cast<float>(m_spinY->getValue());
}

void Fvec2::onValueChange() {
    OnValueChanged();
}

}
