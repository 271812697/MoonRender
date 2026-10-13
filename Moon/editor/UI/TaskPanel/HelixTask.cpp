#include "editor/UI/TaskPanel/HelixTask.h"

#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "feature/DatumLineFeature.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "feature/HelixFeature.h"
#include "feature/SketcherFeature.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/SelectionManager.h"
#include "core/ViewTool.h"
#include "Core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include "core/log.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/SliderFloatProperty.h"
#include "renderer/SceneView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace MOON
{
	namespace
	{
		/** The feature an actor of the selection stands for, and the sub-shape reference
		 * it was picked as. A feature picked in the tree (a datum line, say) *is* the
		 * feature - there is no reference - while an edge picked in the viewport is a
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

	class HelixTask::Internal
	{
	public:
		Internal(HelixTask* p_self, int p_addSubType, Feature* p_feature)
			: self(p_self)
		{
			if (p_feature != nullptr) {
				helix = dynamic_cast<HelixFeature*>(p_feature);
			}
			else {
				helix = new HelixFeature("Helix", p_addSubType);
				isCreatedFeature = true;
				FeatureBody::Active()->setBaseFeatureFor(helix);
				SketcherFeature* selectedSketch
					= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
				helix->setProfile(
					selectedSketch != nullptr
						? selectedSketch
						: SketcherObjManager::instance().GetLastSketcherFeature());
				self->setFeature(helix);
			}
			if (helix == nullptr) {
				return;
			}
			backupMode = helix->mode;
			backupAxisMode = helix->axisMode;
			backupPitch = helix->pitch;
			backupHeight = helix->height;
			backupTurns = helix->turns;
			backupAngle = helix->angle;
			backupGrowth = helix->growth;
			backupLeftHanded = helix->leftHanded;
			backupReversed = helix->reversed;
			backupAxisFeature = helix->axisFeature;
			backupReferences = helix->getSubValues();
			backupReferenceNames = helix->getReferenceNames();
			backupProfile = helix->getProfile();
		}

		~Internal()
		{
		}

		int modeRow() const
		{
			const std::vector<HelixFeature::Mode>& modes = HelixFeature::allModes();
			for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
				if (modes[i] == helix->mode) {
					return i;
				}
			}
			return 0;
		}

		int axisRow() const
		{
			const std::vector<HelixFeature::AxisMode>& modes = HelixFeature::allAxisModes();
			for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
				if (modes[i] == helix->axisMode) {
					return i;
				}
			}
			return 1;  // the vertical sketch axis, the default
		}

		QString profileText() const
		{
			if (helix->getProfile() == nullptr) {
				return QObject::tr("none - pick the profile sketch");
			}
			return QString::fromStdString(helix->getProfile()->GetName());
		}

		QString axisText() const
		{
			if (helix->axisMode != HelixFeature::AxisMode::Reference) {
				return QObject::tr("%1, through the origin of the sketch")
					.arg(QString::fromStdString(
						HelixFeature::axisModeLabel(helix->axisMode)));
			}
			if (helix->axisFeature == nullptr) {
				return QObject::tr("none picked yet - pick a datum line or an edge");
			}
			return QObject::tr("the picked line: %1")
				.arg(QString::fromStdString(helix->axisFeature->GetName()));
		}

		friend HelixTask;
		HelixTask* self = nullptr;
		HelixFeature* helix = nullptr;
		bool isCreatedFeature = false;
		EnumProperty* modeProperty = nullptr;
		EnumProperty* axisProperty = nullptr;
		QLabel* profileLabel = nullptr;
		QLabel* axisLabel = nullptr;

		HelixFeature::Mode backupMode = HelixFeature::Mode::PitchHeightAngle;
		HelixFeature::AxisMode backupAxisMode = HelixFeature::AxisMode::Vertical;
		double backupPitch = 10.0;
		double backupHeight = 30.0;
		double backupTurns = 3.0;
		double backupAngle = 0.0;
		double backupGrowth = 0.0;
		bool backupLeftHanded = false;
		bool backupReversed = false;
		Feature* backupAxisFeature = nullptr;
		std::vector<std::string> backupReferences;
		std::vector<std::vector<std::string>> backupReferenceNames;
		SketcherFeature* backupProfile = nullptr;
	};

	HelixTask::HelixTask(QWidget* parent, int p_addSubType, Feature* p_feature)
		: ParamTaskDialog(parent)
		, ShapeHelper(p_feature)
		, mInternal(new Internal(this, p_addSubType, p_feature))
	{
		PropertyComponent* helix = addGroupParam("Helix");
		// The axis comes first, the way it does in FreeCAD's own helix panel: which line
		// the coil winds around is what everything else is measured against.
		mInternal->axisProperty = new EnumProperty("Axis", helix);
		addParam(mInternal->axisProperty);
		mInternal->modeProperty = new EnumProperty("Mode", helix);
		addParam(mInternal->modeProperty);
		// The ranges are wide enough for a bolt-sized helix and for a spring on a large
		// part; the mode decides which of them are the ones being given.
		auto* pitch = new SliderFloatProperty("Pitch", helix, 0.01f, 1000.0f);
		pitch->setStep(0.1f);
		addParam(pitch);
		auto* height = new SliderFloatProperty("Height", helix, 0.0f, 5000.0f);
		height->setStep(0.1f);
		addParam(height);
		auto* turns = new SliderFloatProperty("Turns", helix, 0.01f, 500.0f);
		turns->setStep(0.01f);
		addParam(turns);
		auto* angle = new SliderFloatProperty("Angle", helix, -89.0f, 89.0f);
		angle->setStep(0.5f);
		addParam(angle);
		auto* growth = new SliderFloatProperty("Growth", helix, -1000.0f, 1000.0f);
		growth->setStep(0.1f);
		addParam(growth);
		auto* leftHanded = new BoolProperty("LeftHanded", helix);
		addParam(leftHanded);
		auto* reversed = new BoolProperty("Reversed", helix);
		addParam(reversed);

		if (mInternal->helix != nullptr) {
			// The combo box has to start on what the helix holds: the property only hands
			// out the list of choices, and its editor is built by buildUi().
			mInternal->modeProperty->setInitIndex(mInternal->modeRow());
			mInternal->axisProperty->setInitIndex(mInternal->axisRow());
		}

		buildUi();

		if (mInternal->helix == nullptr) {
			return;
		}
		HelixFeature& feature = *mInternal->helix;
		pitch->updateWidgetValue(feature.pitch);
		height->updateWidgetValue(feature.height);
		turns->updateWidgetValue(feature.turns);
		angle->updateWidgetValue(feature.angle);
		growth->updateWidgetValue(feature.growth);
		leftHanded->updateWidgetValue(feature.leftHanded);
		reversed->updateWidgetValue(feature.reversed);

		// What a helix is made of: the profile, and the axis it winds around.
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

		auto* axisRow = new QHBoxLayout();
		axisRow->setContentsMargins(0, 0, 0, 0);
		auto* axisButton = new QToolButton(references);
		axisButton->setText(tr("Set Axis from Selection"));
		auto* clearAxisButton = new QToolButton(references);
		clearAxisButton->setText(tr("Clear"));
		axisRow->addWidget(axisButton);
		axisRow->addWidget(clearAxisButton);
		axisRow->addStretch();
		referencesLayout->addLayout(axisRow);

		mInternal->axisLabel = new QLabel(references);
		mInternal->axisLabel->setStyleSheet("color: #c8ccd0;");
		referencesLayout->addWidget(mInternal->axisLabel);
		auto* hint = new QLabel(references);
		hint->setWordWrap(true);
		hint->setStyleSheet("color: #a8a8a8;");
		hint->setText(tr(
			"The profile is the sketch that gets swept; the axis is a datum line or an "
			"edge of the body (pick it and press the button). With no axis the helix "
			"winds around the sketch's own vertical axis, through the origin of the "
			"sketch - an axis lying *in* the profile's plane, which is what the profile "
			"has to wind around."));
		referencesLayout->addWidget(hint);

		const auto groupIndex = groupToIndex.find("Helix");
		if (groupIndex != groupToIndex.end()) {
			m_comps[groupIndex->second].first->addSubWidget(references);
		}

		connect(profileButton, &QToolButton::clicked, this, [this]() {
			applyPickedProfile();
		});
		connect(axisButton, &QToolButton::clicked, this, [this]() {
			applyPickedAxis();
		});
		connect(clearAxisButton, &QToolButton::clicked, this, [this]() {
			clearAxis();
		});

		if (mInternal->isCreatedFeature && ViewTool::getLastestActorSelected() != nullptr
			&& feature.axisFeature == nullptr) {
			// A datum line (or an edge of the body) picked before the panel was opened is
			// the axis, so opening Additive Helix right after picking one needs no second
			// click - and a sketch picked the same way stays the profile.
			feature.axisMode = HelixFeature::AxisMode::Reference;
			applyPickedAxis();
			if (feature.axisFeature == nullptr) {
				// Nothing usable was selected after all: back to the sketch's own axis,
				// which is what the panel shows as its default.
				feature.axisMode = HelixFeature::AxisMode::Vertical;
			}
		}
		refreshReferenceRows();
		updatePreview();
	}

	HelixTask::~HelixTask()
	{
		delete mInternal;
	}

	QVariant HelixTask::getParamValue(const QString& propertyName)
	{
		if (mInternal->helix == nullptr) {
			return QVariant();
		}
		HelixFeature& helix = *mInternal->helix;
		if (propertyName == "Helix:Axis") {
			QList<QString> labels;
			for (HelixFeature::AxisMode axis : HelixFeature::allAxisModes()) {
				labels.push_back(HelixFeature::axisModeLabel(axis));
			}
			return QVariant::fromValue(labels);
		}
		if (propertyName == "Helix:Mode") {
			QList<QString> labels;
			for (HelixFeature::Mode mode : HelixFeature::allModes()) {
				labels.push_back(HelixFeature::modeLabel(mode));
			}
			return QVariant::fromValue(labels);
		}
		if (propertyName == "Helix:Pitch") {
			return QVariant::fromValue(static_cast<float>(helix.pitch));
		}
		if (propertyName == "Helix:Height") {
			return QVariant::fromValue(static_cast<float>(helix.height));
		}
		if (propertyName == "Helix:Turns") {
			return QVariant::fromValue(static_cast<float>(helix.turns));
		}
		if (propertyName == "Helix:Angle") {
			return QVariant::fromValue(static_cast<float>(helix.angle));
		}
		if (propertyName == "Helix:Growth") {
			return QVariant::fromValue(static_cast<float>(helix.growth));
		}
		if (propertyName == "Helix:LeftHanded") {
			return QVariant::fromValue(helix.leftHanded);
		}
		if (propertyName == "Helix:Reversed") {
			return QVariant::fromValue(helix.reversed);
		}
		return QVariant();
	}

	void HelixTask::setParamValue(const QString& propertyName, const QVariant& value)
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		HelixFeature& helix = *mInternal->helix;
		bool changed = false;
		if (propertyName == "Helix:Axis") {
			const std::vector<HelixFeature::AxisMode>& axes = HelixFeature::allAxisModes();
			const int row = value.toInt();
			if (row >= 0 && row < static_cast<int>(axes.size())) {
				helix.axisMode = axes[row];
				changed = true;
			}
		}
		else
		if (propertyName == "Helix:Mode") {
			const std::vector<HelixFeature::Mode>& modes = HelixFeature::allModes();
			const int row = value.toInt();
			if (row >= 0 && row < static_cast<int>(modes.size())) {
				helix.mode = modes[row];
				changed = true;
			}
		}
		else if (propertyName == "Helix:Pitch") {
			helix.pitch = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Helix:Height") {
			helix.height = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Helix:Turns") {
			helix.turns = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Helix:Angle") {
			helix.angle = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Helix:Growth") {
			helix.growth = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Helix:LeftHanded") {
			helix.leftHanded = value.toBool();
			changed = true;
		}
		else if (propertyName == "Helix:Reversed") {
			helix.reversed = value.toBool();
			changed = true;
		}
		if (changed && hasInitUi) {
			updatePreview();
		}
	}

	void HelixTask::clickOk()
	{
		generateFinalShape();
	}

	void HelixTask::clickApply()
	{
		// Apply keeps the preview alive so the same task can be fine-tuned.
	}

	void HelixTask::clickCancel()
	{
		clearPreviewShape();
		if (mInternal->helix == nullptr) {
			return;
		}
		if (mInternal->isCreatedFeature) {
			mInternal->helix->RemoveFromScene();
			delete mInternal->helix;
			mInternal->helix = nullptr;
			setFeature(nullptr);
			return;
		}
		// An existing helix goes back to what it was when the panel opened.
		HelixFeature& helix = *mInternal->helix;
		helix.mode = mInternal->backupMode;
		helix.axisMode = mInternal->backupAxisMode;
		helix.pitch = mInternal->backupPitch;
		helix.height = mInternal->backupHeight;
		helix.turns = mInternal->backupTurns;
		helix.angle = mInternal->backupAngle;
		helix.growth = mInternal->backupGrowth;
		helix.leftHanded = mInternal->backupLeftHanded;
		helix.reversed = mInternal->backupReversed;
		helix.axisFeature = mInternal->backupAxisFeature;
		helix.setSubValues(mInternal->backupReferences);
		helix.setReferenceNames(mInternal->backupReferenceNames);
		helix.setProfile(mInternal->backupProfile);
		helix.execute();
		helix.GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
	}

	void HelixTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
		// An edge of the body is the axis the helix winds around.
		applyPickedAxis();
	}

	void HelixTask::onSelectFace(const std::vector<Part::TopoShape>& face)
	{
		CORE_INFO(
			"[Helix] {0}: the profile comes from a sketch; select the profile sketch in "
			"the tree and press 'Set Profile from Selection'",
			mInternal->helix != nullptr ? mInternal->helix->GetName() : "Helix");
	}

	void HelixTask::applyPickedProfile()
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		SketcherFeature* sketch
			= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
		if (sketch == nullptr) {
			CORE_WARN(
				"[Helix] {0}: select the profile sketch in the tree first",
				mInternal->helix->GetName());
			return;
		}
		mInternal->helix->setProfile(sketch);
		refreshReferenceRows();
		updatePreview();
	}

	void HelixTask::applyPickedAxis()
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		HelixFeature& helix = *mInternal->helix;
		if (helix.axisMode != HelixFeature::AxisMode::Reference) {
			// Only this choice looks at a picked line (see HelixFeature::resolveAxis):
			// saying so is better than taking the pick and quietly not using it.
			CORE_WARN(
				"[Helix] {0}: the axis is '{1}' - choose 'Pick a reference (datum line or "
				"edge)' in Axis first, or the picked line is ignored",
				helix.GetName(),
				HelixFeature::axisModeLabel(helix.axisMode));
			return;
		}
		Core::SceneSystem::Scene* scene = GetSceneView.GetScene();
		if (scene == nullptr) {
			return;
		}
		Feature* axis = nullptr;
		std::string reference;
		for (const SelectID& id : SelectionManager::instance().getSelect()) {
			Core::ECS::Actor* actor = scene->FindActorByID(id);
			if (actor == nullptr) {
				continue;
			}
			std::string picked;
			Feature* feature = featureOfSelectedActor(*actor, picked);
			if (feature == nullptr || feature == &helix
				|| feature == helix.getProfile()) {
				continue;
			}
			axis = feature;
			// A pick of an edge keeps the reference it was picked as, so the axis is
			// read again on every recompute; a pick of a whole object (a datum line)
			// needs none.
			if (picked.rfind("Edge_", 0) == 0) {
				reference = picked;
			}
			break;
		}
		if (axis == nullptr) {
			CORE_WARN(
				"[Helix] {0}: select the axis first - a datum line, or an edge of the "
				"body",
				helix.GetName());
			return;
		}
		helix.axisFeature = axis;
		helix.setSubValues(reference.empty() ? std::vector<std::string>()
		                                     : std::vector<std::string>{ reference });
		helix.setReferenceNames(std::vector<std::vector<std::string>>(
			reference.empty() ? 0 : 1));
		refreshReferenceRows();
		updatePreview();
	}

	void HelixTask::clearAxis()
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		mInternal->helix->axisFeature = nullptr;
		// With the picked line gone the axis is one of the sketch's own again: clearing
		// leaves the helix on the in-plane one rather than on a reference that is not
		// there any more.
		mInternal->helix->axisMode = HelixFeature::AxisMode::Vertical;
		mInternal->helix->setSubValues({});
		mInternal->helix->setReferenceNames({});
		refreshReferenceRows();
		updatePreview();
	}

	void HelixTask::refreshReferenceRows()
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		if (mInternal->profileLabel != nullptr) {
			mInternal->profileLabel->setText(
				tr("Profile: %1").arg(mInternal->profileText()));
		}
		if (mInternal->axisLabel != nullptr) {
			mInternal->axisLabel->setText(tr("Axis: %1").arg(mInternal->axisText()));
		}
	}

	void HelixTask::updatePreview()
	{
		if (mInternal->helix == nullptr) {
			return;
		}
		previewShape();
	}
}
