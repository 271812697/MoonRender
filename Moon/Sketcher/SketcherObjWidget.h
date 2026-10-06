#pragma once
#include <memory>
#include <set>
#include <unordered_map>
#include <chrono>
#include "Interactive/EventWidget.h"
#include "Sketcher/SketcherTypes.h"
#include "Sketcher/SketcherObj.h"

namespace MOON {
	class SketcherObjWidget;
	void defaultLabelOffsetPx(const Sketcher::Constraint* c, float& dx, float& dy);

	/** The editing face of a sketch: everything that only exists while the sketch is
	 * open in the viewport.
	 *
	 * It is an EventWidget - the renderer hands it the mouse and key events and calls
	 * its onUpdate() every frame - and it holds the *view* state of one sketch: the
	 * selection and preselection, what the cursor is dragging, the tools' in-progress
	 * state, the dimension annotations' layout and the drawing options. The sketch
	 * itself (curves, constraints, the solver, the sampling cache) stays in
	 * SketcherObj, which this widget neither owns nor replaces: every change it makes
	 * goes through the model's interface, and every curve it draws is read from it.
	 *
	 * The bodies live in two files, one per half of that job:
	 *   SketcherObjWidgetInteraction.cpp  - events, picking, snapping, dragging
	 *   SketcherObjWidgetDraw.cpp         - drawing, dimension annotations
	 */
	class SketcherObjWidget : public EventWidget
	{
	public:
		using PointPos = Sketcher::PointPos;
		using SelectGeoId = MOON::SelectGeoId;
		static constexpr int NoGeoId = MOON::NoGeoId;
		using CurveSegment = SketcherObj::CurveSegment;
		using SegPoint = SketcherObj::SegPoint;

		/** Viewport drawing options (colours, line widths): shared with the model so
		 * the panels can read them without knowing this class - see SketcherTypes.h. */
		using DrawOption = MOON::DrawOption;
		DrawOption& drawOption() { return m_drawOption; }
		const DrawOption& drawOption() const { return m_drawOption; }

		explicit SketcherObjWidget(SketcherObj* p_sketch);
		virtual ~SketcherObjWidget();

		/** The sketch this widget draws and edits. */
		SketcherObj* sketch() const { return m_sketch; }

		// ------------------------------------------------------------- edit session
		bool InEdit() const { return isInEdit; }
		void beginEdit();
		/** Leaves the edit session: the camera is handed back to the user and the
		* drawing tools are switched off. */
		void leaveEdit();
		/** The sketch is done: commits its shape (SketcherObj::makeDone) and then leaves
		* the edit session. This is what the task dialog calls when the user accepts the
		* sketch, so ending a session never travels from the data layer back up here. */
		void finishEdit();
		void fitCamera();
		virtual void onSetActive(bool flag) override;

		// ----------------------------------------------------------------- drawing
		virtual void onUpdate() override;
		void draw();
		/** Sketch backdrop: the adaptive background grid only. The axes and the origin
		 * they meet at are geometry (see SketcherObj::ensureAxisGeometry) and are drawn
		 * by the geometry pass, so that what is drawn and what can be picked are one
		 * thing. */
		void drawBackground();
		/** Draws one axis as the infinite line it stands for, clipped to the viewport.
		 * p_axisIndex 0 = horizontal axis, 1 = vertical axis. The caller sets colour
		 * and width: the axis is drawn like any other external curve. */
		void drawAxisSpanning(int p_axisIndex);
		/** Draws the two axes (and the origin they meet at) as the backdrop they are.
		 * Called before the sketch's own geometry, so a curve drawn on top of an axis
		 * stays visible instead of being covered by it. */
		void drawSketchAxes();
		bool snapToGridPoint(Base::Vector2d& pos) const;
		// Dimension label overlay: every dimensional constraint gets a draggable text
		// caption while the sketch is edited. Double-clicking a caption opens an editor
		// for the datum value.
		void drawConstraintLabels();
		bool computeConstraintLabel(
			int constrId,
			Base::Vector2d& anchorSketch,
			float& screenX,
			float& screenY
		) const;
		int pickConstraintLabelAt(float mouseX, float mouseY) const;
		/** How many screen pixels one sketch unit is worth right now.
		 *
		 * The dimension annotations are laid out in sketch units and only turned into
		 * pixels for drawing, so that they keep their place on the drawing when the
		 * view is zoomed. */
		float pixelsPerSketchUnit() const;
		void editConstraintValue(int constrId);
		/** Puts the annotation of a dimension where the smart dimension tool dropped
		 * it.
		 *
		 * p_screenX / p_screenY is that place in screen pixels. What is stored are the
		 * offsets a drag of the dimension to that place would have stored, so the
		 * annotation the sketch draws afterwards sits exactly where the preview of the
		 * tool was. */
		void placeDimensionAnnotation(int constrId, float p_screenX, float p_screenY);
		// Small geometric marker for tangent constraints: a tangent line
		// segment through the computed tangency point.
		void drawTangentIcons();
		// Small viewport markers for the remaining geometric constraints
		// (coincident/horizontal/vertical/parallel/...), placed near the
		// geometry they act on.
		void drawConstraintIcons();

