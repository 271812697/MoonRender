#pragma once

#include <functional>

namespace Part { class TopoShape; }

namespace MOON
{
	class Feature;

	/** One operation, as the user acts on it in the viewport.
	 *
	 * It is the thing between the two ends of a task: the handles the user drags and the panel
	 * that edits the numbers are both views of it. It owns what the operation is made of - the
	 * feature, the handles, the preview - so neither end has to.
	 *
	 * The life is in two steps, and the split is what picking a face relies on:
	 *
	 *   Attach()  puts the handles where the operation would stand. Nothing is built: no feature,
	 *             no preview. The user can see what can be done and point at it.
	 *   Begin()   builds the operation - the feature, its preview - at the parameter values the
	 *             handles already hold. A task bar calls it straight away; a pick calls it from the
	 *             first drag, so the result grows out of the shape that was picked instead of
	 *             appearing at some default size.
	 *
	 * Commit() and Abort() end it, from the panel or from whoever offered it.
	 */
	class FeatureInteraction
	{
	public:
		virtual ~FeatureInteraction() = default;

		/** Offers the operation on a picked sub-shape: handles only, nothing built. */
		virtual void Attach(const Part::TopoShape& p_face) = 0;
		/** Starts it for real at the values the handles hold now. */
		virtual void Begin() = 0;
		virtual bool HasBegun() const = 0;
		/** Keeps the result (the panel's OK). */
		virtual void Commit() = 0;
		/** Throws it away (the panel's Cancel, or a pick that was replaced). */
		virtual void Abort() = 0;
		virtual Feature* GetFeature() = 0;
		/** Shows or hides everything this interaction puts in the viewport. */
		virtual void SetVisible(bool p_visible) = 0;
		/** Called when Begin() runs: the task is real now and its panel is needed. Set by
		 * whoever offered the operation. */
		virtual void SetBeginCallback(std::function<void()> p_callback) = 0;
	};
}
