#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include "feature/LinearPatternFeature.h"
#include <vector>
namespace MOON {
	class LinearPatternTask : public ParamTaskDialog,public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit LinearPatternTask(
			QWidget* parent = nullptr,
			TransformMode mode = TransformMode::Feature,
			Feature* feature = nullptr
		);
		virtual ~LinearPatternTask()override;

		virtual QVariant getParamValue(const QString& propertyName)override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value)override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	protected:
		/** A straight edge picked as the direction of the pattern. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge)override;
		/** A planar face picked as the direction: its normal is used. */
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face)override;
	private:
		/** Draws the preview again, picking the preview material that fits what the
		 * pattern does to the shape below it. */
		void refreshPreview();
		class Internal;
		Internal* mInternal = nullptr;
	};
}
