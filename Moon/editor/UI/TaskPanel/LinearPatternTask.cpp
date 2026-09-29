#include "editor/UI/TaskPanel/LinearPatternTask.h"
#include "core/component/TopoShapeActor.h"
#include "feature/FeatureBody.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/SliderIntProperty.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/BoolProperty.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_Shape.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <exception>

namespace MOON {
    namespace
    {
        // What the panel offers for the pattern.
        constexpr int kMaxOccurrences = 48;
        constexpr float kMaxLength = 1000.0f;
    }

    class LinearPatternTask::Internal {
    public:
        Internal(LinearPatternTask* pad, TransformMode type) :self(pad), linearType(type) {
            auto f = self->getFeature();
            if (f) {
                feature = dynamic_cast<LinearPatternFeature*>(f);
                return;
            }

            feature = new LinearPatternFeature("LinearPattern");
            feature->mode = static_cast<int>(linearType);
            self->setFeature(feature);
            isCreateFeature = true;

            // The whole mode patterns the shape of the last feature of the body, so a
            // selection is only needed to say which body that is; what is picked
            // inside it matters for the directions alone.
            Feature* baseFeature = nullptr;
            std::vector<std::string>subValues;
            ViewTool::getSelectedBasedFeature(baseFeature, subValues);
            if (baseFeature) {
                feature->setBaseFeature(baseFeature);
                feature->setSubValues(subValues);
            }
            else {
                FeatureBody::instance().setBaseFeatureFor(feature);
            }

            // Moving along the axes of the sketch the body was built from is the
            // common case, so both directions start there.
            feature->applySketchDirection(1, feature->directionType);
            feature->applySketchDirection(2, feature->directionType2);
        }
        ~Internal() {
        }

