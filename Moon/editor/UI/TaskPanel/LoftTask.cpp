#include "editor/UI/TaskPanel/LoftTask.h"

#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "feature/LoftFeature.h"
#include "feature/SketcherFeature.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/SelectionManager.h"
#include "core/ViewTool.h"
#include "Core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include "core/log.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "Widgets/BoolProperty.h"
#include "renderer/SceneView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>
#include <vector>
#include <algorithm>

namespace MOON
{
	namespace
	{
		/** True when the reference names a sub-shape ("Face_3", "Edge_5", "Vertex_7")
		 * rather than a whole feature. */
		bool isSubShapeReference(const std::string& p_reference)
		{
			return p_reference.rfind("Face_", 0) == 0
				|| p_reference.rfind("Edge_", 0) == 0
				|| p_reference.rfind("Vertex_", 0) == 0;
		}

		/** The feature an actor of the selection stands for, and the sub-shape reference
		 * it was picked as. A feature picked in the tree (a sketch, say) *is* the feature
		 * - there is no reference - while a face or an edge picked in the viewport is a
		 * leaf below one, which is what ViewTool::getActorBasedFeature resolves. */
		Feature* featureOfSelectedActor(Core::ECS::Actor& p_actor, std::string& p_reference)
		{
			p_reference.clear();
			if (Feature* feature = dynamic_cast<Feature*>(&p_actor)) {
				return feature;
			}
			Feature* feature = nullptr;
			if (ViewTool::getActorBasedFeature(&p_actor, feature, p_reference)) {
				return feature;
			}
			return nullptr;
		}
	}

	class LoftTask::Internal
	{
	public:
		Internal(LoftTask* p_self, int p_addSubType, Feature* p_feature)
			: self(p_self)
		{
			if (p_feature != nullptr) {
				loft = dynamic_cast<LoftFeature*>(p_feature);
			}
			else {
				loft = new LoftFeature("Loft", p_addSubType);
				isCreatedFeature = true;
				// The body below is what the loft is fused into (or cut from), the way
				// the other modelling panels take it.
				FeatureBody::Active()->setBaseFeatureFor(loft);
				// The profile is a sketch: the one selected, or the sketch the user built
				// last. The sections are picked in the panel.
				SketcherFeature* selectedSketch
					= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
				loft->setProfile(
					selectedSketch != nullptr
						? selectedSketch
						: SketcherObjManager::instance().GetLastSketcherFeature());
				self->setFeature(loft);
			}
			if (loft == nullptr) {
				return;
			}
			backupRuled = loft->ruled;
			backupClosed = loft->closed;
			backupSections = loft->sections;
			backupReferences = loft->getSubValues();
			backupReferenceNames = loft->getReferenceNames();
			backupProfile = loft->getProfile();
		}

		~Internal()
		{
		}

		QString profileText() const
		{
			if (loft->getProfile() == nullptr) {
				return QObject::tr("none - pick the profile sketch");
			}
			return QString::fromStdString(loft->getProfile()->GetName());
		}

		friend LoftTask;
		LoftTask* self = nullptr;
		LoftFeature* loft = nullptr;
		bool isCreatedFeature = false;
		QLabel* profileLabel = nullptr;
		QLabel* sectionsLabel = nullptr;
		QListWidget* sectionsList = nullptr;

		bool backupRuled = false;
		bool backupClosed = false;
		std::vector<Feature*> backupSections;
		std::vector<std::string> backupReferences;
		std::vector<std::vector<std::string>> backupReferenceNames;
		SketcherFeature* backupProfile = nullptr;
	};

