#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
namespace MOON {
	enum PolarPatternType
	{
		PolarWhole,
		PolarFeature
	};
	class PolarPatternTask : public ParamTaskDialog,public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit PolarPatternTask(QWidget* parent = nullptr, PolarPatternType type=PolarWhole, Feature* feature=nullptr);
		virtual ~PolarPatternTask()override;
		
		virtual QVariant getParamValue(const QString& propertyName)override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value)override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	private:
		class Internal;
		Internal* mInternal = nullptr;
	};
}