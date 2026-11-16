#include "editor/UI/TaskPanel/DatumLineTask.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/FVec3Property.h"
#include "feature/DatumLineFeature.h"
#include "feature/Feature.h"
#include "core/component/TopoShapeActor.h"
#include "core/component/CTopoShape.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "renderer/SceneView.h"
#include "editor/UI/TreeViewPanel/treesync.h"
#include <Core/Global/ServiceLocator.h>
#include <Maths/FVector3.h>

namespace MOON
{
	class DatumLineTask::Internal
	{
	public:
		Internal(DatumLineTask* s, Feature* feature) : self(s)
		{
			if (feature) {
				line = dynamic_cast<DatumLineFeature*>(feature);
			}
			else {
				line = new DatumLineFeature("DatumLine");
				isCreatedFeature = true;
			}

			if (line) {
				// Remember the committed values so Cancel can restore an
				// already-existing datum line that was edited in this task.
				backupOrigin = line->origin;
				backupDirection = line->direction;
				backupLength = line->length;
			}
		}
		~Internal()
		{
		}

	private:
		friend DatumLineTask;
		DatumLineTask* self = nullptr;
		DatumLineFeature* line = nullptr;
		Maths::FVector3 backupOrigin;
		Maths::FVector3 backupDirection;
		float backupLength = 0.0f;
		bool isCreatedFeature = false;
	};

	DatumLineTask::DatumLineTask(QWidget* parent, Feature* feature)
		: ParamTaskDialog(parent), mInternal(new Internal(this, feature))
	{
		PropertyComponent* p = addGroupParam("Datum Line");
		FVec3Property* origin = new FVec3Property("Origin", p);
		addParam(origin);
		FVec3Property* direction = new FVec3Property("Direction", p);
		addParam(direction);
		SliderFloatProperty* length = new SliderFloatProperty("Length", p);
		length->setMinMax(0.1f, 1000.0f);
		length->setStep(0.1f);
		addParam(length);

		buildUi();
		updatePreview();
	}

	DatumLineTask::~DatumLineTask()
	{
		clearPreview();
		delete mInternal;
	}

	QVariant DatumLineTask::getParamValue(const QString& propertyName)
	{
		if (!mInternal->line) {
			return QVariant();
		}
		if (propertyName == "Datum Line:Origin") {
			return QVariant::fromValue(mInternal->line->origin);
		}
		if (propertyName == "Datum Line:Direction") {
			return QVariant::fromValue(mInternal->line->direction);
		}
		if (propertyName == "Datum Line:Length") {
			return QVariant::fromValue(mInternal->line->length);
		}
		return QVariant();
	}

	void DatumLineTask::setParamValue(const QString& propertyName, const QVariant& value)
	{
		if (!mInternal->line) {
			return;
		}
		bool needsPreview = false;
		if (propertyName == "Datum Line:Origin") {
			mInternal->line->origin = value.value<Maths::FVector3>();
			needsPreview = true;
		}
		else if (propertyName == "Datum Line:Direction") {
			mInternal->line->direction = value.value<Maths::FVector3>();
			needsPreview = true;
		}
		else if (propertyName == "Datum Line:Length") {
			mInternal->line->length = value.toFloat();
			needsPreview = true;
		}

		if (needsPreview && hasInitUi) {
			updatePreview();
		}
	}

	void DatumLineTask::updatePreview()
	{
		if (!mInternal->line) {
			return;
		}
		if (!mInternal->line->execute()) {
			CORE_ERROR("Datum line preview failed");
			return;
		}

		auto& view = GetService(Editor::Panels::SceneView);
		auto scene = view.GetScene();
		auto preActor = scene->FindActorByName("DatumLinePreview");
		if (!preActor) {
			preActor = new TopoActor("DatumLinePreview", "TopoShape", true);
		}
		auto topoComp = preActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();
		topo.setShape(mInternal->line->GetTopoShape().getShape());
		topoComp->discretizationShape();
	}

	void DatumLineTask::clearPreview()
	{
		auto& view = GetService(Editor::Panels::SceneView);
		auto scene = view.GetScene();
		auto preActor = scene->FindActorByName("DatumLinePreview");
		if (preActor) {
			GetTreeViewSync.Remove(preActor);
			scene->RemoveActor(preActor);
			delete preActor;
		}
	}

	void DatumLineTask::clickOk()
	{
		if (!mInternal->line) {
			return;
		}
		mInternal->line->execute();
		mInternal->line->makeDone();
		clearPreview();
	}

	void DatumLineTask::clickApply()
	{
		// Apply keeps the preview alive so the same task can be fine-tuned.
	}

	void DatumLineTask::clickCancel()
	{
		clearPreview();
		if (mInternal->isCreatedFeature) {
			mInternal->line->RemoveFromScene();
			delete mInternal->line;
		}
		else if (mInternal->line) {
			// Restore the values the feature had when the task was opened.
			mInternal->line->origin = mInternal->backupOrigin;
			mInternal->line->direction = mInternal->backupDirection;
			mInternal->line->length = mInternal->backupLength;
			mInternal->line->execute();
			mInternal->line->GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
		}
	}
}
