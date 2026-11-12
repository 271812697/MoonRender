#pragma once
#include <memory>

#include "editor/UI/TaskPanel/ParamTaskDialog.h"

namespace MOON {
	class FilletInteraction;
	class FilletFeature;
	class FeatureInteraction;

	/** The fillet's panel.
	 *
	 * It edits a fillet that already exists - the interaction it is given owns the handles, the
	 * feature and the preview (see FilletInteraction). The panel builds no handle and no feature:
	 * a fillet picked in the viewport is handed to it, and so is one started from the task bar.
	 */
	class FilletTask : public ParamTaskDialog
	{
		Q_OBJECT
	public:
		explicit FilletTask(std::unique_ptr<FilletInteraction> p_fillet, QWidget* parent = nullptr);
		~FilletTask() override;

		FilletInteraction* GetFillet() const { return m_fillet.get(); }

		QVariant getParamValue(const QString& propertyName) override;
		void setParamValue(const QString& propertyName, const QVariant& value) override;
		void clickOk() override;
		void clickApply() override;
		void clickCancel() override;

	private:
		/** Lists the edges this fillet is applied to (the References group of the panel). */
		void refreshReferenceRows();
		/** Takes the edges the viewport has selected and adds them to the fillet. */
		void applyPickedEdges();
		/** Drops the rows that are selected in the list from the fillet. */
		void removeSelectedEdges();
		/** A handle was dragged: the interaction took the value, the number follows. */
		void refreshRadius();

		FilletFeature* feature() const;

		std::unique_ptr<FilletInteraction> m_fillet;
		class Internal;
		Internal* mInternal = nullptr;
	};

	/** The fillet's panel, built for a task handed over as a generic interaction (see
	 * MakePadPanel for the same thing on the other side of the task). */
	BaseTaskDialog* MakeFilletPanel(std::unique_ptr<FeatureInteraction> p_interaction);
}
