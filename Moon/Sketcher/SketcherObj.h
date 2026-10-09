#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <set>
#include "TopoShape.h"
#include "Sketcher/SketchePlane2D.h"
#include "Sketcher/Datatypes/Constraint.h"
#include "Sketcher/Datatypes/Sketch.h"
#include "Sketcher/SketcherTypes.h"

namespace Part {
	class  Geometry;
}
namespace MOON {
	/** The sketch as data: its curves, the references projected into it, the
	 * constraints, and the solver they are handed to.
	 *
	 * Nothing in here knows about the viewport. Every curve is sampled into a cache
	 * (CurveSegment) that drawing, picking and snapping read; the operations that
	 * change the geometry maintain that cache, so the data stays consistent
	 * whatever draws it. Editing - selection, dragging, picking, dimension
	 * annotations - lives in SketcherObjWidget, which holds one of these and goes
	 * through this interface to change it. */
	class SketcherObj
	{
	public:
		// Point positions are provided by the ported Sketcher::PointPos
		// (GeoEnum.h); keep a short alias for use inside this class and by
		// code that refers to SketcherObj::PointPos.
		using PointPos = Sketcher::PointPos;
		using SelectGeoId = MOON::SelectGeoId;
		/** "No geometry" for selection and lookups: the negative solver ids are taken
		 * by the external geometry, so an unused element is GeoUndef - the same value
		 * the solver uses for an element that is not set. */
		static constexpr int NoGeoId = MOON::NoGeoId;

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

		SketcherObj();
		~SketcherObj();

		/** --- what changed, for whoever edits this sketch -------------------------
		* The sketch is the layer *below* its editing widget: it does not know the
		* widget and never calls into it. What it does is count the changes a viewer
		* has to react to - the plane it lies on, and its list of constraints. The
		* widget keeps the numbers it has already seen and compares them while it
		* updates (see SketcherObjWidget::syncWithSketch), so learning about a change
		* never has to travel upwards as a call.
		*
		* Nothing here is persisted: the counters only order events within one run. */
		unsigned int planeRevision() const { return m_planeRevision; }
		unsigned int constraintRevision() const { return m_constraintRevision; }

		/** --- what the editing widget reads ---------------------------------------
		/** --- the editing session --------------------------------------------------
		 * Whether something is editing the sketch right now. The tools of a sketch
		 * ask this to know when to switch themselves off; the editing widget keeps
		 * the flag up to date, and nothing here knows what that widget is. */
		bool isBeingEdited() const { return m_beingEdited; }
		void setBeingEdited(bool p_edited) { m_beingEdited = p_edited; }

		/** --- where a dimension's annotation was dropped ---------------------------
		 * The dimension tool knows the sketch point under the cursor when it adds a
		 * constraint, and the widget that draws the annotation turns that point into
		 * the layout it keeps. The point is data of the sketch - the tool and the
		 * drawing widget are two different widgets - and goes away with the
		 * constraint it belongs to. */
		void setAnnotationDropPoint(const Sketcher::Constraint* p_constraint, const Base::Vector2d& p_sketchPos);
		const std::unordered_map<const Sketcher::Constraint*, Base::Vector2d>& annotationDropPoints() const { return m_annotationDrops; }
		void clearAnnotationDropPoint(const Sketcher::Constraint* p_constraint);

		/** --- the selection --------------------------------------------------------
		 * What is picked in the sketch: the element under the cursor (the
		 * preselection) and the elements that are selected. They are data of the
		 * sketch, not of the widget that drew them - the tool that adds a constraint
		 * reads what the drawing tools picked, and a panel lists it - so they live
		 * here and every widget goes through these calls. */
		int getPreselectId() const { return preSelectGeoId.GeoId; }
		const SelectGeoId& getPreSelectGeoId() const { return preSelectGeoId; }
		void setPreselect(int geoId) { preSelectGeoId = { geoId, PointPos::none }; }
		void setPreselect(const SelectGeoId& p_geoId) { preSelectGeoId = p_geoId; }
		void clearPreselect() { preSelectGeoId = { NoGeoId, PointPos::none }; }

		const std::vector<SelectGeoId>& getSelectGeoPosIds() const { return selectIds; }
		/** The ids of the selected elements, without the markers on them. */
		std::vector<int> getSelectIds() const;
		/** True when exactly this element - a curve, or a marker on one - is
		 * selected. */
		bool isSelected(const SelectGeoId& p_geoId) const;
		/** Adds one element; an element that is already selected stays as it is. */
		void addSelect(const SelectGeoId& p_geoId);
		void addSelect(int geoId) { addSelect({ geoId, PointPos::none }); }
		/** Drops every entry that names one of these curves, markers included. */
		void removeSelect(const std::vector<int>& idList);
		void clearSelect() { selectIds.clear(); }
		/** Selects one curve and nothing else. */
		void selectGeo(int geoId);

