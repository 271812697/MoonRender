#include "feature/DatumPlaneFeature.h"
#include "core/component/CTopoShape.h"
#include "core/log.h"
#include "TopoShape.h"
#include "base/BoundBox.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MOON
{
	namespace
	{
		Maths::FVector3 fromGp(const gp_Pnt& p_point)
		{
			return Maths::FVector3(
				static_cast<float>(p_point.X()),
				static_cast<float>(p_point.Y()),
				static_cast<float>(p_point.Z()));
		}

		Maths::FVector3 fromGp(const gp_Dir& p_direction)
		{
			return Maths::FVector3(
				static_cast<float>(p_direction.X()),
				static_cast<float>(p_direction.Y()),
				static_cast<float>(p_direction.Z()));
		}

		/** An X axis for a plane whose normal is given: the world Z projected onto
		 * the plane, or the world X when the normal is (nearly) vertical.
		 *
		 * A plane that three picked points or an edge define has no natural X
		 * direction, and letting one come out of the numbers would make the plane -
		 * and any sketch later put on it - spin with every recompute. FreeCAD makes
		 * the same choice for its "make Y vertical" attachment. */
		Maths::FVector3 stableXAxis(const Maths::FVector3& p_normal)
		{
			const Maths::FVector3 worldZ(0.0f, 0.0f, 1.0f);
			Maths::FVector3 axis = worldZ - p_normal * Maths::FVector3::Dot(worldZ, p_normal);
			if (Maths::FVector3::Length(axis) < 1.0e-4f) {
				const Maths::FVector3 worldX(1.0f, 0.0f, 0.0f);
				axis = worldX - p_normal * Maths::FVector3::Dot(worldX, p_normal);
			}
			return Maths::FVector3::Normalize(axis);
		}

		/** The points a reference stands for: a vertex is its own point, an edge
		 * gives its two ends - the same reading FreeCAD's three-point attachment
		 * does, so an edge counts as two of the three points. */
		void appendReferencePoints(
			const Part::TopoShape& p_reference,
			std::vector<gp_Pnt>& p_points)
		{
			if (p_reference.isNull()) {
				return;
			}
			switch (p_reference.getShape().ShapeType()) {
			case TopAbs_VERTEX:
				p_points.push_back(
					BRep_Tool::Pnt(TopoDS::Vertex(p_reference.getShape())));
				break;
			case TopAbs_EDGE: {
				BRepAdaptor_Curve curve(TopoDS::Edge(p_reference.getShape()));
				double first = curve.FirstParameter();
				double last = curve.LastParameter();
				if (Precision::IsInfinite(first) || Precision::IsInfinite(last)) {
					first = 0.0;
					last = 1.0;
				}
				p_points.push_back(curve.Value(first));
				p_points.push_back(curve.Value(last));
				break;
			}
			default:
				break;
			}
		}
	}

	DatumPlaneFeature::DatumPlaneFeature(const std::string& p_name)
		: DatumFeature(p_name, "DatumPlane")
	{
	}

	DatumPlaneFeature::~DatumPlaneFeature()
	{
	}

	const std::vector<DatumPlaneFeature::MapMode>& DatumPlaneFeature::allMapModes()
	{
		static const std::vector<MapMode> modes = {
			MapMode::Deactivated,
			MapMode::ObjectXY,
			MapMode::ObjectXZ,
			MapMode::ObjectYZ,
			MapMode::FlatFace,
			MapMode::ThreePoints,
			MapMode::NormalToEdge
		};
		return modes;
	}

	const char* DatumPlaneFeature::mapModeName(MapMode p_mode)
	{
		switch (p_mode) {
		case MapMode::Deactivated: return "Deactivated";
		case MapMode::ObjectXY: return "ObjectXY";
		case MapMode::ObjectXZ: return "ObjectXZ";
		case MapMode::ObjectYZ: return "ObjectYZ";
		case MapMode::FlatFace: return "FlatFace";
		case MapMode::ThreePoints: return "ThreePointsPlane";
		case MapMode::NormalToEdge: return "NormalToEdge";
		}
		return "Deactivated";
	}

	const char* DatumPlaneFeature::mapModeLabel(MapMode p_mode)
	{
		switch (p_mode) {
		case MapMode::Deactivated: return "Deactivated";
		case MapMode::ObjectXY: return "Object XY";
		case MapMode::ObjectXZ: return "Object XZ";
		case MapMode::ObjectYZ: return "Object YZ";
		case MapMode::FlatFace: return "Flat face";
		case MapMode::ThreePoints: return "Three points";
		case MapMode::NormalToEdge: return "Normal to edge";
		}
		return "Deactivated";
	}

	DatumPlaneFeature::MapMode DatumPlaneFeature::mapModeFromName(
		const std::string& p_name,
		MapMode p_fallback)
	{
		for (MapMode mode : allMapModes()) {
			if (p_name == mapModeName(mode)) {
				return mode;
			}
		}
		return p_fallback;
	}

	int DatumPlaneFeature::requiredReferenceCount() const
	{
		switch (mapMode) {
		case MapMode::Deactivated:
		case MapMode::ObjectXY:
		case MapMode::ObjectXZ:
		case MapMode::ObjectYZ:
			return 0;
		case MapMode::FlatFace:
			return 1;
		case MapMode::NormalToEdge:
			// An edge for the direction, and a vertex saying where along it the
			// plane stands. The vertex is optional: without one the plane stands at
			// the start of the edge.
			return 2;
		case MapMode::ThreePoints:
			return 3;
		}
		return 0;
	}

	Maths::FVector3 DatumPlaneFeature::baseOrigin() const
	{
		if (m_baseFeature != nullptr) {
			// The pose of the shape below: the global-parallel modes go through the
			// origin of the body they are attached to, the way FreeCAD's ObjectXY/
			// XZ/YZ do, rather than through the world origin. It is read inside the
			// body - the placement of the body node is not part of the chain the
			// plane is attached to (see Feature::applyWorldTransform).
			return m_baseFeature->transform.GetLocalPosition();
		}
		return Maths::FVector3(0.0f, 0.0f, 0.0f);
	}

	bool DatumPlaneFeature::resolveAttachment(
		Maths::FVector3& p_origin,
		Maths::FVector3& p_normal,
		Maths::FVector3& p_xAxis)
	{
		switch (mapMode) {
		case MapMode::Deactivated:
			// Nothing to attach to: the plane is the global XY plane, moved by the
			// offset (which is what the panel's placement fields are for).
			p_origin = Maths::FVector3(0.0f, 0.0f, 0.0f);
			p_normal = Maths::FVector3(0.0f, 0.0f, 1.0f);
			p_xAxis = Maths::FVector3(1.0f, 0.0f, 0.0f);
			return true;
		case MapMode::ObjectXY:
		case MapMode::ObjectXZ:
		case MapMode::ObjectYZ: {
			p_origin = baseOrigin();
			if (mapMode == MapMode::ObjectXY) {
				p_normal = Maths::FVector3(0.0f, 0.0f, 1.0f);
				p_xAxis = Maths::FVector3(1.0f, 0.0f, 0.0f);
			}
			else if (mapMode == MapMode::ObjectXZ) {
				p_normal = Maths::FVector3(0.0f, 1.0f, 0.0f);
				p_xAxis = Maths::FVector3(1.0f, 0.0f, 0.0f);
			}
			else {
				p_normal = Maths::FVector3(1.0f, 0.0f, 0.0f);
				p_xAxis = Maths::FVector3(0.0f, 1.0f, 0.0f);
			}
			return true;
		}
		case MapMode::FlatFace: {
			Part::TopoShape faceShape = getBaseTopoFaceShape();
			if (faceShape.isNull()
				|| faceShape.getShape().ShapeType() != TopAbs_FACE) {
				CORE_ERROR(
					"{0}: 'Flat face' needs one face as its support", GetName());
				return false;
			}
			gp_Pln plane;
			if (!faceShape.findPlane(plane)) {
				CORE_ERROR(
					"{0}: the face it is attached to is not flat", GetName());
				return false;
			}
			const gp_Ax3 position = plane.Position();
			p_origin = fromGp(position.Location());
			p_normal = fromGp(position.Direction());
			p_xAxis = fromGp(position.XDirection());
			if (faceShape.getShape().Orientation() == TopAbs_REVERSED) {
				// A face can be oriented against its own surface (the inside of a
				// pocket is); the plane then faces the other way, like FreeCAD.
				p_normal = -p_normal;
			}
			return true;
		}
		case MapMode::ThreePoints: {
			std::vector<gp_Pnt> points;
			for (const Part::TopoShape& reference : getBaseTopoFaceShapes()) {
				appendReferencePoints(reference, points);
				if (points.size() >= 3) {
					break;
				}
			}
			if (points.size() < 3) {
				CORE_ERROR(
					"{0}: 'Three points' needs three points as its support (a vertex "
					"is one, an edge counts as two)",
					GetName());
				return false;
			}
			const gp_Vec first(points[0], points[1]);
			const gp_Vec second(points[0], points[2]);
			if (first.Magnitude() < Precision::Confusion()
				|| second.Magnitude() < Precision::Confusion()) {
				CORE_ERROR(
					"{0}: two of the three points are the same", GetName());
				return false;
			}
			gp_Vec planeNormal = first.Crossed(second);
			if (planeNormal.Magnitude() < Precision::Confusion()) {
				CORE_ERROR(
					"{0}: the three points lie on one line, they do not make a plane",
					GetName());
				return false;
			}
			planeNormal.Normalize();
			p_origin = fromGp(points[0]);
			p_normal = Maths::FVector3(
				static_cast<float>(planeNormal.X()),
				static_cast<float>(planeNormal.Y()),
				static_cast<float>(planeNormal.Z()));
			p_xAxis = Maths::FVector3::Normalize(fromGp(first.Normalized()));
			return true;
		}
		case MapMode::NormalToEdge: {
			Part::TopoShape edgeShape = getBaseTopoEdgeShape();
			if (edgeShape.isNull()
				|| edgeShape.getShape().ShapeType() != TopAbs_EDGE) {
				CORE_ERROR(
					"{0}: 'Normal to edge' needs one edge as its support", GetName());
				return false;
			}
			BRepAdaptor_Curve curve(TopoDS::Edge(edgeShape.getShape()));
			double parameter = curve.FirstParameter();
			gp_Pnt point = curve.Value(parameter);

			// A second reference - a vertex - says *where* along the edge the plane
			// stands; without one it stands at the start of the edge.
			if (subValues.size() > 1) {
				const Part::TopoShape vertexShape = resolveBaseSubShape(1);
				if (!vertexShape.isNull()
					&& vertexShape.getShape().ShapeType() == TopAbs_VERTEX) {
					const gp_Pnt vertexPoint
						= BRep_Tool::Pnt(TopoDS::Vertex(vertexShape.getShape()));
					// The projection gives a parameter on the *curve*, which for a
					// trimmed edge can sit outside the part the edge covers, so it is
					// clamped back into the edge before it is used.
					double first = 0.0;
					double last = 0.0;
					const Handle(Geom_Curve) curveHandle
						= BRep_Tool::Curve(TopoDS::Edge(edgeShape.getShape()), first, last);
					if (!curveHandle.IsNull()) {
						GeomAPI_ProjectPointOnCurve projector(vertexPoint, curveHandle);
						if (projector.NbPoints() > 0) {
							parameter = std::clamp(
								projector.LowerDistanceParameter(),
								curve.FirstParameter(),
								curve.LastParameter());
							point = curve.Value(parameter);
						}
					}
				}
			}

			gp_Pnt onCurve;
			gp_Vec tangent;
			curve.D1(parameter, onCurve, tangent);
			if (tangent.Magnitude() < Precision::Confusion()) {
				CORE_ERROR(
					"{0}: the edge it is attached to has no direction there", GetName());
				return false;
			}
			tangent.Normalize();
			p_origin = fromGp(point);
			p_normal = Maths::FVector3(
				static_cast<float>(tangent.X()),
				static_cast<float>(tangent.Y()),
				static_cast<float>(tangent.Z()));
			p_xAxis = stableXAxis(p_normal);
			return true;
		}
		}
		return false;
	}

	bool DatumPlaneFeature::execute()
	{
		try {
			// The attachment first: it is what decides where the plane lies. A
			// reference that cannot be resolved any more (the face was deleted, a
			// shape changed under it) leaves the plane where it was, which is better
			// than snapping it back to the world origin.
			Maths::FVector3 planeOrigin = origin;
			Maths::FVector3 planeNormal = normal;
			Maths::FVector3 planeXAxis = xAxis;
			Maths::FVector3 attachedOrigin;
			Maths::FVector3 attachedNormal;
			Maths::FVector3 attachedXAxis;
			if (resolveAttachment(attachedOrigin, attachedNormal, attachedXAxis)) {
				planeOrigin = attachedOrigin;
				planeNormal = attachedNormal;
				planeXAxis = attachedXAxis;
			}
			else if (requiredReferenceCount() > 0) {
				CORE_WARN(
					"{0}: the attachment '{1}' cannot be resolved; the plane keeps the "
					"placement it had",
					GetName(),
					mapModeName(mapMode));
			}

			// The frame the offset and the rotation are applied in: the normal is
			// what the mode fixed, so the axes are rebuilt from it to keep them
			// perpendicular even when the attachment was only roughly orthogonal.
			Maths::FVector3 normalAxis = Maths::FVector3::Normalize(planeNormal);
			Maths::FVector3 xAxisInPlane = planeXAxis
				- normalAxis * Maths::FVector3::Dot(planeXAxis, normalAxis);
			if (Maths::FVector3::Length(xAxisInPlane) < 1.0e-4f) {
				xAxisInPlane = stableXAxis(normalAxis);
			}
			xAxisInPlane = Maths::FVector3::Normalize(xAxisInPlane);
			Maths::FVector3 yAxisInPlane
				= Maths::FVector3::Normalize(Maths::FVector3::Cross(normalAxis, xAxisInPlane));

			const double turn = static_cast<double>(rotation) * M_PI / 180.0;
			const float cosTurn = static_cast<float>(std::cos(turn));
			const float sinTurn = static_cast<float>(std::sin(turn));
			const Maths::FVector3 turnedX = xAxisInPlane * cosTurn + yAxisInPlane * sinTurn;
			const Maths::FVector3 turnedY = yAxisInPlane * cosTurn - xAxisInPlane * sinTurn;

			const Maths::FVector3 centre = planeOrigin
				+ turnedX * offset.x + turnedY * offset.y + normalAxis * offset.z;

			// The size: automatic follows the shape below, so the plane is visible
			// without the user typing a size, and manual is what the panel offers
			// when they want a particular one.
			if (automaticSize) {
				double extent = 0.0;
				if (m_baseFeature != nullptr) {
					const Part::TopoShape baseShape = getBaseTopoShape();
					if (!baseShape.isNull()) {
						extent = baseShape.getBoundBoxOptimal().CalcDiagonalLength() * 0.6;
					}
				}
				if (extent < 1.0) {
					extent = 40.0;
				}
				length = static_cast<float>(extent);
				width = static_cast<float>(extent);
			}
			const double halfWidth = std::max(static_cast<double>(width) * 0.5, 0.01);
			const double halfLength = std::max(static_cast<double>(length) * 0.5, 0.01);

			// The frame is built first and the plane taken from it: gp_Pln has no
			// "point, normal, x axis" constructor, gp_Ax3 has.
			const gp_Ax3 frame(
				gp_Pnt(centre.x, centre.y, centre.z),
				gp_Dir(normalAxis.x, normalAxis.y, normalAxis.z),
				gp_Dir(turnedX.x, turnedX.y, turnedX.z));
			const gp_Pln plane(frame);
			BRepBuilderAPI_MakeFace faceMaker(
				plane, -halfWidth, halfWidth, -halfLength, halfLength);
			if (!faceMaker.IsDone()) {
				CORE_ERROR("{0}: the plane could not be built", GetName());
				return false;
			}

			const TopoDS_Shape face = faceMaker.Face();
			// A datum has nothing below it to be named after, so - like the datum
			// line - it hands on a bare shape and skips setResultShape(), which would
			// warn about the missing element map on every recompute.
			topoShape->setShape(face);
			getPreviewShape().setShape(face);

			origin = planeOrigin;
			normal = normalAxis;
			xAxis = turnedX;
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("DatumPlane execute failed: {}", e.GetMessageString());
			return false;
		}
	}
}
