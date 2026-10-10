#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
namespace MOON {
	class FilletTask : public ParamTaskDialog, public ShapeHelper
	{

	public:
		explicit FilletTask(QWidget* parent = nullptr,Feature* feature=nullptr);
		~FilletTask();
		virtual QVariant getParamValue(const QString& propertyName);
		virtual void setParamValue(const QString& propertyName, const QVariant& value);
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	private:
		void onWidgetLengthInvoke1();
		void onWidgetLengthInvoke2();
		/** Lists the edges this fillet is applied to (the References group of the panel). */
		void refreshReferenceRows();
		/** Takes the edges the viewport has selected and adds them to the fillet. */
		void applyPickedEdges();
		/** Drops the rows that are selected in the list from the fillet. */
		void removeSelectedEdges();
		/** An edge picked while the panel is open is added to the fillet - the same
		 * gesture as pressing "Add from Selection", without the click. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;
		class Internal;
		Internal* mInternal = nullptr;
	};
}
