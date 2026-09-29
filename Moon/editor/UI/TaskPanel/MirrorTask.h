#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include "feature/MirrorFeature.h"
#include <vector>
namespace MOON {
	class MirrorTask : public ParamTaskDialog,public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit MirrorTask(
			QWidget* parent = nullptr,
			TransformMode mode = TransformMode::Feature,
			Feature* feature = nullptr
		);
		virtual ~MirrorTask()override;

		virtual QVariant getParamValue(const QString& propertyName)override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value)override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	protected:
		/** The face picked to use as the mirror plane. */
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face)override;
	private:
		/** Draws the preview again, picking the preview material that fits what the
		 * mirror does to the shape below it. */
		void refreshPreview();
		class Internal;
		Internal* mInternal = nullptr;
	};
}
