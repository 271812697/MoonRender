#include "editor/UI/TaskPanel/ExtrudeTaskDialog.h"

#include "editor/UI/TaskPanel/PadInteraction.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/EnumProperty.h"

namespace MOON {
    namespace
    {
        QString modeName() { return "Extrude:Extrude Type"; }
        QString directionName() { return "Extrude:Extrude Direction"; }
        QString length1Name() { return "Extrude:Length 1"; }
        QString angle1Name() { return "Extrude:Angle 1"; }
        QString length2Name() { return "Extrude:Length 2"; }
        QString angle2Name() { return "Extrude:Angle 2"; }
    }

    ExtrudeTaskDialog::ExtrudeTaskDialog(std::unique_ptr<PadInteraction> p_pad, QWidget* parent)
        : ParamTaskDialog(parent), m_pad(std::move(p_pad))
    {
        PropertyComponent* p = addGroupParam("Extrude");

        EnumProperty* mode = new EnumProperty("Extrude Type", p);
        mode->setInitIndex(m_pad ? m_pad->GetChoice(PadInteraction::Mode) : 0);
        addParam(mode);
        EnumProperty* dir = new EnumProperty("Extrude Direction", p);
        dir->setInitIndex(m_pad ? m_pad->GetChoice(PadInteraction::Direction) : 0);
        addParam(dir);

        m_length1 = new SliderFloatProperty("Length 1", p);
        m_length1->setMinMax(-500, 500);
        m_length1->setStep(0.1);
        addParam(m_length1);
        m_angle1 = new SliderFloatProperty("Angle 1", p);
        m_angle1->setMinMax(-90, 90);
        m_angle1->setStep(1.0);
        addParam(m_angle1);

        m_length2 = new SliderFloatProperty("Length 2", p);
        m_length2->setMinMax(-500, 500);
        m_length2->setStep(0.1);
        addParam(m_length2);
        m_angle2 = new SliderFloatProperty("Angle 2", p);
        m_angle2->setMinMax(-90, 90);
        m_angle2->setStep(1.0);
        addParam(m_angle2);

        buildUi();

        if (m_pad)
        {
            // The handles are the interaction's, and it is the interaction that notices when one
            // moves; the panel only has to put the number it now holds on screen.
            m_pad->SetParamChangedCallback([this](PadInteraction::Param p_param) {
                refresh(p_param);
            });
        }
    }

    BaseTaskDialog* MakePadPanel(std::unique_ptr<FeatureInteraction> p_interaction)
    {
        // The offer promised a pad, so the interaction is one: taking it over as such is the only
        // cast in the arrangement, and it happens once, where the panel is made.
        auto pad = std::unique_ptr<PadInteraction>(
            static_cast<PadInteraction*>(p_interaction.release()));
        return new ExtrudeTaskDialog(std::move(pad));
    }

    ExtrudeTaskDialog::~ExtrudeTaskDialog()
    {
        // The interaction goes with the panel: its handles leave the screen with it.
    }

    QVariant ExtrudeTaskDialog::getParamValue(const QString& propertyName)
    {
        if (!m_pad)
        {
            return QVariant();
        }
        if (propertyName == modeName()) {
            QList<QString>list = { "Length", "Through All", "UpToFace" };
            return QVariant::fromValue(list);
        }
        if (propertyName == directionName()) {
            QList<QString>list = { "Forward", "Reverse", "Double","Sysmetric" };
            return QVariant::fromValue(list);
        }
        if (propertyName == length1Name()) {
            return QVariant::fromValue(m_pad->GetNumber(PadInteraction::Length1));
        }
        if (propertyName == angle1Name()) {
            return QVariant::fromValue(m_pad->GetNumber(PadInteraction::Angle1));
        }
        if (propertyName == length2Name()) {
            return QVariant::fromValue(m_pad->GetNumber(PadInteraction::Length2));
        }
        if (propertyName == angle2Name()) {
            return QVariant::fromValue(m_pad->GetNumber(PadInteraction::Angle2));
        }
        return QVariant();
    }

    void ExtrudeTaskDialog::setParamValue(const QString& propertyName, const QVariant& value)
    {
        if (!m_pad)
        {
            return;
        }
        if (propertyName == modeName()) {
            m_pad->SetChoice(PadInteraction::Mode, value.value<int>());
        }
        else if (propertyName == directionName()) {
            m_pad->SetChoice(PadInteraction::Direction, value.value<int>());
        }
        else if (propertyName == length1Name()) {
            m_pad->SetNumber(PadInteraction::Length1, value.toDouble());
        }
        else if (propertyName == angle1Name()) {
            m_pad->SetNumber(PadInteraction::Angle1, value.toDouble());
        }
        else if (propertyName == length2Name()) {
            m_pad->SetNumber(PadInteraction::Length2, value.toDouble());
        }
        else if (propertyName == angle2Name()) {
            m_pad->SetNumber(PadInteraction::Angle2, value.toDouble());
        }
    }

    void ExtrudeTaskDialog::refresh(PadInteraction::Param p_param)
    {
        if (!m_pad)
        {
            return;
        }
        switch (p_param)
        {
        case PadInteraction::Length1:
            if (m_length1) { m_length1->updateWidgetValue(m_pad->GetNumber(p_param)); }
            break;
        case PadInteraction::Angle1:
            if (m_angle1) { m_angle1->updateWidgetValue(m_pad->GetNumber(p_param)); }
            break;
        case PadInteraction::Length2:
            if (m_length2) { m_length2->updateWidgetValue(m_pad->GetNumber(p_param)); }
            break;
        case PadInteraction::Angle2:
            if (m_angle2) { m_angle2->updateWidgetValue(m_pad->GetNumber(p_param)); }
            break;
        default:
            break;
        }
    }

    void ExtrudeTaskDialog::clickOk()
    {
        if (m_pad)
        {
            m_pad->Commit();
        }
    }

    void ExtrudeTaskDialog::clickApply()
    {
    }

    void ExtrudeTaskDialog::clickCancel()
    {
        if (m_pad)
        {
            m_pad->Abort();
        }
    }
}
