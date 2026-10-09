#include "editor/UI/TaskPanel/PolarPatternTask.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/component/TopoShapeActor.h"
#include "feature/FeatureBody.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/SliderIntProperty.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/BoolProperty.h"
#include "editor/UI/TaskPanel/OriginalsList.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"

#include <BRepAdaptor_Curve.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <exception>

namespace MOON {
    namespace
    {
        // What the panel offers for the pattern.
        constexpr int kMaxOccurrences = 48;
        constexpr float kMaxAngle = 360.0f;
    }

    class PolarPatternTask::Internal {
    public:
        Internal(PolarPatternTask* pad, TransformMode type) :self(pad), polarType(type) {
            auto f = self->getFeature();
            if (f) {
                feature = dynamic_cast<PolarPatternFeature*>(f);
                return;
            }

            feature = new PolarPatternFeature("PolarPattern");
            feature->mode = static_cast<int>(polarType);
            self->setFeature(feature);
            isCreateFeature = true;

            // The whole mode patterns the shape of the last feature of the body, so
            // a selection is only needed to say which body that is; what is picked
            // inside it matters for the axis alone.
            Feature* baseFeature = nullptr;
            std::vector<std::string>subValues;
            ViewTool::getSelectedBasedFeature(baseFeature, subValues);
            if (baseFeature) {
                feature->setBaseFeature(baseFeature);
                feature->setSubValues(subValues);
            }
            else {
                FeatureBody::Active()->setBaseFeatureFor(feature);
            }

            // A circular pattern normally turns inside the plane of the sketch the
            // body was built from, so start on the normal axis of that sketch.
            feature->applySketchAxis(feature->axisType);
            AddBaseFeatureAsDefaultOriginal(*feature, feature->mode, feature->originals);
        }
        ~Internal() {
        }

    private:
        /** True when the pattern takes material away rather than adding it: a
         * pocket, a groove or a fillet that cut into the shape. */
        bool isSubtractivePattern() const
        {
            if (feature == nullptr
                || feature->mode != static_cast<int>(TransformMode::Feature)) {
                // The whole shape mode repeats the shape below as it is, which is
                // never a cut.
                return false;
            }
            for (Feature* original : feature->originals) {
                if (original != nullptr && original->isToolSubtractive()) {
                    return true;
                }
            }
            return false;
        }

        PolarPatternTask* self;
        TransformMode polarType;
        friend PolarPatternTask;
        bool isCreateFeature = false;
        PolarPatternFeature* feature = nullptr;
        EnumProperty* modeProp = nullptr;
        EnumProperty* axisProp = nullptr;
        SliderIntProperty* occurrencesProp = nullptr;
        SliderFloatProperty* angleProp = nullptr;
        BoolProperty* reverseProp = nullptr;
    };
    PolarPatternTask::PolarPatternTask(QWidget* parent, TransformMode type, Feature* feature)
        : ParamTaskDialog(parent), ShapeHelper(feature), mInternal(new Internal(this, type))
    {
        setGenerateShapeName("PolarShape");

        mPreviewOption.isTransparent = false;
        mPreviewOption.isBlend = true;
        mPreviewOption.useDomainColor = false;
        mPreviewOption.g = 0.0f;
        mPreviewOption.b = 0.0f;

        PolarPatternFeature* pattern = mInternal->feature;
        PropertyComponent* p = addGroupParam("Polar");

        mInternal->modeProp = new EnumProperty("Polar Type", p);
        mInternal->modeProp->setInitIndex(pattern ? pattern->mode : 0);
        addParam(mInternal->modeProp);

        mInternal->axisProp = new EnumProperty("Axis", p);
        mInternal->axisProp->setInitIndex(
            pattern ? pattern->axisType : PolarPatternFeature::AxisSketchNormal);
        addParam(mInternal->axisProp);

        mInternal->occurrencesProp = new SliderIntProperty("Occurrences", p);
        mInternal->occurrencesProp->setMinMax(1, kMaxOccurrences);
        addParam(mInternal->occurrencesProp);

        mInternal->angleProp = new SliderFloatProperty("Angle", p);
        mInternal->angleProp->setMinMax(1.0f, kMaxAngle);
        mInternal->angleProp->setStep(1.0f);
        addParam(mInternal->angleProp);

        mInternal->reverseProp = new BoolProperty("Reverse", p);
        addParam(mInternal->reverseProp);

        // The features the pattern works on when it runs in its "feature" mode.
        addGroupParam("Originals");

        buildUi();
        if (pattern) {
            const auto it = groupToIndex.find("Originals");
            if (it != groupToIndex.end()) {
                auto* group = m_comps[it->second].first;
                group->setCollapsed(false);
                group->addSubWidget(new OriginalsList(
                    *pattern,
                    pattern->originals,
                    [this]() { refreshPreview(); },
                    this));
            }
        }
        refreshPreview();
    }
    void PolarPatternTask::refreshPreview()
    {
        // A pattern that takes material away - a pocket, a hole - happens inside the
        // shape below it, so its preview is drawn the way the pocket draws: blended,
        // without a depth test, so the body cannot hide it. A pattern that adds
        // material keeps the transparent preview the other modeling tools use.
        const bool subtractive = mInternal->isSubtractivePattern();
        mPreviewOption.isTransparent = !subtractive;
        mPreviewOption.isBlend = true;
        previewShape();
    }
    QVariant PolarPatternTask::getParamValue(const QString& propertyName)
    {
        if (mInternal->feature == nullptr) {
            return QVariant();
        }
        if (propertyName == "Polar:Polar Type") {
            QList<QString>list = { "Whole", "Feature" };
            return QVariant::fromValue(list);
        }
        else if (propertyName == "Polar:Axis") {
            QList<QString>list = { "Sketch X", "Sketch Y", "Sketch Normal", "Select Edge" };
            return QVariant::fromValue(list);
        }
        else if (propertyName == "Polar:Occurrences") {
            return QVariant::fromValue(mInternal->feature->occurrences);
        }
        else if (propertyName == "Polar:Angle") {
            return QVariant::fromValue(mInternal->feature->angle);
        }
        else if (propertyName == "Polar:Reverse") {
            return QVariant::fromValue(mInternal->feature->reverse);
        }
        return QVariant();
    }

