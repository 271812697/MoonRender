#include "editor/UI/TaskPanel/MirrorTask.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/component/TopoShapeActor.h"
#include "feature/FeatureBody.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/EnumProperty.h"
#include "editor/UI/TaskPanel/OriginalsList.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"

#include <exception>

namespace MOON {
    class MirrorTask::Internal {
    public:
        Internal(MirrorTask* pad, TransformMode type) :self(pad), mirrorType(type) {
            auto f = self->getFeature();
            if (f) {
                feature = dynamic_cast<MirrorFeature*>(f);
                return;
            }

            feature = new MirrorFeature("Mirror");
            feature->mode = static_cast<int>(mirrorType);
            self->setFeature(feature);
            isCreateFeature = true;

            // The whole mode mirrors the shape of the last feature of the body, so a
            // selection is only needed to say which body that is; what is picked
            // inside it matters for the mirror plane alone.
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

            // Mirroring across the plane of the sketch the body was built from is
            // the common case - it reflects the body to the other side of it.
            feature->applySketchPlane(feature->planeType);
            AddBaseFeatureAsDefaultOriginal(*feature, feature->mode, feature->originals);
        }
        ~Internal() {
        }

    private:
        /** True when the mirror takes material away rather than adding it: a pocket
         * or a fillet that cut into the shape. */
        bool isSubtractivePattern() const
        {
            if (feature == nullptr
                || feature->mode != static_cast<int>(TransformMode::Feature)) {
                return false;
            }
            for (Feature* original : feature->originals) {
                if (original != nullptr && original->isToolSubtractive()) {
                    return true;
                }
            }
            return false;
        }

        MirrorTask* self;
        TransformMode mirrorType;
        friend MirrorTask;
        bool isCreateFeature = false;
        MirrorFeature* feature = nullptr;
        EnumProperty* modeProp = nullptr;
        EnumProperty* planeProp = nullptr;
    };
    MirrorTask::MirrorTask(QWidget* parent, TransformMode type, Feature* feature)
        : ParamTaskDialog(parent), ShapeHelper(feature), mInternal(new Internal(this, type))
    {
        setGenerateShapeName("MirrorShape");

        mPreviewOption.isTransparent = false;
        mPreviewOption.isBlend = true;
        mPreviewOption.useDomainColor = false;
        mPreviewOption.g = 0.0f;
        mPreviewOption.b = 0.0f;

        MirrorFeature* mirror = mInternal->feature;
        PropertyComponent* p = addGroupParam("Mirror");

        mInternal->modeProp = new EnumProperty("Mirror Type", p);
        mInternal->modeProp->setInitIndex(mirror ? mirror->mode : 0);
        addParam(mInternal->modeProp);

        mInternal->planeProp = new EnumProperty("Mirror Plane", p);
        mInternal->planeProp->setInitIndex(
            mirror ? mirror->planeType : MirrorFeature::PlaneSketchNormal);
        addParam(mInternal->planeProp);

        // The features the mirror works on when it runs in its "feature" mode.
        addGroupParam("Originals");

        buildUi();
        if (mirror) {
            const auto it = groupToIndex.find("Originals");
            if (it != groupToIndex.end()) {
                auto* group = m_comps[it->second].first;
                group->setCollapsed(false);
                group->addSubWidget(new OriginalsList(
                    *mirror,
                    mirror->originals,
                    [this]() { refreshPreview(); },
                    this));
            }
        }
        refreshPreview();
    }
    void MirrorTask::refreshPreview()
    {
        // What is mirrored can sit inside the body (a hole, a cut), so a subtractive
        // mirror is previewed the way the pocket previews itself - blended, without a
        // depth test - and cannot be hidden by the body. An additive mirror keeps the
        // transparent preview.
        const bool subtractive = mInternal->isSubtractivePattern();
        mPreviewOption.isTransparent = !subtractive;
        mPreviewOption.isBlend = true;
        previewShape();
    }
    QVariant MirrorTask::getParamValue(const QString& propertyName)
    {
        if (mInternal->feature == nullptr) {
            return QVariant();
        }
        if (propertyName == "Mirror:Mirror Type") {
            QList<QString>list = { "Whole", "Feature" };
            return QVariant::fromValue(list);
        }
        else if (propertyName == "Mirror:Mirror Plane") {
            QList<QString>list = {
                "Sketch Normal", "Sketch X", "Sketch Y", "Select Face" };
            return QVariant::fromValue(list);
        }
        return QVariant();
    }

    void MirrorTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        MirrorFeature* feature = mInternal->feature;
        if (feature == nullptr) {
            return;
        }

        bool updatePreView = false;
        if (propertyName == "Mirror:Mirror Type") {
            feature->mode = value.value<int>();
            updatePreView = true;
        }
        else if (propertyName == "Mirror:Mirror Plane") {
            const int planeType = value.value<int>();
            feature->planeType = planeType;
            if (planeType != MirrorFeature::PlanePickedFace
                && !feature->applySketchPlane(planeType)) {
                CORE_WARN(
                    "[Mirror] {0}: there is no sketch below it to take the plane from, "
                    "pick a face instead",
                    feature->GetName());
            }
            updatePreView = true;
        }

        if (updatePreView && hasInitUi) {
            refreshPreview();
        }
    }
    MirrorTask::~MirrorTask()
    {
        delete mInternal;
    }
    void MirrorTask::clickOk()
    {
        generateFinalShape();
    }
    void MirrorTask::clickApply()
    {
    }
    void MirrorTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreateFeature && mInternal->feature) {
            mInternal->feature->RemoveFromScene();
            delete mInternal->feature;
            mInternal->feature = nullptr;
        }
    }
    void MirrorTask::onSelectFace(const std::vector<Part::TopoShape>& face)
    {
        MirrorFeature* feature = mInternal->feature;
        if (feature == nullptr || feature->planeType != MirrorFeature::PlanePickedFace) {
            return;
        }
        if (face.size() < 2 || face[1].isNull()) {
            return;
        }

        try {
            if (!feature->applyPlaneFromFace(face[1])) {
                return;
            }
            refreshPreview();
        }
        catch (const Standard_Failure& e) {
            CORE_ERROR("[Mirror] {0}: {1}", feature->GetName(), e.GetMessageString());
        }
        catch (const std::exception& e) {
            CORE_ERROR("[Mirror] {0}: {1}", feature->GetName(), e.what());
        }
    }
}
