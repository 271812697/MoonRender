#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include <vector>

namespace MOON
{
	class Feature;
	class HelixFeature;

	/** The panel a helix is made and edited in: the profile sketch, the axis it winds
	 * around (a datum line, an edge of the body, or nothing, which leaves the helix on
	 * the normal of the profile's own plane), and the parameters of FreeCAD's helix -
	 * one of its four modes decides which three of pitch, height, turns, angle and
	 * growth are the ones being given, and the panel says which those are. */
	class HelixTask : public ParamTaskDialog, public ShapeHelper
	{
		Q_OBJECT
	public:
		/** @param p_addSubType 0 for a helix that adds material, 1 for one that takes it
		 * away.
		 * @param p_feature the feature being edited, or nullptr for a new helix. */
		explicit HelixTask(
			QWidget* parent = nullptr,
			int p_addSubType = 0,
			Feature* p_feature = nullptr);
		virtual ~HelixTask() override;

		virtual QVariant getParamValue(const QString& propertyName) override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value) override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;

	protected:
		/** An edge picked in the viewport is the axis of the helix. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face) override;

	private:
		void applyPickedProfile();
		void applyPickedAxis();
		void clearAxis();
		void refreshReferenceRows();
		void updatePreview();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