		/** --- what the editing widget reads ---------------------------------------
		 * The widget above draws, picks and snaps from the sketch's data. These are
		 * plain reads on purpose: the sketch hands its data out, it still does not
		 * know who asks for it. */
		const std::vector<std::unique_ptr<Part::Geometry>>& geometries() const { return mGeoList; }
		const std::vector<Sketcher::Constraint*>& constraints() const { return mConstraintList; }
		int geometryCount() const { return static_cast<int>(mGeoList.size()); }
		/** How the sketch plane lies, without a copy (see getPlane for one). */
		const SketcherPlane2D& plane() const { return mPlane; }
		/** True for a curve the sketch keeps as a construction aid (see
		 * setConstruction). */
		bool isConstructionGeometry(int geoId) const { return mConstructionGeoIds.count(geoId) != 0; }

		/** --- the solver, for the drag gestures of the widget ---------------------
		 * The widget grabs elements, moves them and hands the solved geometry back;
		 * the solver itself lives here, so these are the operations it needs. */
		void resetInitialMove() { solvedSketch.resetInitMove(); }
		/** Anchors a drag at the elements it grabbed. @return false when the solver
		 * cannot move them, i.e. when the gesture has to fall back to the plain
		 * parameter edits. */
		bool beginMove(const std::vector<Sketcher::GeoElementId>& p_ids)
		{
			solvedSketch.resetInitMove();
			return solvedSketch.initMove(p_ids) == 0;
		}
		int moveGeometries(
			const std::vector<Sketcher::GeoElementId>& p_ids,
			const Base::Vector3d& p_to,
			bool p_relative)
		{
			return solvedSketch.moveGeometries(p_ids, p_to, p_relative);
		}
		/** Takes the geometry the solver just produced as the sketch's own: the list
		 * keeps its length and order, so the geoIds - and with them every constraint -
		 * stay valid. */
		void takeSolvedGeometry();

		/** A name for the messages this sketch writes. The feature names it after
		 * itself; a sketch that has none is just "SketcherObj". */
		const std::string& getName() const { return m_name; }
		void setName(const std::string& p_name) { m_name = p_name; }

		/** --- geometry -------------------------------------------------------------
		 * The sketch's own curves, in the order they were added: a curve's geoId is
		 * its index here. */
		int addGeometry(std::unique_ptr<Part::Geometry>& ptr);
		int addGeometry(Part::Geometry* curve);
		void addGeometry(const std::vector<Part::Geometry*>& curveList);
		Part::Geometry* getGeometry(int GeoId);
		const Part::Geometry* getGeometry(int GeoId) const;
		int getHighestCurveIndex();
		void deleteGeometry(int GeoId);
		void deleteGeometries(const std::vector<int>& GeoIds);
		void replaceGeometry(int oldGeoId, std::unique_ptr<Part::Geometry>& newGeo);
		void replaceGeometries(const std::vector<int>& oldGeoIds, std::vector<std::unique_ptr<Part::Geometry>>& newGeos);
		bool isClosedCurve(const Part::Geometry* geo);

		int fillet(int geoId1,int geoId2,const Base::Vector3d& refPnt1,const Base::Vector3d& refPnt2,double radius,bool trim = true,bool createCorner = false,bool chamfer = false);
		bool seekTrimPoints(
			int GeoId,
			const Base::Vector3d& point,
			int& GeoId1,
			Base::Vector3d& intersect1,
			int& GeoId2,
			Base::Vector3d& intersect2,double& u1,double&u2
		);
		bool trim(int GeoId,double u1,double u2, const Base::Vector3d& point1, const Base::Vector3d& point2);
		int addSymmetric(const std::vector<int>& geoIdList,int refGeoId);
		std::vector<Part::Geometry*> getSymmetric(
			const std::vector<int>& geoIdList,
			std::map<int, int>& geoIdMap,
			std::map<int, bool>& isStartEndInverted,
			int refGeoId
		);
		Part::TopoShape toShape() const;

		/** Construction geometry is a property of the sketch's own curve: it stays in
		 * the solver, but it is never part of the shape the sketch produces (e.g. the
		 * corner points of a rounded rectangle). */
		void setConstruction(int geoId, bool construction);

