#include "editor/UI/TaskPanel/ChamferTask.h"
#include "editor/UI/TaskPanel/ChamferInteraction.h"
#include "TaskBox.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/EnumProperty.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "feature/ChamferFeature.h"
#include "core/log.h"

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
		explicit Internal(ChamferTask* s) : self(s) {
		}
		~Internal() {
		}
	private:
		SliderFloatProperty* sizeProp = nullptr;
		/** The edge list of the panel (see refreshReferenceRows). */
		QListWidget* edgeList = nullptr;
		QLabel* edgeLabel = nullptr;
		QLabel* edgeHint = nullptr;
		friend ChamferTask;
		ChamferTask* self = nullptr;
	};

	BaseTaskDialog* MakeChamferPanel(std::unique_ptr<FeatureInteraction> p_interaction)
	{
		auto chamfer = std::unique_ptr<ChamferInteraction>(
			static_cast<ChamferInteraction*>(p_interaction.release()));
		return new ChamferTask(std::move(chamfer));
	}

	ChamferTask::ChamferTask(std::unique_ptr<ChamferInteraction> p_chamfer, QWidget* parent)
		: ParamTaskDialog(parent), m_chamfer(std::move(p_chamfer)), mInternal(new Internal(this))
	{
		PropertyComponent* p = addGroupParam("Chamfer");

		EnumProperty* type = new EnumProperty("Type", p);
		// The combo starts on the type the chamfer holds: without this it always showed the first
		// entry, whatever a chamfer opened from the tree was built with.
		type->setInitIndex(feature() != nullptr ? feature()->chamferType : 0);
		addParam(type);
		mInternal->sizeProp = new SliderFloatProperty("Size", p);
		// The size a drag starts from can be much smaller than the tenth the old panel began at:
		// the handle sets the value, and the slider has to be able to show it.
		mInternal->sizeProp->setMinMax(0.001f, 10.0f);
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

		if (feature() == nullptr) {
			return;
		}
		// The handles are the interaction's; a drag already put the value on the feature, the
		// panel only has to show it.
		m_chamfer->SetParamChangedCallback([this]() { refreshSize(); });
		// An edge picked while the panel is open is added to the chamfer by the interaction; the
		// list of edges is what follows.
		m_chamfer->SetEdgesChangedCallback([this]() { refreshReferenceRows(); });

		// The edges the chamfer is applied to, listed with the buttons that take them
		// from the selection - the same shape the sweep's path list and the fillet's
		// edge list have.
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
			if (feature() == nullptr) {
				return;
			}
			feature()->setSubValues({});
			feature()->setReferenceNames({});
			refreshReferenceRows();
			m_chamfer->UpdatePreview();
		});
		refreshReferenceRows();
	}

	ChamferTask::~ChamferTask() {
		delete mInternal;
	}

	ChamferFeature* ChamferTask::feature() const
	{
		return m_chamfer != nullptr ? m_chamfer->GetChamfer() : nullptr;
	}

	void ChamferTask::refreshSize()
	{
		if (mInternal->sizeProp != nullptr && m_chamfer != nullptr) {
			mInternal->sizeProp->updateWidgetValue(m_chamfer->GetSize());
		}
	}

	QVariant ChamferTask::getParamValue(const QString& propertyName) {
		if (propertyName == "Chamfer:Type") {
			QList<QString> list = { "Equal distance", "Two distances", "Distance and Angle" };
			return QVariant::fromValue(list);
		}
		if (feature() == nullptr) {
			return QVariant();
		}
		if (propertyName == "Chamfer:Size") {
			return QVariant::fromValue(feature()->size);
		}
		else if (propertyName == "Chamfer:Size2") {
			return QVariant::fromValue(feature()->size2);
		}
		else if (propertyName == "Chamfer:Angle") {
			return QVariant::fromValue(feature()->angle);
		}
		else if (propertyName == "Chamfer:Flip Direction") {
			return QVariant::fromValue(feature()->flipDirection);
		}
		else if (propertyName == "Chamfer:Use ALL Edges") {
			return QVariant::fromValue(feature()->useAllEdges);
		}
		return QVariant();
	}

	void ChamferTask::setParamValue(const QString& propertyName, const QVariant& value) {
		if (feature() == nullptr) {
			return;
		}
		bool updatePreView = false;
		if (propertyName == "Chamfer:Type") {
			feature()->chamferType = value.value<int>();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Size") {
			// The size goes through the interaction: the arrows are the same value on screen.
			m_chamfer->SetSize(value.toFloat());
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Size2") {
			feature()->size2 = value.toFloat();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Angle") {
			feature()->angle = value.toFloat();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Flip Direction") {
			feature()->flipDirection = value.value<bool>();
			updatePreView = true;
		}
		else if (propertyName == "Chamfer:Use ALL Edges") {
			feature()->useAllEdges = value.value<bool>();
			// The list is not what the chamfer works from while every edge is used: the label and
			// the rows follow the flag instead of waiting for the next rebuild.
			refreshReferenceRows();
			updatePreView = true;
		}
		if (updatePreView) {
			m_chamfer->UpdatePreview();
		}
	}

	void ChamferTask::clickOk() {
		m_chamfer->Commit();
	}

	void ChamferTask::clickApply() {
	}

	void ChamferTask::clickCancel() {
		m_chamfer->Abort();
	}

	void ChamferTask::refreshReferenceRows()
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
			// With "Use ALL Edges" the list is not what the chamfer works from, so it
			// is shown but not something to add to or take from.
			mInternal->edgeList->setEnabled(!feature()->useAllEdges
				&& !references.empty());
		}
	}

	void ChamferTask::applyPickedEdges()
	{
		if (feature() == nullptr) {
			return;
		}
		ChamferFeature& chamferFeature = *feature();
		Feature* base = nullptr;
		std::vector<std::string> picked;
		if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr) {
			CORE_WARN(
				"[Chamfer] {0}: select the edge(s) in the viewport or in the tree first",
				chamferFeature.GetName());
			return;
		}
		if (base != chamferFeature.getBaseFeature()) {
			// A chamfer runs along edges of the one shape it is built on: an edge of
			// another feature is not something it could carry.
			CORE_WARN(
				"[Chamfer] {0}: the edges have to come from '{1}', the shape the chamfer "
				"is built on",
				chamferFeature.GetName(),
				chamferFeature.getBaseFeature() != nullptr
					? chamferFeature.getBaseFeature()->GetName()
					: "the shape below");
			return;
		}
		std::vector<std::string> references = chamferFeature.getSubValues();
		for (const std::string& reference : picked) {
			if (reference.rfind("Edge_", 0) != 0) {
				continue;  // a chamfer runs along edges; a face or a whole object is not one
			}
			if (std::find(references.begin(), references.end(), reference) == references.end()) {
				references.push_back(reference);
			}
		}
		if (references.size() == chamferFeature.getSubValues().size()) {
			return;  // nothing new was selected
		}
		// The mapped names stay with their reference: the ones already there are kept,
		// the new edges start without any and are resolved - and named - on the next build.
		std::vector<std::vector<std::string>> names = chamferFeature.getReferenceNames();
		names.resize(references.size());
		chamferFeature.setSubValues(references);
		chamferFeature.setReferenceNames(names);
		refreshReferenceRows();
		m_chamfer->UpdatePreview();
	}

	void ChamferTask::removeSelectedEdges()
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
				"[Chamfer] {0}: select the rows to remove in the list first",
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
		m_chamfer->UpdatePreview();
	}

}
