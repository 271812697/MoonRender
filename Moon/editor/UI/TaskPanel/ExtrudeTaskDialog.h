#pragma once
#include <memory>

#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/PadInteraction.h"

namespace MOON {
	class SliderFloatProperty;
	class FeatureInteraction;

	/** The pad's panel.
	 *
	 * It edits the parameters of a pad that already exists - the interaction it is given owns the
	 * handles, the feature and the preview. The panel builds no handle and no feature: the task
	 * bar hands it a pad started at its default size, a pick hands it the one that was just
	 * dragged open, and either way it is the same panel working on the same kind of object.
	 */
	class ExtrudeTaskDialog : public ParamTaskDialog
	{
		Q_OBJECT
	public:
		explicit ExtrudeTaskDialog(std::unique_ptr<PadInteraction> p_pad, QWidget* parent = nullptr);
		~ExtrudeTaskDialog() override;

		PadInteraction* GetPad() const { return m_pad.get(); }

		QVariant getParamValue(const QString& propertyName) override;
		void setParamValue(const QString& propertyName, const QVariant& value) override;
		void clickOk() override;
		void clickApply() override;
		void clickCancel() override;

	private:
		/** A handle was dragged: the interaction already took the value, the number follows. */
		void refresh(PadInteraction::Param p_param);

		std::unique_ptr<PadInteraction> m_pad;
		SliderFloatProperty* m_length1 = nullptr;
		SliderFloatProperty* m_angle1 = nullptr;
		SliderFloatProperty* m_length2 = nullptr;
		SliderFloatProperty* m_angle2 = nullptr;
	};

	/** The pad's panel, built for a task handed over as a generic interaction.
	 *
	 * This is what an offer carries: the operation says which interaction it is, and this turns
	 * the one it built into the panel that edits it. Every operation has a function like this one
	 * next to its panel, which is how it gets registered (see the table in SelectionTaskLauncher)
	 * without anything else having to know about it.
	 */
	BaseTaskDialog* MakePadPanel(std::unique_ptr<FeatureInteraction> p_interaction);
}
