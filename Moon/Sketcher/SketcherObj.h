#pragma once
#include<memory>
#include <unordered_map>
#include <chrono>
#include <set>
#include "Interactive/EventWidget.h"
#include "TopoShape.h"
#include "Sketcher/SketchePlane2D.h"
#include "Sketcher/Datatypes/Constraint.h"
#include "Sketcher/Datatypes/Sketch.h"

namespace Part {
	class  Geometry;
}
namespace MOON {
	class Feature;
	void defaultLabelOffsetPx(const Sketcher::Constraint* c, float& dx, float& dy);
	class SketcherObj :public EventWidget
	{
	public:
		// Point positions are provided by the ported Sketcher::PointPos
		// (GeoEnum.h); keep a short alias for use inside this class and by
		// code that refers to SketcherObj::PointPos.
		using PointPos = Sketcher::PointPos;
		// Viewport drawing options for sketch geometry and constraint
		// annotations. Colours are stored in ABGR byte order, matching the
		// renderer's Eigen::Vector4<uint8_t> convention.
		struct DrawOption
		{
			Eigen::Vector4<uint8_t> pointColor { 255, 0, 0, 255 };
			Eigen::Vector4<uint8_t> preselectColor { 255, 0, 255, 255 };
			Eigen::Vector4<uint8_t> selectColor { 255, 255, 255, 0 };
			Eigen::Vector4<uint8_t> constraintColor { 255, 255, 47, 186 };
			Eigen::Vector4<uint8_t> curveColor { 255, 255, 134, 120 };
			Eigen::Vector4<uint8_t> constructionColor { 255, 255, 107, 142 };
			/** Geometry projected in from another feature: same idea as construction
			 * geometry (reference only, never part of the shape the sketch produces),
			 * with its own colour so it cannot be mistaken for something drawn here. */
			Eigen::Vector4<uint8_t> externalColor { 255, 100, 0, 255 };
			/** The axes of the sketch plane, each in the colour CAD packages draw it
			 * in: the horizontal one (the x axis) red, the vertical one (the y axis)
			 * green. The origin is the start of the horizontal axis and keeps the
			 * external colour, so the two lines stay apart from the point they meet
			 * at. */
			Eigen::Vector4<uint8_t> xAxisColor { 255, 0, 0, 255 };
			Eigen::Vector4<uint8_t> yAxisColor { 255, 0, 255, 0 };
			float curveLineWidth = 3.0f;
			float pointSize = 10.0f;
		};
		DrawOption& drawOption() { return m_drawOption; }
		const DrawOption& drawOption() const { return m_drawOption; }
		/** How the solver sees a constraint of this sketch. The diagnosis is the one of
		 * the last solve (see retrieveSolverDiagnostics). */
		enum class ConstraintStatus
		{
			Ok,
			/** Contradicts the other constraints: the sketch cannot be solved. */
			Conflicting,
			/** Adds nothing: something else already fixes what it asks for. */
			Redundant,
			/** Part of it is already enforced by the rest. */
			PartiallyRedundant,
			/** The solver cannot make sense of it at all (bad element, bad value). */
			Malformed
		};
		/** The solver's diagnosis of one constraint, so the panels can say what is wrong
		 * with a sketch instead of only drawing it red in the viewport. */
		ConstraintStatus getConstraintStatus(int p_constrId) const;
		/** Degrees of freedom of the last solve: 0 fully constrained, >0 under-,
		 * <0 over-constrained. */
		int getDegreesOfFreedom() const { return lastDoF; }
		bool hasConflictingConstraints() const { return lastHasConflict; }
		bool hasRedundantConstraints() const { return lastHasRedundancies; }
		bool hasPartiallyRedundantConstraints() const { return lastHasPartialRedundancies; }
		bool hasMalformedConstraints() const { return lastHasMalformedConstraints; }
		struct SelectGeoId
		{
			int GeoId;
			PointPos pointPos = PointPos::none;
		};
		SketcherObj();
		~SketcherObj();