		// --------------------------------------------------------------- selection
		int getPickGeoIndex(const Base::Vector2d& pos, const Base::Matrix4D& viewPortMat);
		SelectGeoId testSelect(const Base::Vector2d& pos);
		std::vector<int> getSelectIds() const;
		std::vector<SelectGeoId> getSelectGeoPosIds() const { return selectIds; }
		int getPreselectId() const { return preSelectGeoId.GeoId; }
		SelectGeoId getPreSelectGeoId() const { return preSelectGeoId; }
		void addSelect(int id);
		/** Adds one element picked in the viewport: a curve, or a marker on one. */
		void addSelect(SelectGeoId geoId);
		void removeSelect(const std::vector<int>& idList);
		void clearSelect();
		void selectGeo(int geoId);
		void setPreselect(int geoId);
		/** Snaps a sketch-plane position onto a curve's marker (or the grid), within the
		 * pixel tolerance the tool wants. */
		bool snapPoint(Base::Vector2d& pos, const std::set<int>& avoid = {});
		Base::Vector2d getMouseHitSketchPlanePoint();
		void moveGeo(SelectGeoId Id, float dx, float dy);
		bool findNextCoincidentPoint(
			const Base::Vector2d& pos,
			const SelectGeoId& current,
			SelectGeoId& next
		) const;

		// ------------------------------------------------------------------ events
		virtual void onMouseMove() override;
		virtual void onLeftMousePressed() override;
		virtual void onLeftMouseReleased() override;
		virtual void onKeyPress(const std::string& key) override;
		virtual void onKeyRelease(const std::string& key) override;

		private:
		/** The sketch data changed under this widget (it counts its changes, it does
		* not call): point the camera at a plane that moved, and drop the annotation
		* layout of constraints that are gone. */
		void syncWithSketch();
		/** Forgets the annotation layout of constraints that are gone: the maps are
		* keyed by the constraint's address, and a later one allocated at the same
		* address would inherit an entry that is not its. */
		void pruneConstraintLayout();
		void pickGeo();
		void updateConstraintLabelInteraction();
		bool getConstraintMeasureEndpoints(
			const Sketcher::Constraint* constraint,
			Base::Vector2d& a,
			Base::Vector2d& b
		) const;
		/** Which part of a dimension an interaction is on. The caption only slides
		 * along the dimension line; the line itself - arrows included - is one handle
		 * that moves the dimension as a whole along the direction its extension lines
		 * run in. */
		enum class LabelHandle
		{
			Caption,
			/** A length dimension: the line - arrows included - is one handle that
			 * moves the dimension as a whole along its extension direction. */
			DimensionLine,
			/** An angle dimension: the arc is the handle, and dragging it changes the
			 * radius it is drawn at while its centre stays on the vertex. */
			AngleArc
		};

