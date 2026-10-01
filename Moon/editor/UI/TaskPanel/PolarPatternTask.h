#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include "feature/PolarPatternFeature.h"
#include <vector>
namespace MOON {
	class PolarPatternTask : public ParamTaskDialog,public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit PolarPatternTask(
			QWidget* parent = nullptr,
			TransformMode mode = TransformMode::Feature,
			Feature* feature = nullptr
		);
		virtual ~PolarPatternTask()override;
		
		virtual QVariant getParamValue(const QString& propertyName)override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value)override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	protected:
		/** The edge picked for the "Select Edge" axis. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge)override;
	private:
		/** Draws the preview again, picking the preview material that fits what the
		 * pattern does to the shape below it. */
		void refreshPreview();
		class Internal;
		Internal* mInternal = nullptr;
	};
}
