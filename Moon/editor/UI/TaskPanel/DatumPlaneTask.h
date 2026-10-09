#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
#include <vector>

namespace MOON
{
	class Feature;
	class DatumPlaneFeature;
	class EnumProperty;

	/** The panel a datum plane is made and edited in.
	 *
	 * It follows the same shape as FreeCAD's datum task: a map mode decides what
	 * the plane is attached to, the sub-shapes that mode works on are listed with
	 * the usual "add from selection" pair of buttons, and a placement offset plus
	 * the face size sit below them. Everything the panel changes is previewed in
	 * the viewport right away; OK commits it, Cancel leaves the plane as it was
	 * (and drops one that this panel created).
	 */
	class DatumPlaneTask : public ParamTaskDialog, public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit DatumPlaneTask(QWidget* parent = nullptr, Feature* feature = nullptr);
		virtual ~DatumPlaneTask() override;

		virtual QVariant getParamValue(const QString& propertyName) override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value) override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;

	protected:
		/** A reference picked in the viewport becomes the plane's support, in the
		 * way the current mode reads it. */
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge) override;
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face) override;
		virtual void onSelectVertex(const std::vector<Part::TopoShape>& vertex) override;

	private:
		/** Takes what is selected in the scene as support (a face, an edge, or the
		 * points the mode needs). */
		void applyPickedReference();
		/** Rebuilds the support rows from the references the plane holds. */
		void refreshSupportList();
		/** Recomputes the plane and shows it. */
		void updatePreview();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
