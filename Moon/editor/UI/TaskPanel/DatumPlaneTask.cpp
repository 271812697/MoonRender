#include "editor/UI/TaskPanel/DatumPlaneTask.h"

#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "feature/DatumPlaneFeature.h"
#include "feature/Feature.h"
#include "core/component/CTopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/FVec3Property.h"
#include "Widgets/SliderFloatProperty.h"

#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

namespace MOON
{
	class DatumPlaneTask::Internal
	{
	public:
		Internal(DatumPlaneTask* p_self, Feature* p_feature)
			: self(p_self)
		{
			if (p_feature != nullptr) {
				plane = dynamic_cast<DatumPlaneFeature*>(p_feature);
			}
			else {
				plane = new DatumPlaneFeature("DatumPlane");
				isCreatedFeature = true;
				// A new plane starts on the shape the user has selected, so the
				// panel opens on the body they were working on.
				Feature* base = nullptr;
				std::vector<std::string> references;
				if (ViewTool::getSelectedBasedFeature(base, references) && base != nullptr) {
					plane->setBaseFeature(base);
				}
				// The preview and the commit go through the shape helper, so it has
				// to know the feature this panel created.
				self->setFeature(plane);
			}
			if (plane == nullptr) {
				return;
			}

			backupMode = plane->mapMode;
			backupReferences = plane->getSubValues();
			backupReferenceNames = plane->getReferenceNames();
			backupOffset = plane->offset;
			backupRotation = plane->rotation;
			backupAutomaticSize = plane->automaticSize;
			backupLength = plane->length;
			backupWidth = plane->width;
			backupOrigin = plane->origin;
			backupNormal = plane->normal;
			backupXAxis = plane->xAxis;
		}

		~Internal()
		{
		}

		/** The row of the mode in the list the panel shows. */
		int modeRow() const
		{
			const std::vector<DatumPlaneFeature::MapMode>& modes
				= DatumPlaneFeature::allMapModes();
			for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
				if (modes[i] == plane->mapMode) {
					return i;
				}
			}
			return 0;
		}

		/** What the current mode expects the user to pick, in one line. */
		QString supportHint() const
		{
			switch (plane->mapMode) {
			case DatumPlaneFeature::MapMode::Deactivated:
				return QObject::tr("Nothing to pick: the offset places the plane.");
			case DatumPlaneFeature::MapMode::ObjectXY:
			case DatumPlaneFeature::MapMode::ObjectXZ:
			case DatumPlaneFeature::MapMode::ObjectYZ:
				return QObject::tr("Parallel to a global plane, through the body.");
			case DatumPlaneFeature::MapMode::FlatFace:
				return QObject::tr("Pick one flat face.");
			case DatumPlaneFeature::MapMode::ThreePoints:
				return QObject::tr("Pick three points or vertices (an edge counts as two).");
			case DatumPlaneFeature::MapMode::NormalToEdge:
				return QObject::tr("Pick one edge, and a vertex to stand at.");
			}
			return QString();
		}

		friend DatumPlaneTask;
		DatumPlaneTask* self = nullptr;
		DatumPlaneFeature* plane = nullptr;
		bool isCreatedFeature = false;
		QListWidget* supportList = nullptr;
		QLabel* supportHintLabel = nullptr;
		EnumProperty* modeProperty = nullptr;

