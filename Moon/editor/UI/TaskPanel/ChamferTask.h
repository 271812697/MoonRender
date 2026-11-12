#pragma once
#include <memory>

#include "editor/UI/TaskPanel/ParamTaskDialog.h"

namespace MOON {
	class ChamferInteraction;
	class ChamferFeature;
	class FeatureInteraction;

	/** The chamfer's panel.
	 *
	 * It edits a chamfer that already exists - the interaction it is given owns the handles, the
	 * feature and the preview (see ChamferInteraction). The panel builds no handle and no feature:
	 * a chamfer picked in the viewport is handed to it, and so is one started from the task bar.
	 */
	class ChamferTask : public ParamTaskDialog
	{
		Q_OBJECT
	public:
		explicit ChamferTask(std::unique_ptr<ChamferInteraction> p_chamfer, QWidget* parent = nullptr);
		~ChamferTask() override;

		ChamferInteraction* GetChamfer() const { return m_chamfer.get(); }

		QVariant getParamValue(const QString& propertyName) override;
		void setParamValue(const QString& propertyName, const QVariant& value) override;
		void clickOk() override;
		void clickApply() override;
		void clickCancel() override;

	private:
		/** Lists the edges this chamfer is applied to (the References group of the panel). */
		void refreshReferenceRows();
		/** Takes the edges the viewport has selected and adds them to the chamfer. */
		void applyPickedEdges();
		/** Drops the rows that are selected in the list from the chamfer. */
		void removeSelectedEdges();
		/** A handle was dragged: the interaction took the value, the number follows. */
		void refreshSize();

		ChamferFeature* feature() const;

		std::unique_ptr<ChamferInteraction> m_chamfer;
		class Internal;
		Internal* mInternal = nullptr;
	};

	/** The chamfer's panel, built for a task handed over as a generic interaction (see
	 * MakeFilletPanel for the same thing on the other side of the task). */
	BaseTaskDialog* MakeChamferPanel(std::unique_ptr<FeatureInteraction> p_interaction);
}