		virtual void onUpdate()override;
		virtual void onMouseMove()override;
		virtual void onLeftMousePressed()override;
		virtual void onLeftMouseReleased()override;
		virtual void onKeyPress(const std::string& key)override;
		virtual void onKeyRelease(const std::string& key)override;
		virtual void onSetActive(bool flag)override;
		void setPlane(const SketcherPlane2D&plane);
		void fitCamera();
		void beginEdit();
		void setDrawGrid(bool v) { m_drawGrid = v; }
		bool isDrawGrid() const { return m_drawGrid; }
		void setSnapToGrid(bool v) { m_snapToGrid = v; }
		bool isSnapToGrid() const { return m_snapToGrid; }
		// Marks geometry that is only used internally to build a shape (e.g.
		// rounded-rectangle corner points). Such geometry stays in the solver
		// but its point markers are hidden in the viewport.
		void setConstruction(int geoId, bool construction);
		void setConstraintVisible(int constrId, bool visible);
		void setGeometryVisible(int geoId, bool visible);
		void selectGeo(int geoId);
		void setPreselect(int geoId);
		bool isGeometryVisible(int geoId) const
		{
			return mHiddenGeoIds.count(geoId) == 0;
		}
		SketcherPlane2D getPlane();
		void getPlaneNormal(double*p);
		bool InEdit()const;
		void draw();
		bool snapToGridPoint(Base::Vector2d& pos) const;
		// Sketch backdrop: the adaptive background grid only. The axes and the origin
		// they meet at are geometry (see ensureAxisGeometry) and are drawn by the
		// geometry pass, so that what is drawn and what can be picked are one thing.
		void drawBackground();
		/** Draws one axis as the infinite line it stands for, clipped to the viewport.
		 * p_axisIndex 0 = horizontal axis, 1 = vertical axis. The caller sets colour
		 * and width: the axis is drawn like any other external curve. */
		void drawAxisSpanning(int p_axisIndex);
		void makeDone();
		int solve(bool updateGeoAfterSolving = true);
		int addGeometry(std::unique_ptr<Part::Geometry>&ptr);
		int addGeometry(Part::Geometry* curve);
		void addGeometry(const std::vector<Part::Geometry*>& curveList);
		Part::Geometry* getGeometry(int GeoId);
		const Part::Geometry* getGeometry(int GeoId) const;
		bool getGeometryPoint(int GeoId, PointPos pos, Base::Vector2d& out) const
		{
			return getGeometryPointSketch(GeoId, pos, out);
		}
		int getHighestCurveIndex();
		int getPickGeoIndex(const Base::Vector2d& pos, const Base::Matrix4D& viewPortMat);
		SelectGeoId testSelect(const Base::Vector2d& pos);
		std::vector<int> getSelectIds() const;
		void addSelect(int id);
		std::vector<SelectGeoId> getSelectGeoPosIds() const {
			return selectIds;
		}
		
		void removeSelect(const std::vector<int>& idList);
		int getPreselectId()const {return preSelectGeoId.GeoId;}
		SelectGeoId getPreSelectGeoId()const { return preSelectGeoId; }
		bool snapPoint(Base::Vector2d& pos,const std::set<int>&avoid={});
		int fillet(int geoId1,int geoId2,const Base::Vector3d& refPnt1,const Base::Vector3d& refPnt2,double radius,bool trim = true,bool createCorner = false,bool chamfer = false);
		bool seekTrimPoints(
			int GeoId,
			const Base::Vector3d& point,
			int& GeoId1,
			Base::Vector3d& intersect1,
			int& GeoId2,
			Base::Vector3d& intersect2,double& u1,double&u2
		);
		void deleteGeometry(int GeoId);
		void deleteGeometries(const std::vector<int>& GeoIds);
		void replaceGeometry(int oldGeoId, std::unique_ptr<Part::Geometry>& newGeo);
		void replaceGeometries(const std::vector<int>& oldGeoIds, std::vector<std::unique_ptr<Part::Geometry>>& newGeos);
		bool isClosedCurve(const Part::Geometry* geo);
		bool trim(int GeoId,double u1,double u2, const Base::Vector3d& point1, const Base::Vector3d& point2);
		// clang-format on
		int addSymmetric(const std::vector<int>& geoIdList,int refGeoId);
		std::vector<Part::Geometry*> getSymmetric(
			const std::vector<int>& geoIdList,
			std::map<int, int>& geoIdMap,
			std::map<int, bool>& isStartEndInverted,
			int refGeoId
		);
		Part::TopoShape toShape() const;

