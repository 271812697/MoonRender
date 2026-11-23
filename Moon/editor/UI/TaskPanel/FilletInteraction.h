#pragma once
#include <functional>
#include <memory>

#include "Interactive/Interactive/EventObject.h"
#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"

namespace MOON
{
	class FilletFeature;
	class AxisTranslationWidget;
	class ExecuteCommand;

	/** A fillet: the operation an edge offers.
	 *
	 * Same shape as the pad's interaction (see PadInteraction): the handles, the feature and the
	 * preview are this object's, and the panel that edits the radius is a view of it.
	 *
	 * The difference is what the handles need to know before anything is built. A pad's handles sit
	 * on the picked face, which the pick already describes; a fillet's two arrows sit where the
	 * rounded surface is going to meet each of the faces next to the edge, and finding those
	 * requires the edge resolved inside the shape below. The feature object is therefore made when
	 * the operation is offered - attached to that shape, not executed - and the handles are placed
	 * from what it resolves. Nothing is previewed and nothing is committed until Begin().
	 */
	class FilletInteraction : public ShapeHelper, public FeatureInteraction
	{
	public:
		FilletInteraction();
		explicit FilletInteraction(FilletFeature* p_feature);
		~FilletInteraction() override;

		FilletInteraction(const FilletInteraction&) = delete;
		FilletInteraction& operator=(const FilletInteraction&) = delete;

		void Attach(const Part::TopoShape& p_edge) override;
		void Begin() override;
		bool HasBegun() const override;
		void Commit() override;
		void Abort() override;
		Feature* GetFeature() override;
		void SetVisible(bool p_visible) override;
		void SetBeginCallback(std::function<void()> p_callback) override;

		FilletFeature* GetFillet() const { return m_feature; }

		/** Puts the radius on the feature and the handles, and redraws the preview: what the panel
		 * and the edges list go through when they change the fillet. */
		void SetRadius(double p_radius);
		double GetRadius() const;
		/** Redraws the preview from whatever the feature holds now. */
		void UpdatePreview();
		/** A handle was dragged and the interaction already took the value. */
		void SetParamChangedCallback(std::function<void()> p_callback);
		/** The set of edges changed - an edge was picked while the panel was open and the
		 * interaction added it - so the panel's list has to be read again. */
		void SetEdgesChangedCallback(std::function<void()> p_callback);

	private:
		/** An edge was picked while the task is open: it is added to the fillet, the same thing
		 * the panel's "Add from Selection" does with the selection. Nothing is added while the
		 * operation is only offered - there is no panel and no list yet. */
		void onSelectEdge(const std::vector<Part::TopoShape>& p_edge) override;

		/** A handle was dragged. Before the fillet exists this is what starts it.
		 *
		 * One entry per handle: the radius is the length of the arrow that was dragged, and the
		 * other one follows. (Reading the first handle in both is why only one arrow used to
		 * move: dragging the second set the radius back to what the first one held.)
		 */
		void onHandle1Changed();
		void onHandle2Changed();
		void setRadiusFrom(AxisTranslationWidget* p_handle);
		/** Rounds the shape by whatever radius the feature holds now.
		 *
		 * A radius of nothing does not build a fillet - OCCT refuses it - and that is the state a
		 * fillet is offered in: the shape stays exactly as it is, nothing is previewed, and the
		 * fillet appears once the drag has reached a radius the shape can actually carry. Below
		 * that the previous shape stays on screen, so what the user sees is the original edge
		 * turning into a rounding rather than an error.
		 * @return whether a shape was produced.
		 */
		bool roundByCurrentRadius();

		/** Makes the arrows on first use, and subscribes them to this object. */
		void ensureHandles();
		/** Places both arrows on the edge the fillet sits on. */
		bool placeHandles();
		/** Makes the feature for a fillet that was opened from the task bar (no pick to attach to)
		 * and gives it a radius that actually produces a fillet. */
		bool buildFeature();

		FilletFeature* m_feature = nullptr;
		/** Whether this object made the feature (and so has to delete it when the task is
		 * dropped). A fillet that already exists is only edited. */
		bool m_createdFeature = false;
		bool m_started = false;
		AxisTranslationWidget* m_handle1 = nullptr;
		AxisTranslationWidget* m_handle2 = nullptr;
		/** The smallest radius worth asking the kernel for, in model units (a share of the shape's
		 * size). Below it the fillet is the shape itself and nothing is previewed. */
		double m_minBuildRadius = 0.0;
		/** Radius per unit of arrow travel.
		 *
		 * The arrows move along the two faces next to the edge, and the radius is the circle
		 * tangent to both of them: the tangency point sits at r / tan(theta/2) from the edge, theta
		 * being the angle between the faces. For a right angle that is the radius itself, which is
		 * why the distance could be used as the radius for so long - on any other angle the arrow
		 * travels cot(theta/2) times too far (or too little) for the radius it asks for. */
		double m_radiusPerUnit = 1.0;
		ExecuteCommandPair m_lengthObserver1{ 0, nullptr };
		ExecuteCommandPair m_lengthObserver2{ 0, nullptr };
		std::function<void()> m_onBegin;
		std::function<void()> m_onParamChanged;
		std::function<void()> m_onEdgesChanged;
	};
}
