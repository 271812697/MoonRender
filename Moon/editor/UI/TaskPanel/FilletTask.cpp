#include "editor/UI/TaskPanel/FilletTask.h"
#include "TaskBox.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/BoolProperty.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "feature/FilletFeature.h"
#include "core/log.h"
#include "Interactive/Widgets/AxisTranslationWidget.h"
#include "base/BoundBox.h"
#include <QListWidget>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <GeomAbs_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <BRepAlgo.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <Precision.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Precision.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include "App/GizmoHelper.h"
#include <algorithm>
namespace MOON {

    namespace
    {
        // A conservative upper bound for the initial fillet radius. An edge
        // cannot be filleted with a radius that is larger than a quarter of
        // its own length (otherwise the two ends of the fillet overlap), so
        // the shortest selected edge limits the initial value.
        float safeInitialFilletRadius(
            const std::vector<Part::TopoShape>& edges,
            double baseDiagonal
        )
        {
            double radius = baseDiagonal * 0.03;
            for (const auto& edge : edges) {
                GProp_GProps props;
                BRepGProp::LinearProperties(edge.getShape(), props);
                const double len = props.Mass();
                if (len > Precision::Confusion()) {
                    radius = std::min(radius, len * 0.25);
                }
            }
            return static_cast<float>(radius);
        }
    }

    class FilletTask::Internal {
    public:
        Internal(FilletTask*s):self(s){
            auto f = self->getFeature();
            if (f) {
                feature = dynamic_cast<FilletFeature*>(f);
            }
            else
            {
                Feature* baseFeature = nullptr;
                std::vector<std::string>subValues;
                ViewTool::getSelectedBasedFeature(baseFeature, subValues);
                if (baseFeature) {
                    isCreatedFeature = true;
                    feature = new FilletFeature("Fillet");
                    feature->setBaseFeature(baseFeature);
                    feature->setSubValues(subValues);
                    self->setFeature(feature);

                    Part::TopoShape baseShape;
                    std::vector<Part::TopoShape> shapes;
                    // Opening this panel reads the shape the fillet sits on. A
                    // reference that the recomputed shape cannot answer any more, or
                    // a kernel failure while looking for a radius, must not take the
                    // panel (or the application) down with it: the panel opens and
                    // the log says what went wrong.
                    try {
                        baseShape = feature->getBaseTopoShape();
                        shapes = feature->getBaseTopoEdgeShapes();
                        feature->len
                            = baseShape.getBoundBoxOptimal().CalcDiagonalLength() * 0.01;
                        feature->radius = safeInitialFilletRadius(
                            shapes,
                            baseShape.getBoundBoxOptimal().CalcDiagonalLength()
                        );

                        // Keep shrinking until the radius really produces a fillet.
                        // execute() only touches the shape once it succeeds, so a
                        // failed attempt leaves the previous (smaller) result.
                        int shrinkGuards = 0;
                        while (feature->radius > 0.001f && !feature->execute()) {
                            feature->radius *= 0.5f;
                            if (++shrinkGuards > 24) {
                                break;
                            }
                        }
                    }
                    catch (Base::Exception& e) {
                        CORE_ERROR("[Fillet] {0}: {1}", feature->GetName(), e.what());
                    }
                    catch (Standard_Failure& e) {
                        CORE_ERROR(
                            "[Fillet] {0}: {1}",
                            feature->GetName(),
                            e.GetMessageString());
                    }

                    // Attach the arrow to the first edge, when the shape has one
                    // that resolved.
                    if (!shapes.empty() && !shapes[0].isNull()) {
                        Part::TopoShape edge = shapes[0];
                        auto [face1, face2] = getAdjacentFacesFromEdge(edge, baseShape);
                        DraggerPlacementProps props1 = getDraggerPlacementFromEdgeAndFace(edge, face1);
                        DraggerPlacementProps props2 = getDraggerPlacementFromEdgeAndFace(edge, face2);
                        feature->origin1[0] = props1.position.x;
                        feature->origin1[1] = props1.position.y;
                        feature->origin1[2] = props1.position.z;
                        feature->dir1[0] = props1.dir.x;
                        feature->dir1[1] = props1.dir.y;
                        feature->dir1[2] = props1.dir.z;
                        feature->origin2[0] = props2.position.x;
                        feature->origin2[1] = props2.position.y;
                        feature->origin2[2] = props2.position.z;
                        feature->dir2[0] = props2.dir.x;
                        feature->dir2[1] = props2.dir.y;
                        feature->dir2[2] = props2.dir.z;
                    }


                }
            }
            if (feature) {
                axisBehaviour1 = new AxisTranslationWidget("fillet");
                axisBehaviour2 = new AxisTranslationWidget("fillet");
                axisBehaviour1->setUpScale(feature->len);
                axisBehaviour2->setUpScale(feature->len);
                //axisBehaviour1->setImmediateInvoke(false);
                axisBehaviour1->setLength(feature->radius);
                axisBehaviour1->setUpOrigin(feature->origin1[0], feature->origin1[1], feature->origin1[2]);
                axisBehaviour1->setUpDir(feature->dir1[0], feature->dir1[1], feature->dir1[2]);
                //axisBehaviour2->setImmediateInvoke(false);
                axisBehaviour2->setLength(feature->radius);
                axisBehaviour2->setUpOrigin(feature->origin2[0], feature->origin2[1], feature->origin2[2]);
                axisBehaviour2->setUpDir(feature->dir2[0], feature->dir2[1], feature->dir2[2]);
                axisBehaviour1->AddObserver(AxisTranslationEvent::LengthChange, self, &FilletTask::onWidgetLengthInvoke1);
                axisBehaviour2->AddObserver(AxisTranslationEvent::LengthChange, self, &FilletTask::onWidgetLengthInvoke2);
            }
        }
        ~Internal() {
            if (axisBehaviour1) {
                delete axisBehaviour1;
                delete axisBehaviour2;
            }
        }
    private:
        SliderFloatProperty* radiusProp;
        /** The edge list of the panel (see refreshReferenceRows). */
        QListWidget* edgeList = nullptr;
        QLabel* edgeLabel = nullptr;
        QLabel* edgeHint = nullptr;
        friend FilletTask;
        FilletTask* self = nullptr;
        FilletFeature* feature = nullptr;
        AxisTranslationWidget* axisBehaviour1 = nullptr;
        AxisTranslationWidget* axisBehaviour2 = nullptr;
        bool isCreatedFeature = false;
        //std::vector<Part::TopoShape>shapes;
    };

