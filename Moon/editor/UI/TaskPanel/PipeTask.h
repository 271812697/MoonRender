#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include <vector>

namespace MOON
{
	class Feature;
	class PipeFeature;

	/** The panel a pipe (a swept profile) is made and edited in.
	 *
	 * It asks for the three things a sweep needs: the profile sketch, the path
	 * (a path sketch, or an edge picked on the body), and how the profile is
	 * carried along that path - FreeCAD's "Mode" and "Transition" - with the
	 * preview updated on every change. OK commits the pipe into the body, Cancel
	 * leaves the body as it was (and drops a pipe this panel created).
	 */
	class PipeTask : public ParamTaskDialog, public ShapeHelper
	{
		Q_OBJECT
	public:
		/** @param p_addSubType 0 for an additive pipe, 1 for a subtractive one.
		 * @param p_feature the feature being edited, or nullptr for a new pipe. */
		explicit PipeTask(
			QWidget* parent = nullptr,
			int p_addSubType = 0,
			Feature* p_feature = nullptr);
		virtual ~PipeTask() override;

		virtual QVariant getParamValue(const QString& propertyName) override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value) override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;

	protected:
		/** A face picked in the viewport is taken as the profile, an edge as the
		 * path - the two things a sweep is made of. */
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face) override;
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;

	private:
		/** Takes the selected sketch as the profile. */
		void applyPickedProfile();
		/** Takes the selection as the path (a sketch, or a sub-shape of a body). */
		void applyPickedSpine();
		/** Rebuilds the rows that show profile and path. */
		void refreshReferenceRows();
		/** Recomputes the sweep and shows it. */
		void updatePreview();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
