#include "editor/UI/TaskPanel/PipeTask.h"

#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "feature/PipeFeature.h"
#include "feature/SketcherFeature.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/component/CTopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/FVec3Property.h"

#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <string>
#include <vector>

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
	}

	class PipeTask::Internal
	{
	public:
		Internal(PipeTask* p_self, int p_addSubType, Feature* p_feature)
			: self(p_self)
		{
			if (p_feature != nullptr) {
				pipe = dynamic_cast<PipeFeature*>(p_feature);
			}
			else {
				pipe = new PipeFeature("Pipe", p_addSubType);
				isCreatedFeature = true;
				// The body below becomes the shape the sweep is fused into (or cut
				// from), the way the modelling panels take it.
				FeatureBody::Active()->setBaseFeatureFor(pipe);
				// The profile is a sketch: the one selected, or the sketch the user
				// built last. The path is picked in the panel (or was selected before
				// opening it).
				SketcherFeature* selectedSketch
					= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
				pipe->setProfile(
					selectedSketch != nullptr
						? selectedSketch
						: SketcherObjManager::instance().GetLastSketcherFeature());
				self->setFeature(pipe);
			}
			if (pipe == nullptr) {
				return;
			}
			backupMode = pipe->mode;
			backupTransition = pipe->transition;
			backupBinormal = pipe->binormal;
			backupSpineFeature = pipe->spineFeature;
			backupReferences = pipe->getSubValues();
			backupReferenceNames = pipe->getReferenceNames();
			backupProfile = pipe->getProfile();
		}

		~Internal()
		{
		}

		int modeRow() const
		{
			const std::vector<PipeFeature::Mode>& modes = PipeFeature::allModes();
			for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
				if (modes[i] == pipe->mode) {
					return i;
				}
			}
			return 0;
		}

		int transitionRow() const
		{
			const std::vector<PipeFeature::Transition>& transitions
				= PipeFeature::allTransitions();
			for (int i = 0; i < static_cast<int>(transitions.size()); ++i) {
				if (transitions[i] == pipe->transition) {
					return i;
				}
			}
			return 0;
		}

		QString profileText() const
		{
			if (pipe->getProfile() == nullptr) {
				return QObject::tr("none - pick the profile sketch");
			}
			return QString::fromStdString(pipe->getProfile()->GetName());
		}

		QString spineText() const
		{
			if (pipe->spineFeature == nullptr) {
				return QObject::tr("none - pick a path sketch, or an edge of the body");
			}
			if (pipe->getSubValues().empty()) {
				return QObject::tr("%1 (whole shape)")
					.arg(QString::fromStdString(pipe->spineFeature->GetName()));
			}
			return QString::fromStdString(pipe->spineFeature->GetName());
		}

		friend PipeTask;
		PipeTask* self = nullptr;
		PipeFeature* pipe = nullptr;
		bool isCreatedFeature = false;
		EnumProperty* modeProperty = nullptr;
		EnumProperty* transitionProperty = nullptr;
		QLabel* profileLabel = nullptr;
		QLabel* spineLabel = nullptr;
		QListWidget* spineList = nullptr;
		QLabel* spineHint = nullptr;

		PipeFeature::Mode backupMode = PipeFeature::Mode::Standard;
		PipeFeature::Transition backupTransition = PipeFeature::Transition::Transformed;
		Maths::FVector3 backupBinormal{ 0.0f, 0.0f, 1.0f };
		Feature* backupSpineFeature = nullptr;
		std::vector<std::string> backupReferences;
		std::vector<std::vector<std::string>> backupReferenceNames;
		SketcherFeature* backupProfile = nullptr;
	};

	PipeTask::PipeTask(QWidget* parent, int p_addSubType, Feature* p_feature)
		: ParamTaskDialog(parent)
		, ShapeHelper(p_feature)
		, mInternal(new Internal(this, p_addSubType, p_feature))
	{
		PropertyComponent* sweep = addGroupParam("Sweep");
		mInternal->modeProperty = new EnumProperty("Mode", sweep);
		addParam(mInternal->modeProperty);
		mInternal->transitionProperty = new EnumProperty("Transition", sweep);
		addParam(mInternal->transitionProperty);
		addParam(new FVec3Property("Binormal", sweep));

		if (mInternal->pipe != nullptr) {
			// The combo boxes have to start on what the pipe holds: the property only
			// hands out the list of choices, and its editor is built by buildUi().
			mInternal->modeProperty->setInitIndex(mInternal->modeRow());
			mInternal->transitionProperty->setInitIndex(mInternal->transitionRow());
		}

		buildUi();

		if (mInternal->pipe == nullptr) {
			return;
		}

		// The two things a sweep needs, listed with the buttons that take them from
		// the selection - the same shape the datum plane's support list has.
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

		auto* spineRow = new QHBoxLayout();
		spineRow->setContentsMargins(0, 0, 0, 0);
		auto* spineButton = new QToolButton(references);
		spineButton->setText(tr("Set Spine from Selection"));
		auto* clearSpine = new QToolButton(references);
		clearSpine->setText(tr("Clear"));
		spineRow->addWidget(spineButton);
		spineRow->addWidget(clearSpine);
		spineRow->addStretch();
		referencesLayout->addLayout(spineRow);

		mInternal->spineLabel = new QLabel(references);
		mInternal->spineLabel->setStyleSheet("color: #c8ccd0;");
		referencesLayout->addWidget(mInternal->spineLabel);
		mInternal->spineList = new QListWidget(references);
		mInternal->spineList->setMinimumHeight(50);
		mInternal->spineList->setStyleSheet(
			"QListWidget { background: transparent; border: none; outline: 0; }"
			"QListWidget::item { height: 20px; padding-left: 2px; }"
		);
		referencesLayout->addWidget(mInternal->spineList);
		mInternal->spineHint = new QLabel(references);
		mInternal->spineHint->setWordWrap(true);
		mInternal->spineHint->setStyleSheet("color: #a8a8a8;");
		mInternal->spineHint->setText(tr(
			"Pick the path: select a path sketch in the tree, or an edge of the body, "
			"then press the button. The profile is the sketch that gets swept along it."));
		referencesLayout->addWidget(mInternal->spineHint);

		const auto groupIndex = groupToIndex.find("Sweep");
		if (groupIndex != groupToIndex.end()) {
			m_comps[groupIndex->second].first->addSubWidget(references);
		}

		connect(profileButton, &QToolButton::clicked, this, [this]() {
			applyPickedProfile();
		});
		connect(spineButton, &QToolButton::clicked, this, [this]() {
			applyPickedSpine();
		});
		connect(clearSpine, &QToolButton::clicked, this, [this]() {
			if (mInternal->pipe == nullptr) {
				return;
			}
			mInternal->pipe->spineFeature = nullptr;
			mInternal->pipe->setSubValues({});
			mInternal->pipe->setReferenceNames({});
			refreshReferenceRows();
			updatePreview();
		});

		if (mInternal->isCreatedFeature) {
			// A selection made before the panel was opened is the path, so opening
			// Additive Pipe right after picking an edge needs no second click. With
			// nothing selected there is nothing to take (and nothing to warn about).
			if (ViewTool::getLastestActorSelected() != nullptr) {
				applyPickedSpine();
			}
			if (mInternal->pipe->getProfile() != nullptr
				&& static_cast<Feature*>(mInternal->pipe->getProfile())
					== mInternal->pipe->spineFeature) {
				// The same sketch cannot sweep along itself: it is worth saying which
				// two things the panel wants rather than leaving a failing preview.
				CORE_WARN(
					"[Pipe] {0}: '{1}' is both the profile and the path; set the other "
					"one from a different sketch or edge",
					mInternal->pipe->GetName(),
					mInternal->pipe->getProfile()->GetName());
			}
		}
		refreshReferenceRows();
		updatePreview();
	}

	PipeTask::~PipeTask()
	{
		delete mInternal;
	}

	QVariant PipeTask::getParamValue(const QString& propertyName)
	{
		if (mInternal->pipe == nullptr) {
			return QVariant();
		}
		PipeFeature& pipe = *mInternal->pipe;
		if (propertyName == "Sweep:Mode") {
			QList<QString> labels;
			for (PipeFeature::Mode mode : PipeFeature::allModes()) {
				labels.push_back(PipeFeature::modeLabel(mode));
			}
			return QVariant::fromValue(labels);
		}
		if (propertyName == "Sweep:Transition") {
			QList<QString> labels;
			for (PipeFeature::Transition transition : PipeFeature::allTransitions()) {
				labels.push_back(PipeFeature::transitionLabel(transition));
			}
			return QVariant::fromValue(labels);
		}
		if (propertyName == "Sweep:Binormal") {
			return QVariant::fromValue(pipe.binormal);
		}
		return QVariant();
	}

	void PipeTask::setParamValue(const QString& propertyName, const QVariant& value)
	{
		if (mInternal->pipe == nullptr) {
			return;
		}
		PipeFeature& pipe = *mInternal->pipe;
		bool changed = false;
		if (propertyName == "Sweep:Mode") {
			const std::vector<PipeFeature::Mode>& modes = PipeFeature::allModes();
			const int row = value.toInt();
			if (row >= 0 && row < static_cast<int>(modes.size())) {
				pipe.mode = modes[row];
				changed = true;
			}
		}
		else if (propertyName == "Sweep:Transition") {
			const std::vector<PipeFeature::Transition>& transitions
				= PipeFeature::allTransitions();
			const int row = value.toInt();
			if (row >= 0 && row < static_cast<int>(transitions.size())) {
				pipe.transition = transitions[row];
				changed = true;
			}
		}
		else if (propertyName == "Sweep:Binormal") {
			pipe.binormal = value.value<Maths::FVector3>();
			changed = true;
		}
		if (changed && hasInitUi) {
			updatePreview();
		}
	}

	void PipeTask::clickOk()
	{
		generateFinalShape();
	}

	void PipeTask::clickApply()
	{
		// Apply keeps the preview alive so the same task can be fine-tuned.
	}

	void PipeTask::clickCancel()
	{
		clearPreviewShape();
		if (mInternal->pipe == nullptr) {
			return;
		}
		if (mInternal->isCreatedFeature) {
			mInternal->pipe->RemoveFromScene();
			delete mInternal->pipe;
			mInternal->pipe = nullptr;
			setFeature(nullptr);
			return;
		}
		// An existing pipe goes back to what it was when the panel opened.
		PipeFeature& pipe = *mInternal->pipe;
		pipe.mode = mInternal->backupMode;
		pipe.transition = mInternal->backupTransition;
		pipe.binormal = mInternal->backupBinormal;
		pipe.spineFeature = mInternal->backupSpineFeature;
		pipe.setSubValues(mInternal->backupReferences);
		pipe.setReferenceNames(mInternal->backupReferenceNames);
		pipe.setProfile(mInternal->backupProfile);
		pipe.execute();
		pipe.GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
	}

	void PipeTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
		applyPickedSpine();
	}

	void PipeTask::onSelectFace(const std::vector<Part::TopoShape>& face)
	{
		// A face is not a sweep profile here - the profile is a sketch - so the pick
		// is only worth a hint rather than a silent nothing.
		CORE_INFO(
			"[Pipe] {0}: the profile comes from a sketch; select the profile sketch in "
			"the tree and press 'Set Profile from Selection'",
			mInternal->pipe != nullptr ? mInternal->pipe->GetName() : "Pipe");
	}

	void PipeTask::applyPickedProfile()
	{
		if (mInternal->pipe == nullptr) {
			return;
		}
		SketcherFeature* sketch
			= dynamic_cast<SketcherFeature*>(ViewTool::getSelectedFeature());
		if (sketch == nullptr) {
			CORE_WARN(
				"[Pipe] {0}: select the profile sketch in the tree first",
				mInternal->pipe->GetName());
			return;
		}
		mInternal->pipe->setProfile(sketch);
		refreshReferenceRows();
		updatePreview();
	}

	void PipeTask::applyPickedSpine()
	{
		if (mInternal->pipe == nullptr) {
			return;
		}
		PipeFeature& pipe = *mInternal->pipe;
		Feature* base = nullptr;
		std::vector<std::string> picked;
		if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr) {
			CORE_WARN(
				"[Pipe] {0}: select the path - a path sketch, or an edge of the body - "
				"first",
				pipe.GetName());
			return;
		}
		if (base == &pipe) {
			CORE_WARN(
				"[Pipe] {0}: the sweep cannot be its own path", pipe.GetName());
			return;
		}

		// A pick that names a sub-shape is the path itself (an edge of a body); a
		// pick of a whole object means "use all of it", which is what a path sketch
		// is.
		std::vector<std::string> references;
		for (const std::string& reference : picked) {
			if (isSubShapeReference(reference)) {
				references.push_back(reference);
			}
		}
		pipe.spineFeature = base;
		pipe.setSubValues(references);
		pipe.setReferenceNames(std::vector<std::vector<std::string>>(references.size()));
		refreshReferenceRows();
		updatePreview();
	}

	void PipeTask::refreshReferenceRows()
	{
		if (mInternal->pipe == nullptr) {
			return;
		}
		if (mInternal->profileLabel != nullptr) {
			mInternal->profileLabel->setText(
				tr("Profile: %1").arg(mInternal->profileText()));
		}
		if (mInternal->spineLabel != nullptr) {
			mInternal->spineLabel->setText(tr("Spine: %1").arg(mInternal->spineText()));
		}
		if (mInternal->spineList != nullptr) {
			mInternal->spineList->clear();
			const std::vector<std::string>& references
				= mInternal->pipe->getSubValues();
			for (int i = 0; i < static_cast<int>(references.size()); ++i) {
				mInternal->spineList->addItem(QString("%1  %2")
					.arg(i)
					.arg(QString::fromStdString(references[i])));
			}
		}
	}

	void PipeTask::updatePreview()
	{
		if (mInternal->pipe == nullptr) {
			return;
		}
		previewShape();
	}
}
