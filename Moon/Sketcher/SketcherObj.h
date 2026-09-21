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
			float curveLineWidth = 3.0f;
			float pointSize = 10.0f;
		};
		DrawOption& drawOption() { return m_drawOption; }
		const DrawOption& drawOption() const { return m_drawOption; }
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
		// Sketch backdrop (adaptive background grid, infinite X/Y axes, origin
		// marker). Kept separate from the geometry pass so background visuals
		// can be tuned/disabled without touching curve rendering.
		void drawBackground();
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

		/** One piece of geometry from outside the sketch, projected into it.
		 *
		 * The reference is "<Type>_<index>" on p_source - the same form the task
		 * panels store when the user picks an edge or a face - and it is resolved by
		 * mapped name first (see ResolveSubShapeRef), so it survives a recompute of
		 * the feature it points at.
		 *
		 * External geometry is *fixed* as far as the solver is concerned: the sketch
		 * can constrain to it but never change it, which is exactly how FreeCAD feeds
		 * it to the solver (the trailing entries of the geometry list, see
		 * Sketch::setUpSketch()).
		 */
		struct ExternalGeometry
		{
			Feature* source = nullptr;
			std::string reference;
			/** Reserved for the section mode (FreeCAD's "intersection" option: take the
			 * section of the source with the sketch plane instead of projecting it).
			 * Only the projection is implemented so far. */
			bool intersection = false;
			/** Names of the source sub-shape, see ResolveSubShapeRef(). */
			std::vector<std::string> names;
			/** The projected curves, in sketch (u, v) coordinates. */
			std::vector<std::unique_ptr<Part::Geometry>> geos;
			/** The source shape the projection was made from. A refresh resolves and
			 * projects again only when this is not the shape the source has now, so a
			 * drag (which solves on every mouse move) does not re-project each step. */
			TopoDS_Shape sourceShape;
			/** Force the next refresh to redo the work even when sourceShape matches. */
			bool dirty = true;
			/** The source sub-shape cannot be resolved any more. */
			bool missing = false;
		};

		/** Adds geometry of another feature to this sketch, projected into its plane.
		 * @return the index of the new entry, or -1 when it could not be added. */
		int addExternalGeometry(
			Feature* p_source,
			const std::string& p_reference,
			bool p_intersection = false);
		void clearExternalGeometry();
		int getExternalGeometryCount() const {
			return static_cast<int>(mExternalGeometry.size());
		}
		const ExternalGeometry* getExternalGeometry(int p_index) const;
		/** Recomputes every projection from its source. Done before solving, so the
		 * solver always sees the sources as they are now. */
		void updateExternalGeometry();
		/** Drops one entry and re-solves.
		 * @return true when p_index existed. */
		bool removeExternalGeometry(int p_index);
		/** Add-external-geometry mode. While it is on, a click in the viewport picks a
		 * sub-shape of another feature and projects it into this sketch instead of
		 * selecting sketch geometry - the tool FreeCAD calls "external geometry". The
		 * mode stays on until it is switched off, so several references can be picked
		 * one after the other. */
		void setExternalGeometryMode(bool p_on);
		/** Whether the picks of the mode are taken as a section of the source with the
		 * sketch plane instead of an orthographic projection of it (FreeCAD's
		 * "intersection" flavour of the same tool). */
		void setExternalGeometryIntersection(bool p_intersection) {
			m_externalGeometryIntersection = p_intersection;
		}
		bool isExternalGeometryIntersection() const {
			return m_externalGeometryIntersection;
		}
		bool isExternalGeometryMode() const { return m_externalGeometryMode; }
		/** The feature this sketch belongs to. Its own geometry can never be an
		 * external reference. */
		void setOwnerFeature(Feature* p_owner) { m_ownerFeature = p_owner; }

		/** "No geometry" for the selection state. The negative solver ids are taken
		 * by the external geometry (see below), so an unused element is GeoUndef -
		 * the same value the solver uses for an element that is not set. */
		static constexpr int NoGeoId = Sketcher::GeoEnum::GeoUndef;

		/** --- the projected curves as selection targets -------------------------
		 * Constraints name an external curve by its solver geoId, and those ids
		 * count from the end of the solver list (the external block is its tail):
		 * the last curve is -1, the first is -count. The selection therefore speaks
		 * the same numbering, and these helpers are the only place that knows how a
		 * negative id maps onto the projection lists. */
		int getExternalCurveCount() const;
		/** Solver geoId of the curve at p_index of the flattened external list. */
		int getExternalGeoId(int p_index) const;
		/** Flattened index a negative geoId refers to, or -1 when it is not one of
		 * the external curves. */
		int getExternalCurveIndex(int p_geoId) const;
		const Part::Geometry* getExternalCurve(int p_geoId) const;
		/** True for the ids that name one of the projected curves (GeoUndef and the
		 * other sentinels are not among them). */
		bool isExternalGeoId(int p_geoId) const;
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
		void editConstraintValue(int constrId);
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
		 * it to, or the automatic offset while it was never moved. */
		float straightDimOffset(
			const Sketcher::Constraint* constraint,
			float p_defaultOffset
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
		std::vector<ExternalGeometry> mExternalGeometry;
		Feature* m_ownerFeature = nullptr;
		bool m_externalGeometryMode = false;
		bool m_externalGeometryIntersection = false;
		SelectGeoId preSelectGeoId = { NoGeoId, PointPos::none };
		std::vector<SelectGeoId> selectIds;
		bool hasClickSelected = false;
		bool m_dragSolverInit = false;
		bool sketchDrawRect = false;
		// P0 dimension-label overlay state
		std::unordered_map<const Sketcher::Constraint*, Base::Vector2d> m_labelManualOffsetPx;
		// 0..1 parameter of the caption along the straight dimension shaft
		std::unordered_map<const Sketcher::Constraint*, double> m_labelManualParam;
		int m_labelHover = -1;
		int m_labelDrag = -1;
		/** Which handle of the dimension m_labelHover / m_labelDrag is on. The arrows
		 * move the dimension line itself, the caption only slides along it. */
		LabelHandle m_labelHoverHandle = LabelHandle::Caption;
		LabelHandle m_labelDragHandle = LabelHandle::Caption;
		/** How far (pixels along its direction) the user dragged a dimension line;
		 * missing means it still sits at its automatic offset. */
		std::unordered_map<const Sketcher::Constraint*, float> m_straightDimOffsetPx;
		/** The radius (pixels) the user dragged an angle annotation arc to. The centre
		 * stays where the geometry puts it, so this is all that moves - and with it the
		 * amount of arc that is drawn. */
		std::unordered_map<const Sketcher::Constraint*, float> m_angleLabelRadiusPx;
		Base::Vector2d m_labelDragOffsetPx;
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
		/** Picks the actor under the cursor and turns it into an external reference.
		 * @return true when something was added. */
		bool pickExternalGeometry();
		/** Identifies every projected curve by (entry, curve in entry). Those keys
		 * survive the block being renumbered, which the geoIds do not. */
		std::vector<std::pair<int, int>> externalCurveKeys() const;
		/** Rebuilds the external geoIds held by the constraints from the keys taken
		 * before the block changed, and drops the constraints whose curve is gone.
		 *
		 * The ids count from the end of the solver list, so adding a curve moves
		 * every one of them - without this, a constraint would silently end up on
		 * another curve after the next reference is added. */
		void remapExternalReferences(const std::vector<std::pair<int, int>>& p_oldKeys);
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