		/** --- the internal geometry of a curve -------------------------------------
		 * An ellipse the user drew carries its parameters - centre, radii, axis
		 * direction - inside itself, and there is no element the rest of the sketch
		 * could be constrained against. FreeCAD exposes them: the major axis, the
		 * minor axis and the two focuses are added as construction curves of their
		 * own, each tied to the ellipse by an InternalAlignment constraint. They
		 * follow the ellipse, they are what a length on an axis or a point on one of
		 * them is constrained to, and being construction they are never part of the
		 * wire the features above build on. Drawing them is what shows the dashed
		 * axes (and the two focus dots) of an ellipse.
		 *
		 * This is FreeCAD's SketchObject::exposeInternalGeometry(): it adds only the
		 * elements that are missing, so calling it again changes nothing.
		 * @return how many internal elements were added, or -1 when p_geoId does not
		 * name a curve that carries internal geometry. */
		int exposeInternalGeometry(int p_geoId);
		/** True when p_geoId is one of those elements: it is the First element of an
		 * InternalAlignment constraint, which is what ties it to its curve. Such an
		 * element cannot be turned into normal geometry - it would then be part of the
		 * sketch's shape - so the construction toggle leaves it alone. */
		bool isInternalGeometry(int p_geoId) const;
		/** The curve an internal element belongs to, or NoGeoId. */
		int internalGeometryOwner(int p_geoId) const;

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

		/** --- constraints ---------------------------------------------------------- */
		/// add constraint
		int addConstraint(const Sketcher::Constraint* constraint);
		/// add constraint
		int addConstraint(std::unique_ptr<Sketcher::Constraint> constraint);
		int getConstraintCount() const { return static_cast<int>(mConstraintList.size()); }
		const Sketcher::Constraint* getConstraint(int index) const;
		/** Drops the constraint at p_index (the index the panels list it by) and solves
		 * the sketch again without it. The elements it named are left alone: what goes
		 * is the constraint, not the geometry it was about.
		 * @return true when p_index named a constraint. */
		bool removeConstraint(int p_index);
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
		void setConstraintVisible(int constrId, bool visible);

		/** --- solver -------------------------------------------------------------- */
		int solve(bool updateGeoAfterSolving = true);
		/** Marks the sketch as finished: the wire (and the face made from it) is what
		 * the features above build on. Called when the sketch is closed. */
		void makeDone();
		Part::TopoShape getDoneFaceShape() const { return doneFaceShape; }
		Part::TopoShape getDoneWireShape() const { return doneWireShape; }

		/** --- the plane the sketch lies on ---------------------------------------- */
		void setPlane(const SketcherPlane2D& plane);
		SketcherPlane2D getPlane();
		void getPlaneNormal(double*p);
		Base::Matrix4D getplaneTransform() const;
		Base::Vector3d getPlaneOrigin() const { return mPlane.origin; }
		Base::Vector3d getPlaneXAxis() const { return mPlane.xAxis; }
		Base::Vector3d getPlaneYAxis() const { return mPlane.yAxis; }
		Base::Matrix4D updateTransform() const;

		/** --- the angle two lines make -------------------------------------------- */
		/** The angle an Angle constraint between two lines would hold with the sketch
		 * as it stands, measured the way the sketcher measures it before the
		 * constraint is added (FreeCAD's SketcherGui::calculateAngle).
		 *
		 * The angle runs counter-clockwise from the end of the first line that is
		 * closest to the corner where the two lines meet - the direction pointing
		 * away from that corner - to the same end of the second line. Those two ends
		 * are what the constraint stores (FirstPos/SecondPos), so the value a panel
		 * offers is the one the solver keeps, instead of its supplement: an angle
		 * whose ends are the far ones would otherwise be solved by flipping one of
		 * the lines, which is exactly the value the user did not mean. */
		struct MeasuredAngle
		{
			int firstGeoId = Sketcher::GeoEnum::GeoUndef;
			PointPos firstPos = PointPos::none;
			int secondGeoId = Sketcher::GeoEnum::GeoUndef;
			PointPos secondPos = PointPos::none;
			/** Radians, in [0, pi]: the two ends carry the side, so the value is
			 * never negative. */
			double radians = 0.0;
			/** True when the angle is measured from the second line to the first;
			 * firstPos/secondPos always sit in the measured order, so a caller that
			 * names the lines swaps them when this is set. */
			bool swapped = false;
			/** False when there is no angle to measure: not two lines, or two lines
			 * that are parallel and apart. */
			bool usable = false;
		};
		/** p_firstPos / p_secondPos name the end of each line the angle is measured
		 * from when the user picked that end; `none` measures from the end closest
		 * to the corner, the way the sketcher does it on its own. */
		MeasuredAngle measureAngleBetweenLines(
			int p_firstGeoId,
			int p_secondGeoId,
			PointPos p_firstPos = PointPos::none,
			PointPos p_secondPos = PointPos::none
		) const;