    void PolarPatternTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        PolarPatternFeature* feature = mInternal->feature;
        if (feature == nullptr) {
            return;
        }

        bool updatePreView = false;
        if (propertyName == "Polar:Polar Type") {
            feature->mode = value.value<int>();
            updatePreView = true;
        }
        else if (propertyName == "Polar:Axis") {
            const int axisType = value.value<int>();
            feature->axisType = axisType;
            if (axisType != PolarPatternFeature::AxisPickedEdge
                && !feature->applySketchAxis(axisType)) {
                CORE_WARN(
                    "[PolarPattern] {0}: there is no sketch below it to take the axis "
                    "from, pick an edge instead",
                    feature->GetName());
            }
            updatePreView = true;
        }
        else if (propertyName == "Polar:Occurrences") {
            feature->occurrences = std::max(value.toInt(), 1);
            updatePreView = true;
        }
        else if (propertyName == "Polar:Angle") {
            feature->angle = value.toFloat();
            updatePreView = true;
        }
        else if (propertyName == "Polar:Reverse") {
            feature->reverse = value.toBool();
            updatePreView = true;
        }

        if (updatePreView && hasInitUi) {
            refreshPreview();
        }
    }
    PolarPatternTask::~PolarPatternTask()
    {
        delete mInternal;
    }
    void PolarPatternTask::clickOk()
    {
        generateFinalShape();
    }
    void PolarPatternTask::clickApply()
    {
    }
    void PolarPatternTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreateFeature && mInternal->feature) {
            mInternal->feature->RemoveFromScene();
            delete mInternal->feature;
            mInternal->feature = nullptr;
        }
    }
    void PolarPatternTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
    {
        PolarPatternFeature* feature = mInternal->feature;
        if (feature == nullptr || feature->axisType != PolarPatternFeature::AxisPickedEdge) {
            return;
        }
        if (edge.size() < 2 || edge[1].isNull()) {
            return;
        }

        try {
            const TopoDS_Edge refEdge = TopoDS::Edge(edge[1].getShape());
            BRepAdaptor_Curve adapt(refEdge);
            gp_Pnt base;
            gp_Dir direction;
            switch (adapt.GetType()) {
                case GeomAbs_Line:
                    base = adapt.Line().Location();
                    direction = adapt.Line().Direction();
                    break;
                case GeomAbs_Circle:
                    base = adapt.Circle().Location();
                    direction = adapt.Circle().Axis().Direction();
                    break;
                default: {
                    // Any other curve only says something about the axis through
                    // the direction it runs in at its middle.
                    const double middle
                        = 0.5 * (adapt.FirstParameter() + adapt.LastParameter());
                    gp_Vec tangent;
                    adapt.D1(middle, base, tangent);
                    if (tangent.Magnitude() < Precision::Confusion()) {
                        return;
                    }
                    direction = gp_Dir(tangent);
                    break;
                }
            }
            feature->axis = gp_Ax1(base, direction);
            refreshPreview();
        }
        catch (const Standard_Failure& e) {
            CORE_ERROR("[PolarPattern] {0}: {1}", feature->GetName(), e.GetMessageString());
        }
        catch (const std::exception& e) {
            CORE_ERROR("[PolarPattern] {0}: {1}", feature->GetName(), e.what());
        }
    }
}
