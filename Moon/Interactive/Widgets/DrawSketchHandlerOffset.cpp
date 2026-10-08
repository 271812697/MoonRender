#include "Interactive/Widgets/DrawSketchHandlerOffset.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>
#include <gp_Dir.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>

namespace MOON
{
	namespace
	{
		constexpr double kOffsetTolerance = 1e-6;

		struct EdgeItem
		{
			TopoDS_Edge edge;
			Base::Vector3d start;
			Base::Vector3d end;
			bool used = false;
		};

		bool GetEdgeEndpoints(const TopoDS_Edge& edge, Base::Vector3d& start, Base::Vector3d& end)
		{
			BRepAdaptor_Curve curve(edge);
			double first = curve.FirstParameter();
			double last = curve.LastParameter();
			if (std::fabs(first) > 1e99) first = -10000.0;
			if (std::fabs(last) > 1e99) last = +10000.0;
			gp_Pnt a = curve.Value(first);
			gp_Pnt b = curve.Value(last);
			start = Base::Vector3d(a.X(), a.Y(), a.Z());
			end = Base::Vector3d(b.X(), b.Y(), b.Z());
			return true;
		}

		bool Near(const Base::Vector3d& a, const Base::Vector3d& b)
		{
			return (a - b).Length() < kOffsetTolerance;
		}

		// Convert an offset edge (already in the sketch-local XY plane) back to a
		// Part::Geometry (line / circle / arc of circle).
		Part::Geometry* CurveToPartGeometry(const BRepAdaptor_Curve& curve)
		{
			if (curve.GetType() == GeomAbs_Line) {
				double first = curve.FirstParameter();
				double last = curve.LastParameter();
				if (std::fabs(first) > 1e99) first = -10000.0;
				if (std::fabs(last) > 1e99) last = +10000.0;
				gp_Pnt a = curve.Value(first);
				gp_Pnt b = curve.Value(last);
				auto* line = new Part::GeomLineSegment();
				line->setPoints(Base::Vector3d(a.X(), a.Y(), a.Z()), Base::Vector3d(b.X(), b.Y(), b.Z()));
				return line;
			}
			if (curve.GetType() == GeomAbs_Circle) {
				gp_Circ circle = curve.Circle();
				gp_Pnt center = circle.Location();
				gp_Pnt beg = curve.Value(curve.FirstParameter());
				gp_Pnt end = curve.Value(curve.LastParameter());
				if (beg.SquareDistance(end) < Precision::Confusion()) {
					auto* gCircle = new Part::GeomCircle();
					gCircle->setCenter(Base::Vector3d(center.X(), center.Y(), center.Z()));
					gCircle->setRadius(circle.Radius());
					return gCircle;
				}
				Handle(Geom_Circle) hCircle = new Geom_Circle(circle);
				double u1 = curve.FirstParameter();
				double u2 = curve.LastParameter();
				Handle(Geom_TrimmedCurve) tCurve = new Geom_TrimmedCurve(hCircle, u1, u2);
				auto* gArc = new Part::GeomArcOfCircle();
				gArc->setHandle(tCurve);
				gArc->reverseIfReversed();
				return gArc;
			}
			// TODO: ellipse / bspline offset support
			return nullptr;
		}
	}

	DrawSketchHandlerOffset::DrawSketchHandlerOffset(const std::string& name)
		: SupperClass(name)
	{
		// Offset is a one-shot tool: it commits on the first click and exits.
		continuousMode = false;
	}

	DrawSketchHandlerOffset::~DrawSketchHandlerOffset()
	{
	}

	void DrawSketchHandlerOffset::onSetActive(bool flag)
	{
		if (flag) {
			// Widgets are long-lived singletons reused by the toolbar, so clear
			// every piece of per-invocation state when the tool is (re)enabled.
			// Otherwise a second activation would reuse the wires and the offset
			// distance from the previous run.
			reset();
			sourceWires.clear();
			listOfGeoIds.clear();
			endpoint = Base::Vector2d(0.0, 0.0);
			onlySingleLines = true;
			deleteOriginal = false;
			offsetLengthSet = false;
			offsetLength = 0.0;
		}
		SupperClass::onSetActive(flag);
	}