		/** --- external geometry: geometry of another feature, into this sketch ----
		 *
		 * The curves are computed outside the sketch (the tool widget
		 * DrawSketchHandlerExternalGeometry picks a sub-shape of another feature and
		 * projects it onto the sketch plane, or cuts it with that plane) and stored
		 * here next to the sketch's own geometry, so both go to the solver as one
		 * list. The storage is therefore the same kind of plain curve list as
		 * mGeoList, and adding one is the same kind of operation as addGeometry().
		 *
		 * The curves are *fixed* as far as the solver is concerned: the sketch can
		 * constrain to them but never change them, which is how the solver is told -
		 * the external block is the tail of the geometry list it is handed, see
		 * Sketch::setUpSketch().
		 */
		/** Adds one already projected curve (in sketch coordinates).
		 * @return the geoId constraints name it by, or NoGeoId when it was not added. */
		int addExternalGeometry(std::unique_ptr<Part::Geometry> p_geo);
		/** Adds a whole projection at once - a face projects to several curves, and
		 * they are one reference as far as the user is concerned.
		 * @return the geoId of the first curve added, or NoGeoId. */
		int addExternalGeometry(std::vector<std::unique_ptr<Part::Geometry>>&& p_geos);
		/** Drops the curve at p_index of the external list. The constraints that
		 * named it go with it; the ones on the other curves move to the ids they
		 * have now.
		 * @return true when p_index existed. */
		bool removeExternalGeometry(int p_index);
		/** Drops the references the user added, and the constraints that named one of
		 * them. The coordinate axes are not part of this: they are always there. */
		void clearExternalGeometry();
		/** How many references the user added - the two axes are not counted, and not
		 * listed by index either: they cannot be removed. */
		int getExternalGeometryCount() const {
			return static_cast<int>(mExternalGeoList.size());
		}
		Part::Geometry* getExternalGeometry(int p_index);
		const Part::Geometry* getExternalGeometry(int p_index) const;

		/** "No geometry" for the selection state. The negative solver ids are taken
		 * by the external geometry (see below), so an unused element is GeoUndef -
		 * the same value the solver uses for an element that is not set. */
		static constexpr int NoGeoId = Sketcher::GeoEnum::GeoUndef;