    FilletTask::FilletTask(QWidget* parent, Feature* feature )
        : ParamTaskDialog(parent),ShapeHelper(feature), mInternal(new Internal(this))
    {       
        setGenerateShapeName("FilletShape");
        mPreviewOption.isTransparent = false;
        mPreviewOption.isBlend = true;
        mPreviewOption.useDomainColor = false;
        PropertyComponent* p=addGroupParam("Fillet");
        mInternal->radiusProp = new SliderFloatProperty("Radius", p);
        mInternal->radiusProp->setMinMax(0.001f, 10.0f);
        addParam(mInternal->radiusProp);
        BoolProperty* intersection = new BoolProperty("Use ALL Edges", p);
        addParam(intersection);
        buildUi();

        if (mInternal->feature == nullptr) {
            return;
        }
        // The edges the fillet is applied to, listed with the buttons that take them from
        // the selection - the same shape the sweep's path list has. An edge picked while
        // the panel is open lands here as well (see onSelectEdge).
        auto* references = new QWidget(this);
        auto* referencesLayout = new QVBoxLayout(references);
        referencesLayout->setContentsMargins(0, 0, 0, 0);
        referencesLayout->setSpacing(2);

        auto* edgeRow = new QHBoxLayout();
        edgeRow->setContentsMargins(0, 0, 0, 0);
        auto* addEdges = new QToolButton(references);
        addEdges->setText(tr("Add from Selection"));
        auto* removeEdges = new QToolButton(references);
        removeEdges->setText(tr("Remove"));
        auto* clearEdges = new QToolButton(references);
        clearEdges->setText(tr("Clear"));
        edgeRow->addWidget(addEdges);
        edgeRow->addWidget(removeEdges);
        edgeRow->addWidget(clearEdges);
        edgeRow->addStretch();
        referencesLayout->addLayout(edgeRow);

        mInternal->edgeLabel = new QLabel(references);
        mInternal->edgeLabel->setStyleSheet("color: #c8ccd0;");
        referencesLayout->addWidget(mInternal->edgeLabel);
        mInternal->edgeList = new QListWidget(references);
        mInternal->edgeList->setMinimumHeight(50);
        mInternal->edgeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
        mInternal->edgeList->setStyleSheet(
            "QListWidget { background: transparent; border: none; outline: 0; }"
            "QListWidget::item { height: 20px; padding-left: 2px; }"
        );
        referencesLayout->addWidget(mInternal->edgeList);
        mInternal->edgeHint = new QLabel(references);
        mInternal->edgeHint->setWordWrap(true);
        mInternal->edgeHint->setStyleSheet("color: #a8a8a8;");
        mInternal->edgeHint->setText(tr(
            "Select edges of the shape below - in the viewport or in the tree - and press "
            "Add from Selection; an edge picked while this panel is open is added as well. "
            "Remove drops the rows that are selected in the list."));
        referencesLayout->addWidget(mInternal->edgeHint);

        const auto groupIndex = groupToIndex.find("Fillet");
        if (groupIndex != groupToIndex.end()) {
            m_comps[groupIndex->second].first->addSubWidget(references);
        }

        connect(addEdges, &QToolButton::clicked, this, [this]() { applyPickedEdges(); });
        connect(removeEdges, &QToolButton::clicked, this, [this]() { removeSelectedEdges(); });
        connect(clearEdges, &QToolButton::clicked, this, [this]() {
            if (mInternal->feature == nullptr) {
                return;
            }
            mInternal->feature->setSubValues({});
            mInternal->feature->setReferenceNames({});
            refreshReferenceRows();
            previewShape();
        });
        refreshReferenceRows();
    }

