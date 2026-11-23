#pragma once
#include <functional>
#include <memory>

#include "Interactive/Interactive/EventObject.h"
#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"

namespace MOON
{
	class ChamferFeature;
	class AxisTranslationWidget;
	class ExecuteCommand;

	/** A chamfer: the operation an edge offers beside the fillet.
	 *
	 * Same shape as the fillet's interaction (see FilletInteraction, and PadInteraction for the
	 * arrangement itself): the handles, the feature and the preview are this object's, and the
	 * panel that edits the size is a view of it.
	 *
	 * The arrows need the edge resolved inside the shape below - they sit where the chamfer face
	 * is going to meet each of the two faces next to the edge - so the feature object is made when
	 * the operation is offered (attached to that shape, not executed) and the arrows are placed
	 * from what it resolves. Nothing is previewed and nothing is committed until Begin().
	 */
	class ChamferInteraction : public ShapeHelper, public FeatureInteraction
	{
	public:
		ChamferInteraction();
		explicit ChamferInteraction(ChamferFeature* p_feature);
		~ChamferInteraction() override;

		ChamferInteraction(const ChamferInteraction&) = delete;
		ChamferInteraction& operator=(const ChamferInteraction&) = delete;

		void Attach(const Part::TopoShape& p_edge) override;
		void Begin() override;
		bool HasBegun() const override;
		void Commit() override;
		void Abort() override;
		Feature* GetFeature() override;
		void SetVisible(bool p_visible) override;
		void SetBeginCallback(std::function<void()> p_callback) override;

		ChamferFeature* GetChamfer() const { return m_feature; }

		/** Puts the size on the feature and the handles, and redraws the preview: what the panel
		 * and the edges list go through when they change the chamfer. */
		void SetSize(double p_size);
		double GetSize() const;
		/** Redraws the preview from whatever the feature holds now. */
		void UpdatePreview();
		/** A handle was dragged and the interaction already took the value. */
		void SetParamChangedCallback(std::function<void()> p_callback);
		/** The set of edges changed - an edge was picked while the panel was open and the
		 * interaction added it - so the panel's list has to be read again. */
		void SetEdgesChangedCallback(std::function<void()> p_callback);

	private:
		/** An edge was picked while the task is open: it is added to the chamfer, the same thing
		 * the panel's "Add from Selection" does with the selection. Nothing is added while the
		 * operation is only offered - there is no panel and no list yet. */
		void onSelectEdge(const std::vector<Part::TopoShape>& p_edge) override;

		/** A handle was dragged. Before the chamfer exists this is what starts it.
		 *
		 * One entry per handle, for the same reason the fillet has two: reading one fixed handle
		 * in both callbacks sets the size back to what that one holds, so the other arrow looks
		 * like it cannot be dragged.
		 */
		void onHandle1Changed();
		void onHandle2Changed();
		void setSizeFrom(AxisTranslationWidget* p_handle);
		/** Chamfers the shape by whatever size the feature holds now.
		 *
		 * A size of nothing does not build a chamfer - OCCT refuses it - and that is the state a
		 * chamfer is offered in: the shape stays exactly as it is, nothing is previewed, and the
		 * chamfer appears once the drag has reached a size the shape can actually carry. Below
		 * that the previous shape stays on screen, so what the user sees is the original edge
		 * turning into a chamfer rather than an error.
		 * @return whether a shape was produced.
		 */
		bool chamferByCurrentSize();

		/** Makes the arrows on first use, and subscribes them to this object. */
		void ensureHandles();
		/** Places both arrows on the edge the chamfer sits on. */
		bool placeHandles();
		/** Makes the feature for a chamfer that was opened from the task bar (no pick to attach
		 * to) and gives it a size that actually produces a chamfer. */
		bool buildFeature();

		ChamferFeature* m_feature = nullptr;
		/** Whether this object made the feature (and so has to delete it when the task is
		 * dropped). A chamfer that already exists is only edited. */
		bool m_createdFeature = false;
		bool m_started = false;
		AxisTranslationWidget* m_handle1 = nullptr;
		AxisTranslationWidget* m_handle2 = nullptr;
		/** The smallest size worth asking the kernel for, in model units (a share of the shape's
		 * size). Below it the chamfer is the shape itself and nothing is previewed. */
		double m_minBuildSize = 0.0;
		/** Size per unit of arrow travel.
		 *
		 * An equal-distance chamfer takes the same amount off both faces next to the edge, and the
		 * arrows walk along those faces: the distance from the edge to where the chamfer face
		 * meets a face is the size itself, at any angle between them. That is where this differs
		 * from the fillet, whose tangency point sits at r / tan(theta/2) from the edge and needs
		 * the cot(theta/2) factor on the drag. One factor here keeps the number and the picture
		 * the same thing, for a straight edge on flat faces exactly and for a curved edge as the
		 * scalar control it is.
		 */
		double m_sizePerUnit = 1.0;
		ExecuteCommandPair m_lengthObserver1{ 0, nullptr };
		ExecuteCommandPair m_lengthObserver2{ 0, nullptr };
		std::function<void()> m_onBegin;
		std::function<void()> m_onParamChanged;
		std::function<void()> m_onEdgesChanged;
	};
}