		/** Everything a linear dimension is laid out from: the measured points (in
		 * sketch and screen space), where each end of the dimension line starts from
		 * and the direction the line may be dragged in, the offset it sits at until it
		 * is moved, and the pixel gap that keeps the caption clear of the line. */
		struct StraightDimFrame
		{
			Base::Vector2d measuredA;
			Base::Vector2d measuredB;
			Eigen::Vector2f screenA;
			Eigen::Vector2f screenB;
			/** The two ends of the line the dimension is drawn from (each one is tied to
			 * the point it measures by an extension line) and the unit direction the
			 * whole line is dragged in. Both ends share the offset, so the line always
			 * stays parallel to what it measures - axis aligned for DistanceX/Y. */
			Eigen::Vector2f baseA;
			Eigen::Vector2f baseB;
			Eigen::Vector2f direction;
			float defaultOffset = 0.0f;
			float gapX = 0.0f;
			float gapY = 0.0f;
		};
		bool straightDimFrame(
			const Sketcher::Constraint* constraint,
			StraightDimFrame& out
		) const;
		/** The offset of the dimension line along its direction: where the user dragged
		 * it to, or the automatic offset while it was never moved. Both are given in
		 * sketch units, so the annotation keeps its place on the drawing when the view
		 * is zoomed; the result is in pixels. */
		float straightDimOffset(
			const Sketcher::Constraint* constraint,
			float p_defaultOffsetSketch
		) const;
		/** The dimension line's two ends in screen space, with the dragged offsets
		 * applied. @return false when the dimension cannot be laid out. */
		bool straightDimShaft(
			const Sketcher::Constraint* constraint,
			Eigen::Vector2f& p_a,
			Eigen::Vector2f& p_b
		) const;
		/** Which dimension line the cursor is on, if any. The whole line is the handle,
		 * not only its arrow heads. */
		int pickConstraintDimLineAt(float p_mouseX, float p_mouseY) const;
		/** Which angle annotation arc the cursor is on, if any. */
		int pickConstraintAngleArcAt(float p_mouseX, float p_mouseY) const;
		/** What the cursor is on: the constraint and which of its handles, or -1.
		 * The arrows are tested before the caption - they sit at the ends of the
		 * dimension line, the caption in its middle. */
		void pickLabelTarget(
			float p_mouseX,
			float p_mouseY,
			int& p_constrId,
			LabelHandle& p_handle
		) const;
		
		// Computes the straight dimension shaft (trackA..trackB) in screen
		// space plus the fixed pixel gap that separates the caption from the
		// shaft. Used both for drawing and for constraining label dragging.
		bool computeStraightLabelTrack(
			const Sketcher::Constraint* constraint,
			float& trackAx,
			float& trackAy,
			float& trackBx,
			float& trackBy,
			float& gapX,
			float& gapY
		) const;
		// Computes the angle annotation arc (center, radius, start and sweep
		// in screen degrees). For a single line the center is the line start
		// and the radius is half the line length; the caption can then only
		// slide along this arc.
		bool computeAngleLabelTrack(
			const Sketcher::Constraint* constraint,
			float& centerX,
			float& centerY,
			float& radiusPx,
			float& startDeg,
			float& sweepDeg
		) const;
		bool computeTangentIconAnchor(
			const Sketcher::Constraint* constraint,
			Base::Vector2d& anchorSketch,
			Base::Vector2d& dirSketch,
			Base::Vector2d& normalSketch
		) const;
		Base::Vector2d constraintLabelAnchor(const Sketcher::Constraint* constraint) const;
		std::string constraintLabelText(const Sketcher::Constraint* constraint) const;
		/** The radial annotation of a radius/diameter constraint, in screen space: where
		 * its shaft starts (the centre of the circle or arc) and where it ends (a point
		 * of the rim). Both ends are the projection of two points of the sketch, which
		 * is what keeps them on the centre and on the rim whatever the camera does -
		 * only a direction that misses an arc is turned onto it.
		 * @return false when the constraint is not radial, or its geometry is gone. */
		bool radiusDimShaft(
			const Sketcher::Constraint* p_constraint,
			Eigen::Vector2f& p_centerScreen,
			Eigen::Vector2f& p_rimScreen
		) const;
		/** The sketch-space vector that a screen-space vector stands for. The plane is
		 * projected by an affine map, and only its inverse says what a vector on screen
		 * means in the sketch: the two are the same only face on.
		 * @return false when the plane is seen edge on, i.e. when it says nothing. */
		bool sketchVectorOfScreenVector(
			const Eigen::Vector2f& p_screenVector,
			Base::Vector2d& p_out
		) const;
		bool constraintInError(int constrId) const;

