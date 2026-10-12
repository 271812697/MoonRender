#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
namespace MOON {
	class ChamferTask : public ParamTaskDialog, public ShapeHelper {
	public:
		explicit ChamferTask(QWidget* parent = nullptr, Feature* feature = nullptr);
		~ChamferTask();
		virtual QVariant getParamValue(const QString& propertyName);
		virtual void setParamValue(const QString& propertyName, const QVariant& value);
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
	private:
		void onWidgetLengthInvoke1();
		void onWidgetLengthInvoke2();
		/** Lists the edges this chamfer is applied to (the References group of the panel). */
		void refreshReferenceRows();
		/** Takes the edges the viewport has selected and adds them to the chamfer. */
		void applyPickedEdges();
		/** Drops the rows that are selected in the list from the chamfer. */
		void removeSelectedEdges();
		/** An edge picked while the panel is open is added to the chamfer - the same
		 * gesture as pressing "Add from Selection", without the click. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;
		class Internal;
		Internal* mInternal = nullptr;
	};
}