    private:
        LinearPatternTask* self;
        TransformMode linearType;
        friend LinearPatternTask;
        bool isCreateFeature = false;
        LinearPatternFeature* feature = nullptr;
        EnumProperty* modeProp = nullptr;
        EnumProperty* directionProp = nullptr;
        EnumProperty* dimensionProp = nullptr;
        SliderFloatProperty* lengthProp = nullptr;
        SliderIntProperty* occurrencesProp = nullptr;
        BoolProperty* reverseProp = nullptr;
        EnumProperty* directionProp2 = nullptr;
        EnumProperty* dimensionProp2 = nullptr;
        SliderFloatProperty* lengthProp2 = nullptr;
        SliderIntProperty* occurrencesProp2 = nullptr;
        BoolProperty* reverseProp2 = nullptr;
        /** The direction a pick belongs to, which is the one the user touched last:
         * with both directions set to "Select Edge" there is nothing else to tell
         * them apart. */
        int pickedDirection = 1;
    };
    LinearPatternTask::LinearPatternTask(QWidget* parent, TransformMode type, Feature* feature)
        : ParamTaskDialog(parent), ShapeHelper(feature), mInternal(new Internal(this, type))
    {
        setGenerateShapeName("LinearShape");

        mPreviewOption.isTransparent = true;
        mPreviewOption.isBlend = true;
        mPreviewOption.useDomainColor = false;
        mPreviewOption.g = 0.0f;
        mPreviewOption.b = 0.0f;

        LinearPatternFeature* pattern = mInternal->feature;
        PropertyComponent* top = addGroupParam("Linear");
        mInternal->modeProp = new EnumProperty("Pattern Type", top);
        mInternal->modeProp->setInitIndex(pattern ? pattern->mode : 0);
        addParam(mInternal->modeProp);

        PropertyComponent* first = addGroupParam("Direction 1");
        mInternal->directionProp = new EnumProperty("Direction", first);
        mInternal->directionProp->setInitIndex(
            pattern ? pattern->directionType : LinearPatternFeature::DirectionSketchX);
        addParam(mInternal->directionProp);
        mInternal->dimensionProp = new EnumProperty("Mode", first);
        mInternal->dimensionProp->setInitIndex(
            pattern ? pattern->dimensionMode : LinearPatternFeature::Extent);
        addParam(mInternal->dimensionProp);
        mInternal->lengthProp = new SliderFloatProperty("Length", first);
        mInternal->lengthProp->setMinMax(0.01f, kMaxLength);
        mInternal->lengthProp->setStep(0.5f);
        addParam(mInternal->lengthProp);
        mInternal->occurrencesProp = new SliderIntProperty("Occurrences", first);
        mInternal->occurrencesProp->setMinMax(1, kMaxOccurrences);
        addParam(mInternal->occurrencesProp);
        mInternal->reverseProp = new BoolProperty("Reverse", first);
        addParam(mInternal->reverseProp);

        PropertyComponent* second = addGroupParam("Direction 2");
        mInternal->directionProp2 = new EnumProperty("Direction", second);
        mInternal->directionProp2->setInitIndex(
            pattern ? pattern->directionType2 : LinearPatternFeature::DirectionSketchY);
        addParam(mInternal->directionProp2);
        mInternal->dimensionProp2 = new EnumProperty("Mode", second);
        mInternal->dimensionProp2->setInitIndex(
            pattern ? pattern->dimensionMode2 : LinearPatternFeature::Extent);
        addParam(mInternal->dimensionProp2);
        mInternal->lengthProp2 = new SliderFloatProperty("Length", second);
        mInternal->lengthProp2->setMinMax(0.01f, kMaxLength);
        mInternal->lengthProp2->setStep(0.5f);
        addParam(mInternal->lengthProp2);
        mInternal->occurrencesProp2 = new SliderIntProperty("Occurrences", second);
        mInternal->occurrencesProp2->setMinMax(1, kMaxOccurrences);
        addParam(mInternal->occurrencesProp2);
        mInternal->reverseProp2 = new BoolProperty("Reverse", second);
        addParam(mInternal->reverseProp2);

        buildUi();
        previewShape();
    }
    QVariant LinearPatternTask::getParamValue(const QString& propertyName)
    {
        if (mInternal->feature == nullptr) {
            return QVariant();
        }
        QList<QString>directions = {
            "Sketch X", "Sketch Y", "Sketch Normal", "Select Edge/Face" };
        QList<QString>dimensions = { "Extent", "Spacing" };

        if (propertyName == "Linear:Pattern Type") {
            QList<QString>list = { "Whole", "Feature" };
            return QVariant::fromValue(list);
        }
        else if (propertyName == "Direction 1:Direction") {
            return QVariant::fromValue(directions);
        }
        else if (propertyName == "Direction 1:Mode") {
            return QVariant::fromValue(dimensions);
        }
        else if (propertyName == "Direction 1:Length") {
            return QVariant::fromValue(mInternal->feature->length);
        }
        else if (propertyName == "Direction 1:Occurrences") {
            return QVariant::fromValue(mInternal->feature->occurrences);
        }
        else if (propertyName == "Direction 1:Reverse") {
            return QVariant::fromValue(mInternal->feature->reverse);
        }
        else if (propertyName == "Direction 2:Direction") {
            return QVariant::fromValue(directions);
        }
        else if (propertyName == "Direction 2:Mode") {
            return QVariant::fromValue(dimensions);
        }
        else if (propertyName == "Direction 2:Length") {
            return QVariant::fromValue(mInternal->feature->length2);
        }
        else if (propertyName == "Direction 2:Occurrences") {
            return QVariant::fromValue(mInternal->feature->occurrences2);
        }
        else if (propertyName == "Direction 2:Reverse") {
            return QVariant::fromValue(mInternal->feature->reverse2);
        }
        return QVariant();
    }

    void LinearPatternTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        LinearPatternFeature* feature = mInternal->feature;
        if (feature == nullptr) {
            return;
        }

        bool updatePreView = false;
        if (propertyName == "Linear:Pattern Type") {
            feature->mode = value.value<int>();
            updatePreView = true;
        }
        else if (propertyName == "Direction 1:Direction") {
            const int directionType = value.value<int>();
            feature->directionType = directionType;
            mInternal->pickedDirection = 1;
            if (directionType != LinearPatternFeature::DirectionPickedEdge
                && !feature->applySketchDirection(1, directionType)) {
                CORE_WARN(
                    "[LinearPattern] {0}: there is no sketch below it to take the first "
                    "direction from, pick an edge instead",
                    feature->GetName());
            }
            updatePreView = true;
        }
        else if (propertyName == "Direction 1:Mode") {
            feature->dimensionMode = value.value<int>();
            updatePreView = true;
        }
        else if (propertyName == "Direction 1:Length") {
            feature->length = value.toFloat();
            updatePreView = true;
        }
        else if (propertyName == "Direction 1:Occurrences") {
            feature->occurrences = std::max(value.toInt(), 1);
            updatePreView = true;
        }
        else if (propertyName == "Direction 1:Reverse") {
            feature->reverse = value.toBool();
            updatePreView = true;
        }
        else if (propertyName == "Direction 2:Direction") {
            const int directionType = value.value<int>();
            feature->directionType2 = directionType;
            mInternal->pickedDirection = 2;
            if (directionType != LinearPatternFeature::DirectionPickedEdge
                && !feature->applySketchDirection(2, directionType)) {
                CORE_WARN(
                    "[LinearPattern] {0}: there is no sketch below it to take the second "
                    "direction from, pick an edge instead",
                    feature->GetName());
            }
            updatePreView = true;
        }
        else if (propertyName == "Direction 2:Mode") {
            feature->dimensionMode2 = value.value<int>();
            updatePreView = true;
        }
        else if (propertyName == "Direction 2:Length") {
            feature->length2 = value.toFloat();
            updatePreView = true;
        }
        else if (propertyName == "Direction 2:Occurrences") {
            feature->occurrences2 = std::max(value.toInt(), 1);
            updatePreView = true;
        }
        else if (propertyName == "Direction 2:Reverse") {
            feature->reverse2 = value.toBool();
            updatePreView = true;
        }