		/** How the sketch plane lies in the viewport right now: what the adaptive
		 * background grid is drawn from, and what the grid snapping has to agree with -
		 * they share this so the lines the user sees and the places the cursor snaps to
		 * are one lattice. An orthographic camera projects along its own view direction,
		 * so the plane is not necessarily face on, and every length here is measured
		 * through the camera rather than assumed. */
		struct GridView
		{
			/** View-space position of the plane origin. */
			float oX = 0.0f;
			float oY = 0.0f;
			/** View-space direction the plane axes run in, i.e. how far one sketch unit
			 * along u / v travels across the screen (1 = not foreshortened). */
			float uX = 0.0f;
			float uY = 0.0f;
			float vX = 0.0f;
			float vY = 0.0f;
			/** Viewport half extents, in view-space units. */
			float hx = 0.0f;
			float hy = 0.0f;
			/** The adaptive grid step, in sketch units. */
			float step = 0.0f;
		};
		/** Fills in p_out for the current camera.
		 * @return false when there is nothing to describe: no camera, a perspective one,
		 *         or an orthographic one seen along the plane (the plane is a line on
		 *         screen then, and its visible part runs off to infinity). */
		bool gridView(GridView& p_out) const;

		enum SelectState
		{
			Stop,
			Hot,
			OperationGeo,
			DragRect,
			End
		};
		enum SelectMode {
			OverrideSelect,
			AppendSelect
		};

		/** The sketch this widget works on. Not owned: the feature owns it and outlives
		 * this widget. */
		SketcherObj* m_sketch = nullptr;
		/** The sketch revisions this widget has already reacted to (see
		* syncWithSketch). */
		unsigned int m_seenPlaneRevision = 0;
		unsigned int m_seenConstraintRevision = 0;
		DrawOption m_drawOption;
		bool isInEdit = true;
		SelectGeoId preSelectGeoId = { NoGeoId, PointPos::none };
		std::vector<SelectGeoId> selectIds;
		bool hasClickSelected = false;
		bool m_dragSolverInit = false;
		bool sketchDrawRect = false;
		SelectState selectState = Stop;
		SelectMode selectMode = OverrideSelect;
		Base::Vector2d onSketchPosP1;
		Base::Vector2d onSketchPosClicked;
		Base::Vector2d onSketchPosMove;
		Base::Vector2d onSketchPosP2;
		bool isHaveActiveHandler = false;
		// Dimension-label overlay state. The offsets are kept in sketch units, so
		// that an annotation stays where it was put while the view is zoomed; drawing
		// converts them with pixelsPerSketchUnit().
		std::unordered_map<const Sketcher::Constraint*, Base::Vector2d> m_labelManualOffsetSketch;
		// 0..1 parameter of the caption along the straight dimension shaft
		std::unordered_map<const Sketcher::Constraint*, double> m_labelManualParam;
		int m_labelHover = -1;
		int m_labelDrag = -1;
		/** Which handle of the dimension m_labelHover / m_labelDrag is on. The arrows
		 * move the dimension line itself, the caption only slides along it. */
		LabelHandle m_labelHoverHandle = LabelHandle::Caption;
		LabelHandle m_labelDragHandle = LabelHandle::Caption;
		/** How far (sketch units along its direction) the user dragged a dimension
		 * line; missing means it still sits at its automatic offset. */
		std::unordered_map<const Sketcher::Constraint*, float> m_straightDimOffsetSketch;
		/** The radius (sketch units) the user dragged an angle annotation arc to. The
		 * centre stays where the geometry puts it, so this is all that moves - and with
		 * it the amount of arc that is drawn. */
		std::unordered_map<const Sketcher::Constraint*, float> m_angleLabelRadiusSketch;
		int m_lastLabelClick = -1;
		std::chrono::steady_clock::time_point m_lastLabelClickTime;
	};
}




