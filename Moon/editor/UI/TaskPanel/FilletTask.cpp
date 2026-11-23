#include "editor/UI/TaskPanel/FilletTask.h"
#include "editor/UI/TaskPanel/FilletInteraction.h"
#include "TaskBox.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/BoolProperty.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "feature/FilletFeature.h"
#include "core/log.h"
#include <QListWidget>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace MOON {

    class FilletTask::Internal {
    public:
        explicit Internal(FilletTask* s) : self(s) {
        }
        ~Internal() {
        }
    private:
        SliderFloatProperty* radiusProp = nullptr;
        /** The edge list of the panel (see refreshReferenceRows). */
        QListWidget* edgeList = nullptr;
        QLabel* edgeLabel = nullptr;
        QLabel* edgeHint = nullptr;
        friend FilletTask;
        FilletTask* self = nullptr;
    };

    BaseTaskDialog* MakeFilletPanel(std::unique_ptr<FeatureInteraction> p_interaction)
    {
        auto fillet = std::unique_ptr<FilletInteraction>(
            static_cast<FilletInteraction*>(p_interaction.release()));
        return new FilletTask(std::move(fillet));
    }

    FilletTask::FilletTask(std::unique_ptr<FilletInteraction> p_fillet, QWidget* parent)
        : ParamTaskDialog(parent), m_fillet(std::move(p_fillet)), mInternal(new Internal(this))
    {
        PropertyComponent* p = addGroupParam("Fillet");
        mInternal->radiusProp = new SliderFloatProperty("Radius", p);
        mInternal->radiusProp->setMinMax(0.001f, 10.0f);
        addParam(mInternal->radiusProp);
        BoolProperty* intersection = new BoolProperty("Use ALL Edges", p);
        addParam(intersection);
        buildUi();

        if (feature() == nullptr) {
            return;
        }
        // The handles are the interaction's; a drag already put the value on the feature, the
        // panel only has to show it.
        m_fillet->SetParamChangedCallback([this]() { refreshRadius(); });
        // An edge picked while the panel is open is added to the fillet by the interaction; the
        // list of edges is what follows.
        m_fillet->SetEdgesChangedCallback([this]() { refreshReferenceRows(); });

        // The edges the fillet is applied to, listed with the buttons that take them from
        // the selection - the same shape the sweep's path list has. An edge picked while
        // the panel is open lands here as well (see FilletInteraction::onSelectEdge).
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
            if (feature() == nullptr) {
                return;
            }
            feature()->setSubValues({});
            feature()->setReferenceNames({});
            refreshReferenceRows();
            m_fillet->UpdatePreview();
        });
        refreshReferenceRows();
    }

    FilletTask::~FilletTask()
    {
        delete mInternal;
    }

    FilletFeature* FilletTask::feature() const
    {
        return m_fillet != nullptr ? m_fillet->GetFillet() : nullptr;
    }

    void FilletTask::refreshRadius()
    {
        if (mInternal->radiusProp != nullptr && m_fillet != nullptr) {
            mInternal->radiusProp->updateWidgetValue(m_fillet->GetRadius());
        }
    }

    QVariant FilletTask::getParamValue(const QString& propertyName)
    {
        if (feature() == nullptr) {
            return QVariant();
        }
        if (propertyName == "Fillet:Radius") {
            return QVariant::fromValue(feature()->radius);
        }
        else if (propertyName == "Fillet:Use ALL Edges") {
            return QVariant::fromValue(feature()->useAllEdges);
        }
        return QVariant();
    }

    void FilletTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        if (feature() == nullptr) {
            return;
        }
        bool updatePreView = false;
        if (propertyName == "Fillet:Radius") {
            m_fillet->SetRadius(value.toFloat());
            updatePreView = true;
        }
        else if (propertyName == "Fillet:Use ALL Edges") {
            feature()->useAllEdges = value.value<bool>();
            updatePreView = true;
        }
        if (updatePreView) {
            m_fillet->UpdatePreview();
        }
    }

    void FilletTask::clickOk()
    {
        m_fillet->Commit();
    }

    void FilletTask::clickApply()
    {
    }

    void FilletTask::clickCancel()
    {
        m_fillet->Abort();
    }

    void FilletTask::refreshReferenceRows()
    {
        if (feature() == nullptr) {
            return;
        }
        const std::vector<std::string>& references = feature()->getSubValues();
        if (mInternal->edgeLabel != nullptr) {
            if (feature()->useAllEdges) {
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
            mInternal->edgeList->setEnabled(!feature()->useAllEdges && !references.empty());
        }
    }

    void FilletTask::applyPickedEdges()
    {
        if (feature() == nullptr) {
            return;
        }
        FilletFeature& filletFeature = *feature();
        Feature* base = nullptr;
        std::vector<std::string> picked;
        if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr) {
            CORE_WARN(
                "[Fillet] {0}: select the edge(s) in the viewport or in the tree first",
                filletFeature.GetName());
            return;
        }
        if (base != filletFeature.getBaseFeature()) {
            // A fillet runs along edges of the one shape it is built on: an edge of
            // another feature is not something it could carry.
            CORE_WARN(
                "[Fillet] {0}: the edges have to come from '{1}', the shape the fillet is "
                "built on",
                filletFeature.GetName(),
                filletFeature.getBaseFeature() != nullptr
                    ? filletFeature.getBaseFeature()->GetName()
                    : "the shape below");
            return;
        }
        std::vector<std::string> references = filletFeature.getSubValues();
        for (const std::string& reference : picked) {
            if (reference.rfind("Edge_", 0) != 0) {
                continue;  // a fillet runs along edges; a face or a whole object is not one
            }
            if (std::find(references.begin(), references.end(), reference) == references.end()) {
                references.push_back(reference);
            }
        }
        if (references.size() == filletFeature.getSubValues().size()) {
            return;  // nothing new was selected
        }
        // The mapped names stay with their reference: the ones already there are kept,
        // the new edges start without any and are resolved - and named - on the next build.
        std::vector<std::vector<std::string>> names = filletFeature.getReferenceNames();
        names.resize(references.size());
        filletFeature.setSubValues(references);
        filletFeature.setReferenceNames(names);
        refreshReferenceRows();
        m_fillet->UpdatePreview();
    }

    void FilletTask::removeSelectedEdges()
    {
        if (feature() == nullptr || mInternal->edgeList == nullptr) {
            return;
        }
        std::set<int> dropped;
        for (QListWidgetItem* item : mInternal->edgeList->selectedItems()) {
            dropped.insert(mInternal->edgeList->row(item));
        }
        if (dropped.empty()) {
            CORE_WARN(
                "[Fillet] {0}: select the rows to remove in the list first",
                feature()->GetName());
            return;
        }
        std::vector<std::vector<std::string>> names = feature()->getReferenceNames();
        names.resize(feature()->getSubValues().size());
        std::vector<std::string> references;
        std::vector<std::vector<std::string>> keptNames;
        for (int i = 0; i < static_cast<int>(feature()->getSubValues().size()); ++i) {
            if (dropped.count(i) != 0) {
                continue;  // this edge is what is being taken out
            }
            references.push_back(feature()->getSubValues()[i]);
            keptNames.push_back(names[i]);
        }
        feature()->setSubValues(references);
        feature()->setReferenceNames(keptNames);
        refreshReferenceRows();
        m_fillet->UpdatePreview();
    }
}