	LoftTask::LoftTask(QWidget* parent, int p_addSubType, Feature* p_feature)
		: ParamTaskDialog(parent)
		, ShapeHelper(p_feature)
		, mInternal(new Internal(this, p_addSubType, p_feature))
	{
		PropertyComponent* loft = addGroupParam("Loft");
		// The two switches a loft has, and nothing else: what it is made of is picked.
		auto* ruled = new BoolProperty("Ruled", loft);
		addParam(ruled);
		auto* closed = new BoolProperty("Closed", loft);
		addParam(closed);

		buildUi();

		if (mInternal->loft == nullptr) {
			return;
		}
		// An edited loft starts on the values it holds; a new one on the defaults.
		ruled->updateWidgetValue(mInternal->loft->ruled);
		closed->updateWidgetValue(mInternal->loft->closed);

		// The profile and the sections, listed with the buttons that take them from the
		// selection - the shape the pipe's panel has for its profile and its path.
		auto* references = new QWidget(this);
		auto* referencesLayout = new QVBoxLayout(references);
		referencesLayout->setContentsMargins(0, 0, 0, 0);
		referencesLayout->setSpacing(2);

		auto* profileRow = new QHBoxLayout();
		profileRow->setContentsMargins(0, 0, 0, 0);
		auto* profileButton = new QToolButton(references);
		profileButton->setText(tr("Set Profile from Selection"));
		mInternal->profileLabel = new QLabel(references);
		mInternal->profileLabel->setStyleSheet("color: #c8ccd0;");
		profileRow->addWidget(profileButton);
		profileRow->addWidget(mInternal->profileLabel, 1);
		referencesLayout->addLayout(profileRow);

		auto* sectionRow = new QHBoxLayout();
		sectionRow->setContentsMargins(0, 0, 0, 0);
		auto* addButton = new QToolButton(references);
		addButton->setText(tr("Add Sections from Selection"));
		auto* removeButton = new QToolButton(references);
		removeButton->setText(tr("Remove"));
		sectionRow->addWidget(addButton);
		sectionRow->addWidget(removeButton);
		sectionRow->addStretch();
		referencesLayout->addLayout(sectionRow);

		mInternal->sectionsLabel = new QLabel(references);
		mInternal->sectionsLabel->setStyleSheet("color: #c8ccd0;");
		referencesLayout->addWidget(mInternal->sectionsLabel);
		mInternal->sectionsList = new QListWidget(references);
		mInternal->sectionsList->setMinimumHeight(50);
		mInternal->sectionsList->setStyleSheet(
			"QListWidget { background: transparent; border: none; outline: 0; }"
			"QListWidget::item { height: 20px; padding-left: 2px; }"
		);
		referencesLayout->addWidget(mInternal->sectionsList);
		auto* hint = new QLabel(references);
		hint->setWordWrap(true);
		hint->setStyleSheet("color: #a8a8a8;");
		hint->setText(tr(
			"The profile is the first section; add the ones the loft runs through - "
			"select the sketches in the tree (or a face of the body) and press the "
			"button. Every section needs the same amount of wires as the profile."));
		referencesLayout->addWidget(hint);

		const auto groupIndex = groupToIndex.find("Loft");
		if (groupIndex != groupToIndex.end()) {
			m_comps[groupIndex->second].first->addSubWidget(references);
		}

		connect(profileButton, &QToolButton::clicked, this, [this]() {
			applyPickedProfile();
		});
		connect(addButton, &QToolButton::clicked, this, [this]() {
			addPickedSections();
		});
		connect(removeButton, &QToolButton::clicked, this, [this]() {
			removeCurrentSection();
		});

		if (mInternal->isCreatedFeature && ViewTool::getLastestActorSelected() != nullptr) {
			// A selection made before the panel was opened is the first section, so
			// opening Additive Loft right after picking the sketches needs no second
			// click.
			addPickedSections();
		}
		refreshReferenceRows();
		updatePreview();
	}

	LoftTask::~LoftTask()
	{
		delete mInternal;
	}

	QVariant LoftTask::getParamValue(const QString& propertyName)
	{
		if (mInternal->loft == nullptr) {
			return QVariant();
		}
		if (propertyName == "Loft:Ruled") {
			return QVariant::fromValue(mInternal->loft->ruled);
		}
		if (propertyName == "Loft:Closed") {
			return QVariant::fromValue(mInternal->loft->closed);
		}
		return QVariant();
	}

	void LoftTask::setParamValue(const QString& propertyName, const QVariant& value)
	{
		if (mInternal->loft == nullptr) {
			return;
		}
		bool changed = false;
		if (propertyName == "Loft:Ruled") {
			mInternal->loft->ruled = value.toBool();
			changed = true;
		}
		else if (propertyName == "Loft:Closed") {
			mInternal->loft->closed = value.toBool();
			changed = true;
		}
		if (changed && hasInitUi) {
			updatePreview();
		}
	}

	void LoftTask::clickOk()
	{
		generateFinalShape();
	}

	void LoftTask::clickApply()
	{
		// Apply keeps the preview alive so the same task can be fine-tuned.
	}

	void LoftTask::clickCancel()
	{
		clearPreviewShape();
		if (mInternal->loft == nullptr) {
			return;
		}
		if (mInternal->isCreatedFeature) {
			mInternal->loft->RemoveFromScene();
			delete mInternal->loft;
			mInternal->loft = nullptr;
			setFeature(nullptr);
			return;
		}
		// An existing loft goes back to what it was when the panel opened.
		LoftFeature& loft = *mInternal->loft;
		loft.ruled = mInternal->backupRuled;
		loft.closed = mInternal->backupClosed;
		loft.sections = mInternal->backupSections;
		loft.setSubValues(mInternal->backupReferences);
		loft.setReferenceNames(mInternal->backupReferenceNames);
		loft.setProfile(mInternal->backupProfile);
		loft.execute();
		loft.GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
	}

	void LoftTask::onSelectFace(const std::vector<Part::TopoShape>& face)
	{
		// A face of the body can be a section of a loft - taken as the reference it was
		// picked as, so it is read again on every recompute.
		addPickedSections();
	}