		DatumPlaneFeature::MapMode backupMode = DatumPlaneFeature::MapMode::ObjectXY;
		std::vector<std::string> backupReferences;
		std::vector<std::vector<std::string>> backupReferenceNames;
		Maths::FVector3 backupOffset{ 0.0f, 0.0f, 0.0f };
		float backupRotation = 0.0f;
		bool backupAutomaticSize = true;
		float backupLength = 40.0f;
		float backupWidth = 40.0f;
		Maths::FVector3 backupOrigin{ 0.0f, 0.0f, 0.0f };
		Maths::FVector3 backupNormal{ 0.0f, 0.0f, 1.0f };
		Maths::FVector3 backupXAxis{ 1.0f, 0.0f, 0.0f };
	};

	DatumPlaneTask::DatumPlaneTask(QWidget* parent, Feature* feature)
		: ParamTaskDialog(parent), ShapeHelper(feature), mInternal(new Internal(this, feature))
	{
		// The plane is a reference, so it is drawn transparent and on top of the
		// body instead of hiding it.
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
		mPreviewOption.r = 0.25f;
		mPreviewOption.g = 0.55f;
		mPreviewOption.b = 1.0f;
		mPreviewOption.a = 0.55f;

		PropertyComponent* attachment = addGroupParam("Attachment");
		mInternal->modeProperty = new EnumProperty("Map Mode", attachment);
		addParam(mInternal->modeProperty);
		if (mInternal->plane != nullptr) {
			// The mode the plane already has is the one the combo box has to start
			// on: the property hands out the list, not the selection, and its editor
			// is built by buildUi() below.
			mInternal->modeProperty->setInitIndex(mInternal->modeRow());
		}

		PropertyComponent* offsetGroup = addGroupParam("Attachment Offset");
		addParam(new FVec3Property("Offset", offsetGroup));
		auto* rotation = new SliderFloatProperty("Rotation", offsetGroup, -180.0f, 180.0f);
		rotation->setStep(0.5f);
		addParam(rotation);

		PropertyComponent* sizeGroup = addGroupParam("Size");
		addParam(new BoolProperty(
			"Automatic Size", sizeGroup, BoolProperty::Style::InviwoRect));
		auto* length = new SliderFloatProperty("Length", sizeGroup, 0.1f, 1000.0f);
		length->setStep(0.1f);
		addParam(length);
		auto* width = new SliderFloatProperty("Width", sizeGroup, 0.1f, 1000.0f);
		width->setStep(0.1f);
		addParam(width);

		buildUi();

		if (mInternal->plane == nullptr) {
			return;
		}

		// The support rows live under the mode inside the attachment group, the way
		// the sketch panel nests its own lists.
		auto* supportRow = new QWidget(this);
		auto* supportLayout = new QVBoxLayout(supportRow);
		supportLayout->setContentsMargins(0, 0, 0, 0);
		supportLayout->setSpacing(2);
		auto* buttons = new QHBoxLayout();
		buttons->setContentsMargins(0, 0, 0, 0);
		auto* addButton = new QToolButton(supportRow);
		addButton->setText(tr("Add from Selection"));
		auto* removeButton = new QToolButton(supportRow);
		removeButton->setText(tr("Remove"));
		buttons->addWidget(addButton);
		buttons->addWidget(removeButton);
		buttons->addStretch();
		supportLayout->addLayout(buttons);
		mInternal->supportList = new QListWidget(supportRow);
		mInternal->supportList->setMinimumHeight(60);
		mInternal->supportList->setStyleSheet(
			"QListWidget { background: transparent; border: none; outline: 0; }"
			"QListWidget::item { height: 20px; padding-left: 2px; }"
			"QListWidget::item:selected { background-color: #7ab2e8; color: white; }"
		);
		supportLayout->addWidget(mInternal->supportList);
		mInternal->supportHintLabel = new QLabel(supportRow);
		mInternal->supportHintLabel->setWordWrap(true);
		mInternal->supportHintLabel->setStyleSheet("color: #a8a8a8;");
		supportLayout->addWidget(mInternal->supportHintLabel);

		const auto groupIndex = groupToIndex.find("Attachment");
		if (groupIndex != groupToIndex.end()) {
			m_comps[groupIndex->second].first->addSubWidget(supportRow);
		}

		connect(addButton, &QToolButton::clicked, this, [this]() {
			applyPickedReference();
		});
		connect(removeButton, &QToolButton::clicked, this, [this]() {
			if (mInternal->plane == nullptr || mInternal->supportList == nullptr) {
				return;
			}
			const int row = mInternal->supportList->currentRow();
			std::vector<std::string> references = mInternal->plane->getSubValues();
			std::vector<std::vector<std::string>> names
				= mInternal->plane->getReferenceNames();
			if (row < 0 || row >= static_cast<int>(references.size())) {
				return;
			}
			references.erase(references.begin() + row);
			if (row < static_cast<int>(names.size())) {
				names.erase(names.begin() + row);
			}
			mInternal->plane->setSubValues(references);
			mInternal->plane->setReferenceNames(names);
			refreshSupportList();
			updatePreview();
		});

		refreshSupportList();
		updatePreview();
	}

	DatumPlaneTask::~DatumPlaneTask()
	{
		delete mInternal;
	}

	QVariant DatumPlaneTask::getParamValue(const QString& propertyName)
	{
		if (mInternal->plane == nullptr) {
			return QVariant();
		}
		DatumPlaneFeature& plane = *mInternal->plane;
		if (propertyName == "Attachment:Map Mode") {
			QList<QString> labels;
			for (DatumPlaneFeature::MapMode mode : DatumPlaneFeature::allMapModes()) {
				labels.push_back(DatumPlaneFeature::mapModeLabel(mode));
			}
			return QVariant::fromValue(labels);
		}
		if (propertyName == "Attachment Offset:Offset") {
			return QVariant::fromValue(plane.offset);
		}
		if (propertyName == "Attachment Offset:Rotation") {
			return QVariant::fromValue(plane.rotation);
		}
		if (propertyName == "Size:Automatic Size") {
			return QVariant::fromValue(plane.automaticSize);
		}
		if (propertyName == "Size:Length") {
			return QVariant::fromValue(plane.length);
		}
		if (propertyName == "Size:Width") {
			return QVariant::fromValue(plane.width);
		}
		return QVariant();
	}

	void DatumPlaneTask::setParamValue(const QString& propertyName, const QVariant& value)
	{
		if (mInternal->plane == nullptr) {
			return;
		}
		DatumPlaneFeature& plane = *mInternal->plane;
		bool changed = false;
		if (propertyName == "Attachment:Map Mode") {
			const std::vector<DatumPlaneFeature::MapMode>& modes
				= DatumPlaneFeature::allMapModes();
			const int row = value.toInt();
			if (row >= 0 && row < static_cast<int>(modes.size())) {
				plane.mapMode = modes[row];
				// A mode that reads fewer references than the last one keeps the
				// ones it can use, so switching back and forth is not destructive.
				const int wanted = plane.requiredReferenceCount();
				std::vector<std::string> references = plane.getSubValues();
				std::vector<std::vector<std::string>> names = plane.getReferenceNames();
				if (wanted == 0) {
					references.clear();
					names.clear();
				}
				else if (static_cast<int>(references.size()) > wanted) {
					references.resize(wanted);
					if (static_cast<int>(names.size()) > wanted) {
						names.resize(wanted);
					}
				}
				plane.setSubValues(references);
				plane.setReferenceNames(names);
				refreshSupportList();
				changed = true;
			}
		}
		else if (propertyName == "Attachment Offset:Offset") {
			plane.offset = value.value<Maths::FVector3>();
			changed = true;
		}
		else if (propertyName == "Attachment Offset:Rotation") {
			plane.rotation = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Size:Automatic Size") {
			plane.automaticSize = value.value<bool>();
			changed = true;
		}
		else if (propertyName == "Size:Length") {
			plane.length = value.toFloat();
			changed = true;
		}
		else if (propertyName == "Size:Width") {
			plane.width = value.toFloat();
			changed = true;
		}
		if (changed && hasInitUi) {
			updatePreview();
		}
	}

	void DatumPlaneTask::clickOk()
	{
		if (mInternal->plane == nullptr) {
			return;
		}
		// A datum is not the tip of the body: the body stays visible and the plane
		// is added next to it, which is why this does not go through the "final
		// shape" of the modelling panels (that one hides everything below).
		mInternal->plane->execute();
		mInternal->plane->makeDone();
		clearPreviewShape();
	}

	void DatumPlaneTask::clickApply()
	{
		// Apply keeps the preview alive so the same panel can be fine-tuned.
	}

	void DatumPlaneTask::clickCancel()
	{
		clearPreviewShape();
		if (mInternal->plane == nullptr) {
			return;
		}
		if (mInternal->isCreatedFeature) {
			mInternal->plane->RemoveFromScene();
			delete mInternal->plane;
			mInternal->plane = nullptr;
			setFeature(nullptr);
			return;
		}
		// An existing plane goes back to what it was when the panel opened.
		DatumPlaneFeature& plane = *mInternal->plane;
		plane.mapMode = mInternal->backupMode;
		plane.setSubValues(mInternal->backupReferences);
		plane.setReferenceNames(mInternal->backupReferenceNames);
		plane.offset = mInternal->backupOffset;
		plane.rotation = mInternal->backupRotation;
		plane.automaticSize = mInternal->backupAutomaticSize;
		plane.length = mInternal->backupLength;
		plane.width = mInternal->backupWidth;
		plane.origin = mInternal->backupOrigin;
		plane.normal = mInternal->backupNormal;
		plane.xAxis = mInternal->backupXAxis;
		plane.execute();
		plane.GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
	}

	void DatumPlaneTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
		applyPickedReference();
	}

	void DatumPlaneTask::onSelectFace(const std::vector<Part::TopoShape>& face)
	{
		applyPickedReference();
	}

	void DatumPlaneTask::onSelectVertex(const std::vector<Part::TopoShape>& vertex)
	{
		applyPickedReference();
	}

	void DatumPlaneTask::applyPickedReference()
	{
		if (mInternal->plane == nullptr) {
			return;
		}
		DatumPlaneFeature& plane = *mInternal->plane;
		const int wanted = plane.requiredReferenceCount();
		if (wanted == 0) {
			CORE_INFO(
				"[DatumPlane] {0}: '{1}' takes no support",
				plane.GetName(),
				DatumPlaneFeature::mapModeName(plane.mapMode));
			return;
		}

		Feature* base = nullptr;
		std::vector<std::string> picked;
		if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr
			|| picked.empty()) {
			CORE_WARN(
				"[DatumPlane] {0}: select the sub-shape to attach to first",
				plane.GetName());
			return;
		}

		std::vector<std::string> references = plane.getSubValues();
		std::vector<std::vector<std::string>> names = plane.getReferenceNames();
		if (wanted == 1) {
			// One reference: the pick replaces whatever was there, so the panel
			// always shows the face or the edge the plane is really on. The
			// selection is a set, so a single pick - the normal way to attach - is
			// the only case whose order means anything; with several selected the
			// first one that the scene handed over is taken.
			references.assign(1, picked.front());
			names.clear();
			names.resize(1);
		}
		else {
			// Three points: they accumulate, the way the three-point attachment is
			// picked point by point; a full list starts over.
			if (static_cast<int>(references.size()) >= wanted) {
				references.clear();
				names.clear();
			}
			for (const std::string& reference : picked) {
				if (static_cast<int>(references.size()) >= wanted) {
					break;
				}
				references.push_back(reference);
				names.resize(references.size());
			}
		}

		plane.setBaseFeature(base);
		plane.setSubValues(references);
		plane.setReferenceNames(names);
		refreshSupportList();
		updatePreview();
	}

	void DatumPlaneTask::refreshSupportList()
	{
		if (mInternal->plane == nullptr || mInternal->supportList == nullptr) {
			return;
		}
		mInternal->supportList->clear();
		const std::vector<std::string>& references = mInternal->plane->getSubValues();
		for (int i = 0; i < static_cast<int>(references.size()); ++i) {
			mInternal->supportList->addItem(QString("%1  %2")
				.arg(i)
				.arg(QString::fromStdString(references[i])));
		}
		if (mInternal->supportHintLabel != nullptr) {
			mInternal->supportHintLabel->setText(mInternal->supportHint());
		}
	}

	void DatumPlaneTask::updatePreview()
	{
		if (mInternal->plane == nullptr) {
			return;
		}
		previewShape();
	}
}
