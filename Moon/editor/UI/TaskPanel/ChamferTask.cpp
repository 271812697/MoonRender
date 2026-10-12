#include "editor/UI/TaskPanel/ChamferTask.h"
#include "TaskBox.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/EnumProperty.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "feature/ChamferFeature.h"
#include "core/log.h"
#include "Interactive/Widgets/AxisTranslationWidget.h"
#include "base/BoundBox.h"
#include "App/GizmoHelper.h"

#include <QStringList>
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

	class ChamferTask::Internal {
	public:
		Internal(ChamferTask* s) : self(s) {
			auto f = self->getFeature();
			if (f) {
				feature = dynamic_cast<ChamferFeature*>(f);
			}
			else {
				Feature* baseFeature = nullptr;
				std::vector<std::string> subValues;
				ViewTool::getSelectedBasedFeature(baseFeature, subValues);
				if (baseFeature) {
					isCreatedFeature = true;
					feature = new ChamferFeature("Chamfer");
					feature->setBaseFeature(baseFeature);
					feature->setSubValues(subValues);
					self->setFeature(feature);

					Part::TopoShape baseShape = feature->getBaseTopoShape();
					std::vector<Part::TopoShape> shapes = feature->getBaseTopoEdgeShapes();
					feature->len = baseShape.getBoundBoxOptimal().CalcDiagonalLength() * 0.01;
					feature->size = baseShape.getBoundBoxOptimal().CalcDiagonalLength() * 0.03;

					// Attach the arrows to the first edge (one per adjacent face).
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
			if (feature) {
				axisBehaviour1 = new AxisTranslationWidget("chamfer");
				axisBehaviour2 = new AxisTranslationWidget("chamfer");
				axisBehaviour1->setUpScale(feature->len);
				axisBehaviour2->setUpScale(feature->len);
				axisBehaviour1->setLength(feature->size);
				axisBehaviour1->setUpOrigin(feature->origin1[0], feature->origin1[1], feature->origin1[2]);
				axisBehaviour1->setUpDir(feature->dir1[0], feature->dir1[1], feature->dir1[2]);
				axisBehaviour2->setLength(feature->size);
				axisBehaviour2->setUpOrigin(feature->origin2[0], feature->origin2[1], feature->origin2[2]);
				axisBehaviour2->setUpDir(feature->dir2[0], feature->dir2[1], feature->dir2[2]);
				axisBehaviour1->AddObserver(AxisTranslationEvent::LengthChange, self, &ChamferTask::onWidgetLengthInvoke1);
				axisBehaviour2->AddObserver(AxisTranslationEvent::LengthChange, self, &ChamferTask::onWidgetLengthInvoke2);
			}
		}
		~Internal() {
			if (axisBehaviour1) {
				delete axisBehaviour1;
				delete axisBehaviour2;
			}
		}
	private:
		SliderFloatProperty* sizeProp;
		/** The edge list of the panel (see refreshReferenceRows). */
		QListWidget* edgeList = nullptr;
		QLabel* edgeLabel = nullptr;
		QLabel* edgeHint = nullptr;
		friend ChamferTask;
		ChamferTask* self = nullptr;
		ChamferFeature* feature = nullptr;
		AxisTranslationWidget* axisBehaviour1 = nullptr;
		AxisTranslationWidget* axisBehaviour2 = nullptr;
		bool isCreatedFeature = false;
	};

	ChamferTask::ChamferTask(QWidget* parent, Feature* feature)
		: ParamTaskDialog(parent), ShapeHelper(feature), mInternal(new Internal(this)) {
		setGenerateShapeName("ChamferShape");
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
		PropertyComponent* p = addGroupParam("Chamfer");

		EnumProperty* type = new EnumProperty("Type", p);
		addParam(type);
		mInternal->sizeProp = new SliderFloatProperty("Size", p);
		mInternal->sizeProp->setMinMax(0.1f, 10.f);
		addParam(mInternal->sizeProp);
		SliderFloatProperty* size2 = new SliderFloatProperty("Size2", p);
		size2->setMinMax(0.1f, 10.f);
		addParam(size2);
		SliderFloatProperty* angle = new SliderFloatProperty("Angle", p);
		angle->setMinMax(1.f, 179.f);
		addParam(angle);
		addParam(new BoolProperty("Flip Direction", p));
		addParam(new BoolProperty("Use ALL Edges", p));
		buildUi();

		if (mInternal->feature == nullptr) {
			return;
		}
		// The edges the chamfer is applied to, listed with the buttons that take them
		// from the selection - the same shape the sweep's path list and the fillet's
		// edge list have. An edge picked while the panel is open lands here as well
		// (see onSelectEdge).
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

		const auto groupIndex = groupToIndex.find("Chamfer");
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

	ChamferTask::~ChamferTask() {
		delete mInternal;
	}

	QVariant ChamferTask::getParamValue(const QString& propertyName) {
		if (propertyName == "Chamfer:Type") {
			QList<QString> list = { "Equal distance", "Two distances", "Distance and Angle" };
			return QVariant::fromValue(list);
		}
		else if (propertyName == "Chamfer:Size") {
			return QVariant::fromValue(mInternal->feature->size);
		}
		else if (propertyName == "Chamfer:Size2") {
			return QVariant::fromValue(mInternal->feature->size2);
		}
		else if (propertyName == "Chamfer:Angle") {
			return QVariant::fromValue(mInternal->feature->angle);
		}
		else if (propertyName == "Chamfer:Flip Direction") {
			return QVariant::fromValue(mInternal->feature->flipDirection);
		}
		else if (propertyName == "Chamfer:Use ALL Edges") {
			return QVariant::fromValue(mInternal->feature->useAllEdges);
		}
		return QVariant();
	}

	void ChamferTask::setParamValue(const QString& propertyName, const QVariant& value) {
		bool updatePreView = false;
		if (propertyName == "Chamfer:Type") {
			mInternal->feature->chamferType = value.value<int>();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Size") {
			mInternal->feature->size = value.toFloat();
			if (mInternal->axisBehaviour1) {
				mInternal->axisBehaviour1->setLength(mInternal->feature->size);
				mInternal->axisBehaviour2->setLength(mInternal->feature->size);
			}
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Size2") {
			mInternal->feature->size2 = value.toFloat();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Angle") {
			mInternal->feature->angle = value.toFloat();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Flip Direction") {
			mInternal->feature->flipDirection = value.value<bool>();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Use ALL Edges") {
			mInternal->feature->useAllEdges = value.value<bool>();
			updatePreView = true;
		}
		if (updatePreView && mInternal->axisBehaviour1) {
			previewShape();
		}
	}

	void ChamferTask::clickOk() {
		generateFinalShape();
	}

	void ChamferTask::clickApply() {
	}

	void ChamferTask::clickCancel() {
		clearPreviewShape();
		if (mInternal->isCreatedFeature) {
			mInternal->feature->RemoveFromScene();
			delete mInternal->feature;
		}
	}

	void ChamferTask::onWidgetLengthInvoke1() {
		mInternal->feature->size = mInternal->axisBehaviour1->getLength();
		mInternal->sizeProp->updateWidgetValue(mInternal->feature->size);
	}

	void ChamferTask::onWidgetLengthInvoke2() {
		mInternal->feature->size = mInternal->axisBehaviour2->getLength();
		mInternal->sizeProp->updateWidgetValue(mInternal->feature->size);
	}

	void ChamferTask::refreshReferenceRows()
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
			// With "Use ALL Edges" the list is not what the chamfer works from, so it
			// is shown but not something to add to or take from.
			mInternal->edgeList->setEnabled(!mInternal->feature->useAllEdges
				&& !references.empty());
		}
	}

	void ChamferTask::applyPickedEdges()
	{
		if (mInternal->feature == nullptr) {
			return;
		}
		ChamferFeature& feature = *mInternal->feature;
		Feature* base = nullptr;
		std::vector<std::string> picked;
		if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr) {
			CORE_WARN(
				"[Chamfer] {0}: select the edge(s) in the viewport or in the tree first",
				feature.GetName());
			return;
		}
		if (base != feature.getBaseFeature()) {
			// A chamfer runs along edges of the one shape it is built on: an edge of
			// another feature is not something it could carry.
			CORE_WARN(
				"[Chamfer] {0}: the edges have to come from '{1}', the shape the chamfer "
				"is built on",
				feature.GetName(),
				feature.getBaseFeature() != nullptr
					? feature.getBaseFeature()->GetName()
					: "the shape below");
			return;
		}
		std::vector<std::string> references = feature.getSubValues();
		for (const std::string& reference : picked) {
			if (reference.rfind("Edge_", 0) != 0) {
				continue;  // a chamfer runs along edges; a face or a whole object is not one
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

	void ChamferTask::removeSelectedEdges()
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
				"[Chamfer] {0}: select the rows to remove in the list first",
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

	void ChamferTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
		// Picking an edge while the panel is open adds it, which is the same thing the
		// button does - a picked edge is what the user means by it.
		applyPickedEdges();
	}

}