        if (updatePreView && hasInitUi) {
            previewShape();
        }
    }
    LinearPatternTask::~LinearPatternTask()
    {
        delete mInternal;
    }
    void LinearPatternTask::clickOk()
    {
        generateFinalShape();
    }
    void LinearPatternTask::clickApply()
    {
    }
    void LinearPatternTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreateFeature && mInternal->feature) {
            mInternal->feature->RemoveFromScene();
            delete mInternal->feature;
            mInternal->feature = nullptr;
        }
    }
    void LinearPatternTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
    {
        LinearPatternFeature* feature = mInternal->feature;
        if (feature == nullptr || edge.size() < 2 || edge[1].isNull()) {
            return;
        }

        try {
            const TopoDS_Edge refEdge = TopoDS::Edge(edge[1].getShape());
            BRepAdaptor_Curve adapt(refEdge);
            if (adapt.GetType() != GeomAbs_Line) {
                CORE_WARN(
                    "[LinearPattern] {0}: the direction edge must be a straight line",
                    feature->GetName());
                return;
            }
            const gp_Dir direction = adapt.Line().Direction();
            if (mInternal->pickedDirection == 2) {
                feature->direction2 = direction;
                feature->directionType2 = LinearPatternFeature::DirectionPickedEdge;
            }
            else {
                feature->direction = direction;
                feature->directionType = LinearPatternFeature::DirectionPickedEdge;
            }
            previewShape();
        }
        catch (const Standard_Failure& e) {
            CORE_ERROR(
                "[LinearPattern] {0}: {1}", feature->GetName(), e.GetMessageString());
        }
        catch (const std::exception& e) {
            CORE_ERROR("[LinearPattern] {0}: {1}", feature->GetName(), e.what());
        }
    }
    void LinearPatternTask::onSelectFace(const std::vector<Part::TopoShape>& face)
    {
        LinearPatternFeature* feature = mInternal->feature;
        if (feature == nullptr || face.size() < 2 || face[1].isNull()) {
            return;
        }

        try {
            TopoDS_Shape shape = face[1].getShape();
            if (shape.ShapeType() != TopAbs_FACE) {
                if (!face[1].hasSubShape(TopAbs_FACE)) {
                    return;
                }
                shape = face[1].getSubShape(TopAbs_FACE, 1);
            }
            BRepAdaptor_Surface adapt(TopoDS::Face(shape));
            if (adapt.GetType() != GeomAbs_Plane) {
                CORE_WARN(
                    "[LinearPattern] {0}: the picked face is not planar, it cannot give "
                    "a direction",
                    feature->GetName());
                return;
            }
            const gp_Dir direction = adapt.Plane().Axis().Direction();
            if (mInternal->pickedDirection == 2) {
                feature->direction2 = direction;
                feature->directionType2 = LinearPatternFeature::DirectionPickedEdge;
            }
            else {
                feature->direction = direction;
                feature->directionType = LinearPatternFeature::DirectionPickedEdge;
            }
            previewShape();
        }
        catch (const Standard_Failure& e) {
            CORE_ERROR(
                "[LinearPattern] {0}: {1}", feature->GetName(), e.GetMessageString());
        }
        catch (const std::exception& e) {
            CORE_ERROR("[LinearPattern] {0}: {1}", feature->GetName(), e.what());
        }
    }
}