	void DrawSketchHandlerOffset::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj) {
			listOfGeoIds = Obj->getSelectIds();
		}
	}

	void DrawSketchHandlerOffset::generateSourceWires()
	{
		sourceWires.clear();
		onlySingleLines = true;

		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) return;

		std::vector<EdgeItem> items;
		for (int geoId : listOfGeoIds) {
			const Part::Geometry* pGeo = Obj->getGeometry(geoId);
			if (!pGeo) continue;
			auto geo = std::unique_ptr<Part::Geometry>(pGeo->copy());
			Part::Geometry* raw = geo.get();
			raw->reverseIfReversed();
			TopoDS_Shape shape = raw->toShape();
			if (shape.ShapeType() != TopAbs_EDGE) continue;
			EdgeItem item;
			item.edge = TopoDS::Edge(shape);
			GetEdgeEndpoints(item.edge, item.start, item.end);
			items.push_back(item);
		}
		if (items.empty()) return;

		// Greedily group connected edges into chains by shared endpoints.
		for (size_t seed = 0; seed < items.size(); ++seed) {
			if (items[seed].used) continue;
			items[seed].used = true;
			std::vector<int> chain = { static_cast<int>(seed) };

			bool extended = true;
			while (extended) {
				extended = false;
				const Base::Vector3d chainStart = items[chain.front()].start;
				const Base::Vector3d chainEnd = items[chain.back()].end;
				for (size_t j = 0; j < items.size(); ++j) {
					if (items[j].used) continue;
					if (Near(items[j].end, chainStart)) {
						chain.insert(chain.begin(), static_cast<int>(j));
						items[j].used = true;
						extended = true;
						break;
					}
					if (Near(items[j].start, chainEnd)) {
						chain.push_back(static_cast<int>(j));
						items[j].used = true;
						extended = true;
						break;
					}
					if (Near(items[j].start, chainStart)) {
						TopoDS_Edge reversedEdge = TopoDS::Edge(items[j].edge.Reversed());
						items[j].edge = reversedEdge;
						std::swap(items[j].start, items[j].end);
						chain.insert(chain.begin(), static_cast<int>(j));
						items[j].used = true;
						extended = true;
						break;
					}
					if (Near(items[j].end, chainEnd)) {
						TopoDS_Edge reversedEdge = TopoDS::Edge(items[j].edge.Reversed());
						items[j].edge = reversedEdge;
						std::swap(items[j].start, items[j].end);
						chain.push_back(static_cast<int>(j));
						items[j].used = true;
						extended = true;
						break;
					}
				}
			}

			// Try to make a wire of the ordered chain. If the edges are not
			// topologically connected, fall back to offsetting each edge alone.
			BRepBuilderAPI_MakeWire mkWire;
			bool wireOk = true;
			for (int idx : chain) {
				mkWire.Add(items[idx].edge);
				if (mkWire.Error() != BRepBuilderAPI_WireDone) {
					wireOk = false;
					break;
				}
			}
			if (wireOk && chain.size() > 0) {
				TopoDS_Wire wire = mkWire.Wire();
				// Make sure closed wires are CCW relative to the sketch plane
				// (+Z), so a positive offset always expands outwards and a
				// negative one inwards (same rule as FreeCAD).
				if (wire.Closed()) {
					BRepBuilderAPI_MakeFace mkFace(wire);
					if (mkFace.IsDone()) {
						TopoDS_Face face = mkFace.Face();
						BRepAdaptor_Surface surface(face);
						if (surface.GetType() == GeomAbs_Plane) {
							gp_Dir normal = surface.Plane().Axis().Direction();
							if (normal.Z() < 0.0) {
								wire.Reverse();
							}
						}
					}
				}
				sourceWires.push_back(wire);
				if (chain.size() != 1) {
					onlySingleLines = false;
				}
			}
			else {
				for (int idx : chain) {
					BRepBuilderAPI_MakeWire singleWire;
					singleWire.Add(items[idx].edge);
					sourceWires.push_back(singleWire.Wire());
				}
			}
		}
	}

	bool DrawSketchHandlerOffset::findOffsetLength()
	{
		if (sourceWires.empty()) {
			generateSourceWires();
		}
		if (sourceWires.empty()) {
			return false;
		}

		double newOffsetLength = std::numeric_limits<double>::max();
		BRepBuilderAPI_MakeVertex mkVertex({ endpoint.x, endpoint.y, 0.0 });
		TopoDS_Vertex vertex = mkVertex.Vertex();

		for (auto& wire : sourceWires) {
			BRepExtrema_DistShapeShape distTool(wire, vertex);
			if (!distTool.IsDone()) continue;
			double distance = distTool.Value();
			if (distance >= newOffsetLength) continue;
			newOffsetLength = distance;

			// Negative distance offsets towards the inside of closed wires.
			if (BRep_Tool::IsClosed(wire)) {
				BRepBuilderAPI_MakeFace mkFace(wire);
				if (mkFace.IsDone()) {
					TopoDS_Face face = mkFace.Face();
					BRepClass_FaceClassifier classifier(face, { endpoint.x, endpoint.y, 0.0 }, Precision::Confusion());
					if (classifier.State() == TopAbs_IN) {
						newOffsetLength = -newOffsetLength;
					}
				}
			}
		}

		if (newOffsetLength == std::numeric_limits<double>::max()) {
			return false;
		}
		offsetLength = newOffsetLength;
		offsetLengthSet = true;
		return true;
	}

	void DrawSketchHandlerOffset::buildOffsetGeometry()
	{
		ShapeGeometry.clear();
		if (sourceWires.empty()) return;
		if (std::fabs(offsetLength) < Precision::Confusion()) return;

		// OCC join type: Arc = 0, Intersection = 2.
		const short joinType = constructionMethod() == ConstructionMethods::OffsetConstructionMethod::Intersection ? 2 : 0;

		BRepOffsetAPI_MakeOffset mkOffset;
		if (onlySingleLines) {
			// A plane is required for offsetting single lines; it also forces the
			// offset direction for that degenerate case.
			TopoDS_Face workingPlane = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)));
			mkOffset = BRepOffsetAPI_MakeOffset(workingPlane);
		}
		mkOffset.Init(GeomAbs_JoinType(joinType), false);
		for (auto& wire : sourceWires) {
			mkOffset.AddWire(wire);
		}

		try {
			mkOffset.Perform(offsetLength);
		}
		catch (const Standard_Failure&) {
			CORE_ERROR("Offset failed (OCC).");
			return;
		}

		TopoDS_Shape offsetShape = mkOffset.Shape();
		if (offsetShape.IsNull()) {
			return;
		}
		offsetShape = BRepBuilderAPI_Copy(offsetShape).Shape();

		TopExp_Explorer explorer(offsetShape, TopAbs_EDGE);
		for (; explorer.More(); explorer.Next()) {
			const TopoDS_Edge& edge = TopoDS::Edge(explorer.Current());
			BRepAdaptor_Curve curve(edge);
			Part::Geometry* geo = CurveToPartGeometry(curve);
			if (geo) {
				ShapeGeometry.emplace_back(std::unique_ptr<Part::Geometry>(geo));
			}
		}
	}

	void DrawSketchHandlerOffset::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		if (state() == SelectMode::SeekFirst) {
			endpoint = onSketchPos;
			if (sourceWires.empty()) {
				generateSourceWires();
			}
			if (findOffsetLength() && std::fabs(offsetLength) > Precision::Confusion()) {
				buildOffsetGeometry();
				CreateAndDrawShapeGeometry();
			}
		}
	}

	void DrawSketchHandlerOffset::deleteOriginalGeos()
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj && !listOfGeoIds.empty()) {
			Obj->deleteGeometries(listOfGeoIds);
		}
	}

	void DrawSketchHandlerOffset::executeCommands()
	{
		if (sourceWires.empty()) {
			generateSourceWires();
		}
		if (!offsetLengthSet) {
			findOffsetLength();
		}
		if (std::fabs(offsetLength) > Precision::Confusion()) {
			buildOffsetGeometry();
			SupperClass::executeCommands();
			if (deleteOriginal) {
				deleteOriginalGeos();
			}
		}
		quit();
	}

	void DrawSketchHandlerOffset::onKeyPress(const std::string& key)
	{
		if (key == "D") {
			deleteOriginal = !deleteOriginal;
			CORE_INFO("Offset delete original: {}", deleteOriginal);
		}
		else {
			SupperClass::onKeyPress(key);
		}
	}
}

