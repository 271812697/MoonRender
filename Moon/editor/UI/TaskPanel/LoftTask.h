#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include <vector>

namespace MOON
{
	class Feature;
	class LoftFeature;

	/** The panel a loft is made and edited in.
	 *
	 * It asks for the profile sketch and for the sections the loft runs through - a
	 * sketch picked in the tree, or a face of the body picked in the viewport, which is
	 * what FreeCAD's loft accepts as a section as well - plus the two switches a loft
	 * has, Ruled and Closed. Like the other modelling panels, what is shown while it is
	 * open is the preview, OK commits it into the body, and Cancel leaves the body as it
	 * was (and drops a loft this panel created).
	 */
	class LoftTask : public ParamTaskDialog, public ShapeHelper
	{
		Q_OBJECT
	public:
		/** @param p_addSubType 0 for a loft that adds material, 1 for one that takes it
		 * away.
		 * @param p_feature the feature being edited, or nullptr for a new loft. */
		explicit LoftTask(
			QWidget* parent = nullptr,
			int p_addSubType = 0,
			Feature* p_feature = nullptr);
		virtual ~LoftTask() override;

		virtual QVariant getParamValue(const QString& propertyName) override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value) override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;

	protected:
		/** A face picked in the viewport is a section of the loft, taken as the
		 * reference it was picked as; an edge is not used by a loft. */
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face) override;
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;

	private:
		/** Takes the selected sketch as the profile. */
		void applyPickedProfile();
		/** Adds what is selected - sketches, or faces of the body - to the sections. */
		void addPickedSections();
		/** Takes the current row out of the sections. */
		void removeCurrentSection();
		/** Rebuilds the rows that show the profile and the sections. */
		void refreshReferenceRows();
		/** Recomputes the loft and shows it. */
		void updatePreview();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