		/** --- the projected curves as selection targets -------------------------
		 * Constraints name an external curve by its solver geoId, and those ids
		 * count from the end of the solver list (the external block is its tail):
		 * the last curve is -1, the first is -count.
		 *
		 * The two coordinate axes are the last entries of that block, so they keep
		 * the fixed ids GeoEnum documents: the horizontal axis is -1 - its start
		 * point is the root point, i.e. the origin - and the vertical axis is -2.
		 * Every reference the user adds sits below them, from -3 down (RefExt). The
		 * selection therefore speaks the same numbering, and these helpers are the
		 * only place that knows how a negative id maps onto the curves. */
		int getExternalCurveCount() const;
		/** Solver geoId of the curve at p_index - the user's references first, then
		 * the two axes. */
		int getExternalGeoId(int p_index) const;
		/** Index a negative geoId refers to, or -1 when it is not one of the
		 * external curves. */
		int getExternalCurveIndex(int p_geoId) const;
		const Part::Geometry* getExternalCurve(int p_geoId) const;
		/** True for the ids that name one of the projected curves (GeoUndef and the
		 * other sentinels are not among them). */
		bool isExternalGeoId(int p_geoId) const;
		/** True for the two axis ids: the horizontal axis -1 (whose start point is the
		 * root point, i.e. the sketch origin) and the vertical axis -2. They are part
		 * of the external block but are neither added nor removed by the user. */
		bool isAxisCurve(int p_geoId) const
		{
			return p_geoId == Sketcher::GeoEnum::HAxis
				|| p_geoId == Sketcher::GeoEnum::VAxis;
		}
		/** Read-only lookup over both halves of the solver list: internal geometry by
		 * index, external curves by their negative id. Null when p_geoId names
		 * nothing. */
		const Part::Geometry* resolveGeometry(int p_geoId) const;
		Part::TopoShape getDoneFaceShape() {
			return doneFaceShape;
		}
		Part::TopoShape getDoneWireShape() {
			return doneWireShape;
		}
		Base::Matrix4D getplaneTransform() const;
		Base::Vector3d getPlaneOrigin() {
			return mPlane.origin;
		}
		Base::Vector3d getPlaneXAxis() {
			return mPlane.xAxis;
		}
		Base::Vector3d getPlaneYAxis() {
			return mPlane.yAxis;
		}	
		/// add constraint
		int addConstraint(const Sketcher::Constraint* constraint);
		/// add constraint
		int addConstraint(std::unique_ptr<Sketcher::Constraint> constraint);
		int getConstraintCount() const { return static_cast<int>(mConstraintList.size()); }
		const Sketcher::Constraint* getConstraint(int index) const;
		// Find an existing constraint with the same type and elements (datum
		// value ignored), so dimensional values can be edited via setDatum().
		int findConstraint(const Sketcher::Constraint* pattern) const;
		// Change the datum of an existing dimensional/tangent/perpendicular
		// constraint and re-solve; on failure the value is rolled back.
		int setDatum(int constrId, double datum);
		// helper function to create a new constraint and move it to the Constraint Property
		void addConstraint(
			Sketcher::ConstraintType constrType,
			int firstGeoId,
			Sketcher::PointPos firstPos,
			int secondGeoId = Sketcher::GeoEnum::GeoUndef,
			Sketcher::PointPos secondPos = Sketcher::PointPos::none,
			int thirdGeoId = Sketcher::GeoEnum::GeoUndef,
			Sketcher::PointPos thirdPos = Sketcher::PointPos::none
		);
		// creates a new constraint
		std::unique_ptr<Sketcher::Constraint> createConstraint(
			Sketcher::ConstraintType constrType,
			int firstGeoId,
			Sketcher::PointPos firstPos,
			int secondGeoId = Sketcher::GeoEnum::GeoUndef,
			Sketcher::PointPos secondPos = Sketcher::PointPos::none,
			int thirdGeoId = Sketcher::GeoEnum::GeoUndef,
			Sketcher::PointPos thirdPos = Sketcher::PointPos::none
		);
		// Dimension label overlay (P0): every dimensional constraint gets a
		// draggable text caption while the sketch is edited. Double-clicking a
		// caption opens an editor for the datum value.
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
	private:
		void retrieveSolverDiagnostics();
		int lastDoF;
		bool lastHasConflict;
		bool lastHasRedundancies;
		bool lastHasPartialRedundancies;
		bool lastHasMalformedConstraints;
		int lastSolverStatus;
		std::vector<int> lastConflicting;
		std::vector<int> lastRedundant;
		std::vector<int> lastPartiallyRedundant;
		std::vector<int> lastMalformedConstraints;
	private:
		Part::TopoShape doneWireShape;
		Part::TopoShape doneFaceShape;
		struct CurveSegment;
		void updateGeoSegment(int id);
		void pickGeo();
		void updateConstraintLabelInteraction();
		bool findNextCoincidentPoint(
			const Base::Vector2d& pos,
			const SelectGeoId& current,
			SelectGeoId& next
		) const;
		bool getGeometryPointSketch(int geoId, PointPos pos, Base::Vector2d& out) const;
		bool getGeometryCenterSketch(int geoId, Base::Vector2d& out) const;
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
		/** Drops the placement the user gave a dimension (caption offset, caption
		 * parameter, arrow offsets). The maps are keyed by the constraint's address,
		 * so a constraint that goes away has to take its entries with it - otherwise a
		 * later constraint allocated at the same address would inherit them. */
		void forgetConstraintLayout(const Sketcher::Constraint* p_constraint);
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
		bool constraintInError(int constrId) const;
		void addSelect(SelectGeoId geoId);
		void clearSelect();
		void moveGeo(SelectGeoId geoId,float dx,float dy);
		Base::Matrix4D updateTransform()const;
		Base::Vector2d getMouseHitSketchPlanePoint();
		CurveSegment getCurveSegment( Part::Geometry* geo) ;
		/** The discretization a geometry is drawn from. The sketch's own curves are
		 * sampled when they are added, the external ones when their projection is
		 * computed, so an entry is normally already there; this only samples when one
		 * is missing, because drawing nothing would be worse than the extra work.
		 * Having an entry is what marks a geometry as sampled (a point samples to no
		 * polyline at all, so the content cannot tell). */
		CurveSegment& segmentOf(Part::Geometry* geo);
		/** The same without creating an entry: the hit test and the snapping must not
		 * turn a missing cache into an empty one. */
		const CurveSegment* findSegment(const Part::Geometry* geo) const;
		SketcherPlane2D mPlane ;
		Base::Matrix4D planeTransform;
		bool isInEdit = true;
		bool m_drawGrid = true;
		bool m_snapToGrid = false;
		std::set<int> mConstructionGeoIds;
		std::set<int> mHiddenGeoIds;
		DrawOption m_drawOption;
		Sketcher::Sketch solvedSketch;
		std::vector<Sketcher::Constraint*> mConstraintList;
		std::vector<std::unique_ptr<Part::Geometry>>mGeoList;
		std::vector<std::unique_ptr<Part::Geometry>>mExternalGeoList;
		/** The sketch axes, [0] vertical and [1] horizontal, i.e. the order the tail
		 * of the external block is built in (the horizontal axis is the very last, so
		 * that it is the -1 the root point lives on). */
		std::unique_ptr<Part::Geometry> mExternalAxes[2];
		SelectGeoId preSelectGeoId = { NoGeoId, PointPos::none };
		std::vector<SelectGeoId> selectIds;
		bool hasClickSelected = false;
		bool m_dragSolverInit = false;
		bool sketchDrawRect = false;
		// P0 dimension-label overlay state. The offsets are kept in sketch units, so
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
		/** The caption offset (sketch units) the caption drag started from. */
		Base::Vector2d m_labelDragOffsetSketch;
		/** Where the caption drag was pressed, in pixels. */
		Base::Vector2d m_labelDragPressPx;
		int m_lastLabelClick = -1;
		std::chrono::steady_clock::time_point m_lastLabelClickTime;
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
		/** Rebuilds the external geoIds held by the constraints from the curves the
		 * block held before it changed, and drops the constraints whose curve is
		 * gone.
		 *
		 * The ids count from the end of the solver list, so adding a curve moves
		 * every one of them - without this, a constraint would silently end up on
		 * another curve after the next reference is added. The curves themselves are
		 * the identity: p_before are the pointers the block held, so a curve that
		 * moved to another index is still found. */
		void remapExternalReferences(const std::vector<Part::Geometry*>& p_before);
		/** The curves of the external block, in order of their indices. Taken before
		 * the block is changed, and handed to remapExternalReferences() after. */
		std::vector<Part::Geometry*> externalGeometryPointers() const;
		/** Creates the two coordinate axes. They are the fixed tail of the external
		 * block (see getExternalCurveCount), they exist from the moment the sketch
		 * does, and nothing ever removes them - so an empty list of references still
		 * leaves a sketch that can be constrained to the origin and the axes. */
		void ensureAxisGeometry();
		bool isHaveActiveHandler = false;
		SelectState selectState = Stop;
		SelectMode selectMode = OverrideSelect;
		Base::Vector2d onSketchPosP1;
		Base::Vector2d onSketchPosClicked;//used for click when select geometry curve
		Base::Vector2d onSketchPosMove;//used for mouse move
		Base::Vector2d onSketchPosP2;

		struct SegPoint
		{
			PointPos pointPos;
			Base::Vector3d coord;
			SegPoint(const Base::Vector3d& c,const PointPos& p):coord(c),pointPos(p) {}
		};
		struct CurveSegment
		{
			//point discret of curver
			std::vector<Base::Vector3d> point;
			//the param value of point 
			std::vector<double> params;
			//the start、center、end position of the curve
			std::vector<SegPoint>sepoints;
			CurveSegment() {}
		};
		std::unordered_map<Part::Geometry*, CurveSegment>mGeoSegment;
	};
}