		/** The angle two lines make at the corner they meet, from the four ends of
		 * the lines themselves: the one rule both the sketcher tools measure an
		 * angle by (FreeCAD's SketcherGui::calculateAngle). p_firstPos /
		 * p_secondPos name an end the user picked, when there is one. */
		static MeasuredAngle measureAngleBetweenLineEnds(
			const Base::Vector2d& p_firstStart,
			const Base::Vector2d& p_firstEnd,
			const Base::Vector2d& p_secondStart,
			const Base::Vector2d& p_secondEnd,
			PointPos p_firstPos = PointPos::none,
			PointPos p_secondPos = PointPos::none
		);

		/** --- what the sketch shows -----------------------------------------------
		 * The flags the document keeps with the geometry: the drawing grid, the grid
		 * snapping, and which curves and constraints the user hid. They are data of
		 * the sketch (they are written to the .moon file with it), so they live
		 * here - the widget reads them to draw, the panels read and set them. */
		void setDrawGrid(bool v) { m_drawGrid = v; }
		bool isDrawGrid() const { return m_drawGrid; }
		void setSnapToGrid(bool v) { m_snapToGrid = v; }
		bool isSnapToGrid() const { return m_snapToGrid; }
		void setGeometryVisible(int geoId, bool visible);
		bool isGeometryVisible(int geoId) const
		{
			return mHiddenGeoIds.count(geoId) == 0;
		}

		/** --- the sampled curves ---------------------------------------------------
		 * What a curve is drawn, picked and snapped from: a polyline plus the
		 * markers that stand on it (start, end, centre). The sketch's own curves are
		 * sampled when they are added, the external ones when their projection is
		 * computed, so an entry is normally already there. */
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
		/** The discretization a geometry is drawn from. Having an entry is what marks
		 * a geometry as sampled (a point samples to no polyline at all, so the
		 * content cannot tell). */
		CurveSegment getCurveSegment(Part::Geometry* geo);
		CurveSegment& segmentOf(Part::Geometry* geo);
		/** The same without creating an entry: the hit test and the snapping must not
		 * turn a missing cache into an empty one. */
		const CurveSegment* findSegment(const Part::Geometry* geo) const;
		void updateGeoSegment(int id);
		bool getGeometryPoint(int GeoId, PointPos pos, Base::Vector2d& out) const
		{
			return getGeometryPointSketch(GeoId, pos, out);
		}
		bool getGeometryPointSketch(int geoId, PointPos pos, Base::Vector2d& out) const;
		bool getGeometryCenterSketch(int geoId, Base::Vector2d& out) const;

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
		/** What the sketch calls itself in its messages (see getName). */
		std::string m_name = "SketcherObj";
		SketcherPlane2D mPlane;
		Base::Matrix4D planeTransform;
		bool m_drawGrid = true;
		bool m_snapToGrid = false;
		std::set<int> mConstructionGeoIds;
		std::set<int> mHiddenGeoIds;
		Sketcher::Sketch solvedSketch;
		std::vector<Sketcher::Constraint*> mConstraintList;
		std::vector<std::unique_ptr<Part::Geometry>>mGeoList;
		std::vector<std::unique_ptr<Part::Geometry>>mExternalGeoList;
		/** The sketch axes, [0] vertical and [1] horizontal, i.e. the order the tail
		 * of the external block is built in (the horizontal axis is the very last, so
		 * that it is the -1 the root point lives on). */
		std::unique_ptr<Part::Geometry> mExternalAxes[2];
		std::unordered_map<Part::Geometry*, CurveSegment>mGeoSegment;
		/** Change counters (see planeRevision / constraintRevision). */
		unsigned int m_planeRevision = 0;
		unsigned int m_constraintRevision = 0;
		/** The selection (see getPreselectId / getSelectGeoPosIds). */
		SelectGeoId preSelectGeoId = { NoGeoId, PointPos::none };
		std::vector<SelectGeoId> selectIds;
		/** True while a widget is editing the sketch (see isBeingEdited). */
		bool m_beingEdited = false;
		/** Where the annotation of a dimension was dropped (see
		 * annotationDropPoints). */
		std::unordered_map<const Sketcher::Constraint*, Base::Vector2d> m_annotationDrops;
		/** Drops the drop points of constraints that are gone. */
		void pruneAnnotationDrops();
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
	};
}