	void LoftTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
		CORE_INFO(
			"[Loft] {0}: a loft runs through sections - select a sketch, or a face of "
			"the body",
			mInternal->loft != nullptr ? mInternal->loft->GetName() : "Loft");
	}

	void LoftTask::applyPickedProfile()
	{
		if (mInternal->loft == nullptr) {
			return;
		}
		SketcherFeature* sketch
			= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
		if (sketch == nullptr) {
			CORE_WARN(
				"[Loft] {0}: select the profile sketch in the tree first",
				mInternal->loft->GetName());
			return;
		}
		if (sketch == mInternal->loft->getProfile()) {
			return;
		}
		// The profile and the sections share the reference block: the first entry is the
		// profile's - a sketch is used whole, so it stays empty - and the ones after it
		// belong to the sections, in order.
		std::vector<std::string> references = mInternal->loft->getSubValues();
		references.insert(references.begin(), std::string());
		const std::size_t wanted = mInternal->loft->sections.size() + 1;
		if (references.size() > wanted) {
			references.resize(wanted);
		}
		mInternal->loft->setProfile(sketch);
		mInternal->loft->setSubValues(references);
		mInternal->loft->setReferenceNames(
			std::vector<std::vector<std::string>>(references.size()));
		refreshReferenceRows();
		updatePreview();
	}

	void LoftTask::addPickedSections()
	{
		if (mInternal->loft == nullptr) {
			return;
		}
		LoftFeature& loft = *mInternal->loft;
		Core::SceneSystem::Scene* scene = GetSceneView.GetScene();
		if (scene == nullptr) {
			return;
		}
		int added = 0;
		for (const SelectID& id : SelectionManager::instance().getSelect()) {
			Core::ECS::Actor* actor = scene->FindActorByID(id);
			if (actor == nullptr) {
				continue;
			}
			std::string reference;
			Feature* feature = featureOfSelectedActor(*actor, reference);
			if (feature == nullptr || feature == &loft) {
				continue;
			}
			// A pick of a whole object is a section taken whole (a sketch); a pick of a
			// face keeps the reference it was picked as, so the section survives a
			// recompute of the shape below.
			const std::string sectionReference
				= isSubShapeReference(reference) ? reference : std::string();
			if (feature == loft.getProfile() && sectionReference.empty()) {
				continue;  // the profile is already the first section
			}
			loft.sections.push_back(feature);
			std::vector<std::string> references = loft.getSubValues();
			while (references.size() < loft.sections.size()) {
				references.push_back(std::string());
			}
			references[loft.sections.size() - 1] = sectionReference;
			loft.setSubValues(references);
			loft.setReferenceNames(
				std::vector<std::vector<std::string>>(references.size()));
			++added;
		}
		if (added == 0) {
			CORE_WARN(
				"[Loft] {0}: select the sections first - a sketch, or a face of the body",
				loft.GetName());
			return;
		}
		refreshReferenceRows();
		updatePreview();
	}

	void LoftTask::removeCurrentSection()
	{
		if (mInternal->loft == nullptr || mInternal->sectionsList == nullptr) {
			return;
		}
		LoftFeature& loft = *mInternal->loft;
		const int row = mInternal->sectionsList->currentRow();
		if (row < 0 || row >= static_cast<int>(loft.sections.size())) {
			return;
		}
		loft.sections.erase(loft.sections.begin() + row);
		std::vector<std::string> references = loft.getSubValues();
		if (row < static_cast<int>(references.size())) {
			references.erase(references.begin() + row);
		}
		loft.setSubValues(references);
		loft.setReferenceNames(std::vector<std::vector<std::string>>(references.size()));
		refreshReferenceRows();
		updatePreview();
	}

	void LoftTask::refreshReferenceRows()
	{
		if (mInternal->loft == nullptr) {
			return;
		}
		const LoftFeature& loft = *mInternal->loft;
		if (mInternal->profileLabel != nullptr) {
			mInternal->profileLabel->setText(
				tr("Profile: %1").arg(mInternal->profileText()));
		}
		if (mInternal->sectionsLabel != nullptr) {
			mInternal->sectionsLabel->setText(
				tr("Sections: %1").arg(static_cast<int>(loft.sections.size())));
		}
		if (mInternal->sectionsList != nullptr) {
			mInternal->sectionsList->clear();
			const std::vector<std::string>& references = loft.getSubValues();
			for (int i = 0; i < static_cast<int>(loft.sections.size()); ++i) {
				const QString name = loft.sections[i] != nullptr
					? QString::fromStdString(loft.sections[i]->GetName())
					: tr("(gone)");
				const bool hasReference = i < static_cast<int>(references.size())
					&& !references[i].empty();
				mInternal->sectionsList->addItem(
					hasReference
						? tr("%1  %2").arg(name, QString::fromStdString(references[i]))
						: name);
			}
		}
	}

	void LoftTask::updatePreview()
	{
		if (mInternal->loft == nullptr) {
			return;
		}
		if (mInternal->loft->sections.empty()) {
			// Nothing to loft through yet: the panel opens like this, and a preview that
			// says "a loft needs at least two sections" on the way in reads as a failure
			// rather than as the state the user still has to fill in.
			clearPreviewShape();
			return;
		}
		previewShape();
	}
}