    FilletTask::~FilletTask()
    {
        delete mInternal;
    }

    QVariant FilletTask::getParamValue(const QString& propertyName)
    {
        if (propertyName == "Fillet:Radius") {
            return QVariant::fromValue(mInternal->feature->radius);
        }
		else if (propertyName == "Fillet:Use ALL Edges") {
			return QVariant::fromValue(mInternal->feature->useAllEdges);
        }
        return QVariant();
    }

    void FilletTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        bool updatePreView = false;
        if (propertyName == "Fillet:Radius") {
            mInternal->feature->radius=value.toFloat();
            if (mInternal->axisBehaviour1) {
                mInternal->axisBehaviour1->setLength(mInternal->feature->radius);
                mInternal->axisBehaviour2->setLength(mInternal->feature->radius);
            }
            updatePreView = true;
        }
       
		else if (propertyName == "Fillet:Use ALL Edges") {
			mInternal->feature->useAllEdges= value.value<bool>();
            updatePreView = true;
        }
        if (updatePreView&& mInternal->axisBehaviour1) {
            previewShape();
        }
    }


    void FilletTask::clickOk()
    {
        generateFinalShape();
    }
    void FilletTask::clickApply()
    {
    }
    void FilletTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreatedFeature) {
            mInternal->feature->RemoveFromScene();
            delete mInternal->feature;
        }
    }
    void FilletTask::onWidgetLengthInvoke1()
    {        
        mInternal->feature->radius= mInternal->axisBehaviour1->getLength();
        mInternal->radiusProp->updateWidgetValue(mInternal->feature->radius);
    }
    void FilletTask::onWidgetLengthInvoke2() {
        mInternal->feature->radius = mInternal->axisBehaviour2->getLength();
        mInternal->radiusProp->updateWidgetValue(mInternal->feature->radius);
    }

    void FilletTask::refreshReferenceRows()
    {
        if (mInternal->feature == nullptr) {
            return;
        }
        const std::vector<std::string>& references = mInternal->feature->getSubValues();
        if (mInternal->edgeLabel != nullptr) {
            if (mInternal->feature->useAllEdges) {
                mInternal->edgeLabel->setText(tr("Edges: every edge of the shape below"));
            }
            else {
                mInternal->edgeLabel->setText(
                    tr("Edges: %1 selected").arg(static_cast<int>(references.size())));
            }
        }
        if (mInternal->edgeList != nullptr) {
            mInternal->edgeList->clear();
            for (int i = 0; i < static_cast<int>(references.size()); ++i) {
                mInternal->edgeList->addItem(
                    QString("%1  %2").arg(i).arg(QString::fromStdString(references[i])));
            }
            // With "Use ALL Edges" the list is not what the fillet works from, so it is
            // shown but not something to add to or take from.
            mInternal->edgeList->setEnabled(!mInternal->feature->useAllEdges
                && !references.empty());
        }
    }

    void FilletTask::applyPickedEdges()
    {
        if (mInternal->feature == nullptr) {
            return;
        }
        FilletFeature& feature = *mInternal->feature;
        Feature* base = nullptr;
        std::vector<std::string> picked;
        if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr) {
            CORE_WARN(
                "[Fillet] {0}: select the edge(s) in the viewport or in the tree first",
                feature.GetName());
            return;
        }
        if (base != feature.getBaseFeature()) {
            // A fillet runs along edges of the one shape it is built on: an edge of
            // another feature is not something it could carry.
            CORE_WARN(
                "[Fillet] {0}: the edges have to come from '{1}', the shape the fillet is "
                "built on",
                feature.GetName(),
                feature.getBaseFeature() != nullptr
                    ? feature.getBaseFeature()->GetName()
                    : "the shape below");
            return;
        }
        std::vector<std::string> references = feature.getSubValues();
        for (const std::string& reference : picked) {
            if (reference.rfind("Edge_", 0) != 0) {
                continue;  // a fillet runs along edges; a face or a whole object is not one
            }
            if (std::find(references.begin(), references.end(), reference) == references.end()) {
                references.push_back(reference);
            }
        }
        if (references.size() == feature.getSubValues().size()) {
            return;  // nothing new was selected
        }
        // The mapped names stay with their reference: the ones already there are kept,
        // the new edges start without any and are resolved - and named - on the next build.
        std::vector<std::vector<std::string>> names = feature.getReferenceNames();
        names.resize(references.size());
        feature.setSubValues(references);
        feature.setReferenceNames(names);
        refreshReferenceRows();
        previewShape();
    }

    void FilletTask::removeSelectedEdges()
    {
        if (mInternal->feature == nullptr || mInternal->edgeList == nullptr) {
            return;
        }
        std::set<int> dropped;
        for (QListWidgetItem* item : mInternal->edgeList->selectedItems()) {
            dropped.insert(mInternal->edgeList->row(item));
        }
        if (dropped.empty()) {
            CORE_WARN(
                "[Fillet] {0}: select the rows to remove in the list first",
                mInternal->feature->GetName());
            return;
        }
        std::vector<std::vector<std::string>> names = mInternal->feature->getReferenceNames();
        names.resize(mInternal->feature->getSubValues().size());
        std::vector<std::string> references;
        std::vector<std::vector<std::string>> keptNames;
        for (int i = 0; i < static_cast<int>(mInternal->feature->getSubValues().size()); ++i) {
            if (dropped.count(i) != 0) {
                continue;  // this edge is what is being taken out
            }
            references.push_back(mInternal->feature->getSubValues()[i]);
            keptNames.push_back(names[i]);
        }
        mInternal->feature->setSubValues(references);
        mInternal->feature->setReferenceNames(keptNames);
        refreshReferenceRows();
        previewShape();
    }

    void FilletTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
    {
        // Picking an edge while the panel is open adds it, which is the same thing the
        // button does - a picked edge is what the user means by it.
        applyPickedEdges();
    }
}
