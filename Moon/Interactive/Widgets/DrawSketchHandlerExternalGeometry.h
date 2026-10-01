#pragma once
#include "Interactive/Widgets/DrawSketchHandler.h"

namespace MOON
{
	/** Tool widget: brings geometry of another feature into the active sketch.
	 *
	 * The two flavours share the one widget, because they are one tool with one
	 * mode - the toolbar has a button for each:
	 *  - Projection: the picked sub-shape is projected onto the sketch plane along
	 *    its normal - the shadow the shape casts onto that plane;
	 *  - Section: the picked shape is cut with the sketch plane and the cut curves
	 *    are taken - what a sketch drawn on a section wants to constrain to.
	 *
	 * The tool owns the whole reference step (this is what used to live in
	 * SketcherObj): it picks the sub-shape under the cursor, resolves the feature
	 * and the name it stands for, computes the curves in sketch (u, v)
	 * coordinates and hands them to the sketch with
	 * SketcherObj::addExternalGeometry(). The sketch only stores them, next to its
	 * own geometry, and treats them as fixed (blocked) for the solver.
	 */
	class DrawSketchHandlerExternalGeometry : public DrawSketchHandler
	{
	public:
		enum class EMode
		{
			/** Project the picked sub-shape onto the sketch plane. */
			Projection,
			/** Take the section of the picked shape with the sketch plane. */
			Section
		};

		DrawSketchHandlerExternalGeometry(const std::string& name);
		virtual ~DrawSketchHandlerExternalGeometry();

		/** Name the widget is registered under in the gizmo pass. Both toolbar
		 * buttons switch on this one widget (see SketchToolbar). */
		static constexpr const char* WidgetName = "DrawSketchHandlerExternalGeometry";

		void setMode(EMode p_mode) { m_mode = p_mode; }
		EMode getMode() const { return m_mode; }

		virtual void onUpdate() override;
		virtual void onMouseMove() override;
		virtual void onLeftMousePressed() override;
		virtual void onKeyPress(const std::string& key) override;
		virtual void quit() override;

	private:
		/** Picks the sub-shape under the cursor and adds its projection (or its
		 * section) to the active sketch.
		 * @return true when at least one curve was added. */
		bool pickAndAddReference();

		EMode m_mode = EMode::Projection;
	};
}
