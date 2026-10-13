#include "feature/HelixFeature.h"
#include "feature/SketcherFeature.h"
#include "feature/SubShapeRef.h"
#include "core/log.h"
#include "TopoShape.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomFill_Trihedron.hxx>
#include <Precision.hxx>
#include <ShapeFix_Solid.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MOON
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		/** FreeCAD's default, and the factor the result's tolerance is opened by: a
		 * helical sweep is an approximation, and the fusion below needs that much room
		 * to succeed (see the header). */
		constexpr double kTolerance = 0.1;

		double toRadians(double p_degrees)
		{
			return p_degrees * kPi / 180.0;
		}

		double toDegrees(double p_radians)
		{
			return p_radians * 180.0 / kPi;
		}

		bool isNull(const Maths::FVector3& p_vector)
		{
			return Maths::FVector3::Length(p_vector) < 1.0e-12f;
		}

		/** A straight edge of the shape, as the point it starts at and its direction:
		 * what a datum line - or any feature that carries a line, an edge of the body
		 * that was picked - hands over as an axis. */
		bool lineOfShape(const TopoDS_Shape& p_shape, gp_Pnt& p_base, gp_Dir& p_direction)
		{
			if (p_shape.IsNull()) {
				return false;
			}
			for (TopExp_Explorer explorer(p_shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
				const BRepAdaptor_Curve curve(TopoDS::Edge(explorer.Current()));
				if (curve.GetType() != GeomAbs_Line) {
					continue;
				}
				p_base = curve.Value(curve.FirstParameter());
				p_direction = curve.Line().Direction();
				return true;
			}
			return false;
		}

		/** The center of the shape's bounding box: where the profile is taken to be, the
		 * same reading FreeCAD's helix takes of its profile. */
		bool centerOfShape(const TopoDS_Shape& p_shape, gp_Pnt& p_center)
		{
			if (p_shape.IsNull()) {
				return false;
			}
			Bnd_Box box;
			BRepBndLib::Add(p_shape, box);
			if (box.IsVoid()) {
				return false;
			}
			double xMin, yMin, zMin, xMax, yMax, zMax;
			box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
			p_center = gp_Pnt(
				(xMin + xMax) * 0.5, (yMin + yMax) * 0.5, (zMin + zMax) * 0.5);
			return true;
		}
	}

	HelixFeature::HelixFeature(const std::string& p_name, int p_addSubType)
		: FeatureBaseProfile(p_name, p_addSubType == 0 ? "Helix" : "HelixCut")
	{
		this->addSubType = p_addSubType;
	}

	HelixFeature::~HelixFeature()
	{
	}

	Part::TopoShape HelixFeature::getToolShape()
	{
		return toolShape;
	}

	bool HelixFeature::isToolSubtractive() const
	{
		return addSubType == 1;
	}

	const std::vector<HelixFeature::Mode>& HelixFeature::allModes()
	{
		static const std::vector<Mode> modes = {
			Mode::PitchHeightAngle,
			Mode::PitchTurnsAngle,
			Mode::HeightTurnsAngle,
			Mode::HeightTurnsGrowth };
		return modes;
	}

	const char* HelixFeature::modeName(Mode p_mode)
	{
		// The names are the ones FreeCAD's helix mode enum carries, which is what a
		// document stores.
		switch (p_mode) {
		case Mode::PitchHeightAngle: return "pitch_height_angle";
		case Mode::PitchTurnsAngle: return "pitch_turns_angle";
		case Mode::HeightTurnsAngle: return "height_turns_angle";
		case Mode::HeightTurnsGrowth: return "height_turns_growth";
		}
		return "pitch_height_angle";
	}

	const char* HelixFeature::modeLabel(Mode p_mode)
	{
		// What the panel shows for them: which three fields are the ones being given.
		switch (p_mode) {
		case Mode::PitchHeightAngle: return "Pitch, height, angle";
		case Mode::PitchTurnsAngle: return "Pitch, turns, angle";
		case Mode::HeightTurnsAngle: return "Height, turns, angle";
		case Mode::HeightTurnsGrowth: return "Height, turns, growth";
		}
		return "Pitch, height, angle";
	}

	HelixFeature::Mode HelixFeature::modeFromName(const std::string& p_name, Mode p_fallback)
	{
		for (Mode mode : allModes()) {
			if (p_name == modeName(mode)) {
				return mode;
			}
		}
		return p_fallback;
	}

	const std::vector<HelixFeature::AxisMode>& HelixFeature::allAxisModes()
	{
		static const std::vector<AxisMode> modes = {
			AxisMode::Normal, AxisMode::Vertical, AxisMode::Horizontal, AxisMode::Reference };
		return modes;
	}

	const char* HelixFeature::axisModeName(AxisMode p_mode)
	{
		switch (p_mode) {
		case AxisMode::Normal: return "Normal";
		case AxisMode::Vertical: return "Vertical";
		case AxisMode::Horizontal: return "Horizontal";
		case AxisMode::Reference: return "Reference";
		}
		return "Vertical";
	}

	const char* HelixFeature::axisModeLabel(AxisMode p_mode)
	{
		// FreeCAD's wording for the same three axes, and what its panel says about the
		// picked one.
		switch (p_mode) {
		case AxisMode::Normal: return "Normal sketch axis";
		case AxisMode::Vertical: return "Vertical sketch axis";
		case AxisMode::Horizontal: return "Horizontal sketch axis";
		case AxisMode::Reference: return "Pick a reference (datum line or edge)";
		}
		return "Vertical sketch axis";
	}

	HelixFeature::AxisMode HelixFeature::axisModeFromName(
		const std::string& p_name,
		AxisMode p_fallback)
	{
		for (AxisMode mode : allAxisModes()) {
			if (p_name == axisModeName(mode)) {
				return mode;
			}
		}
		return p_fallback;
	}

	bool HelixFeature::normalizeParameters()
	{
		// Which three of the five parameters are given, and which are worked out from
		// them: the arithmetic, and the complaints, of FreeCAD's helix.
		switch (mode) {
		case Mode::PitchHeightAngle:
			if (pitch < Precision::Confusion()) {
				CORE_ERROR("{0}: the pitch is too small", GetName());
				return false;
			}
			if (height < Precision::Confusion()) {
				CORE_ERROR("{0}: the height is too small", GetName());
				return false;
			}
			turns = height / pitch;
			growth = pitch * std::tan(toRadians(angle));
			break;
		case Mode::PitchTurnsAngle:
			if (pitch < Precision::Confusion()) {
				CORE_ERROR("{0}: the pitch is too small", GetName());
				return false;
			}
			if (turns < Precision::Confusion()) {
				CORE_ERROR("{0}: the number of turns is too small", GetName());
				return false;
			}
			height = turns * pitch;
			growth = pitch * std::tan(toRadians(angle));
			break;
		case Mode::HeightTurnsAngle:
			if (height < Precision::Confusion()) {
				CORE_ERROR("{0}: the height is too small", GetName());
				return false;
			}
			if (turns < Precision::Confusion()) {
				CORE_ERROR("{0}: the number of turns is too small", GetName());
				return false;
			}
			pitch = height / turns;
			growth = pitch * std::tan(toRadians(angle));
			break;
		case Mode::HeightTurnsGrowth:
			if (turns < Precision::Confusion()) {
				CORE_ERROR("{0}: the number of turns is too small", GetName());
				return false;
			}
			if (height < Precision::Confusion()
				&& std::abs(growth) < Precision::Confusion()
				&& turns > 1.0) {
				CORE_ERROR(
					"{0}: either the height or the growth has to be given", GetName());
				return false;
			}
			pitch = height / turns;
			if (height > 0.0) {
				angle = toDegrees(std::atan(turns * growth / height));
			}
			// A flat spiral (no height) is a perfectly good helix, and FreeCAD leaves
			// the angle alone there so that going back to another mode still works.
			break;
		}
		return true;
	}

	bool HelixFeature::resolveAxis(const Part::TopoShape& p_profileFace)
	{
		if (axisFeature != nullptr) {
			if (axisMode != AxisMode::Reference) {
				// The axis is one of the sketch's own: a line that happens to be selected
				// in the tree or the viewport is not what this helix winds around.
				CORE_INFO(
					"{0}: the axis is set to a sketch axis ('{1}'), so the picked line "
					"('{2}') is left alone",
					GetName(),
					axisModeLabel(axisMode),
					axisFeature->GetName());
			}
			else {
			gp_Pnt base;
			gp_Dir direction;
			Part::TopoShape axisShape;
			const std::vector<std::string>& references = getSubValues();
			if (!references.empty() && !references[0].empty()) {
				// The axis was picked as an edge of that feature: the reference is
				// resolved against it, the way a pipe's path reference is.
				if (m_referenceNames.size() < references.size()) {
					m_referenceNames.resize(references.size());
				}
				axisShape = ResolveSubShapeRef(
					*axisFeature, references[0], m_referenceNames[0], GetName());
			}
			else {
				axisShape = axisFeature->getWorldTopoShape();
			}
			// The axis is a line that feature carries - a datum line, or anything else
			// that hands over a straight edge. Its shape is read with its pose applied,
			// the way the shape of a feature is read everywhere else.
			if (lineOfShape(axisShape.getShape(), base, direction)) {
				axisBase = Maths::FVector3(
					static_cast<float>(base.X()),
					static_cast<float>(base.Y()),
					static_cast<float>(base.Z()));
				axisDirection = Maths::FVector3(
					static_cast<float>(direction.X()),
					static_cast<float>(direction.Y()),
					static_cast<float>(direction.Z()));
				return true;
			}
			CORE_ERROR(
				"{0}: the axis it was given ('{1}') has no straight edge to wind around",
				GetName(),
				axisFeature->GetName());
			return false;
		}

		}
		// The axis is a picked line, and none was: worth saying, because the three sketch
		// axes below would build a coil just fine and quietly ignore what was asked for.
		if (axisMode == AxisMode::Reference) {
			CORE_ERROR("{0}: its axis is a picked line, and none was picked", GetName());
			return false;
		}
		// One of the sketch's own axes, all of them through the origin of the sketch: the
		// vertical and the horizontal one lie *in* the profile's plane, the normal leaves
		// it. A helix winds *around* its axis with the profile carried around it, so the
		// further the profile sits from the axis the wider the coil - which is why an axis
		// in the plane is the one to pick for a coil, and why the normal (the profile
		// centred on the axis) gives a coil with no radius at all.
		if (mProfile == nullptr) {
			CORE_ERROR(
				"{0}: it has no profile sketch to take an axis from", GetName());
			return false;
		}
		// The three sketch axes, all of them through the origin of the sketch: the normal
		// leaves the profile's plane, the other two lie in it. Only the last is the one a
		// helix usually wants - an axis the profile winds around - but FreeCAD offers all
		// three, and a profile sitting off to one side of the sketch works with any of
		// them.
		axisBase = mProfile->transform.GetWorldPosition();
		switch (axisMode) {
		case AxisMode::Normal:
			axisDirection = mProfile->transform.GetWorldForward();
			break;
		case AxisMode::Horizontal:
			axisDirection = mProfile->transform.GetWorldRight();
			break;
		case AxisMode::Vertical:
		default:
			axisDirection = mProfile->transform.GetWorldUp();
			break;
		}
		if (Maths::FVector3::Length(axisDirection) < 1.0e-6f) {
			CORE_ERROR("{0}: its profile sketch has no plane to take an axis from", GetName());
			return false;
		}
		axisDirection = Maths::FVector3::Normalize(axisDirection);
		return true;
	}

	Part::TopoShape HelixFeature::helixPath(
		const Part::TopoShape& p_profileFace,
		double p_breakAtTurn)
	{
		const bool growthMode = mode == Mode::HeightTurnsGrowth;
		// A tapered helix with no angle is a straight one, and the angle it carries is
		// then whatever the mode before it left there (see normalizeParameters).
		const double pathAngle
			= std::abs(angle) < Precision::Confusion() ? 0.0 : angle;

		const gp_Pnt axisPoint(axisBase.x, axisBase.y, axisBase.z);
		const gp_Dir axisDir(axisDirection.x, axisDirection.y, axisDirection.z);
		const Maths::FVector3 axisVector = axisDirection;

		// The direction from the axis towards where the helix has to start - the cross
		// product of the axis with the profile's normal, the same construction FreeCAD
		// uses to find the radius the profile sits at.
		gp_Pln plane;
		if (!p_profileFace.findPlane(plane)) {
			throw Base::ValueError("the profile is not planar");
		}
		const gp_Dir profileNormal = plane.Axis().Direction();
		const Maths::FVector3 normal(
			static_cast<float>(profileNormal.X()),
			static_cast<float>(profileNormal.Y()),
			static_cast<float>(profileNormal.Z()));
		Maths::FVector3 start = Maths::FVector3::Cross(axisVector, normal);
		if (isNull(start)) {
			// The axis is (nearly) along the profile's normal: only the twist of the
			// helix matters then, and any direction in the profile's plane will do.
			start = Maths::FVector3::Cross(normal, Maths::FVector3(1.0f, 2.0f, 3.0f));
			if (isNull(start)) {
				start = Maths::FVector3::Cross(normal, Maths::FVector3(3.0f, 2.0f, 1.0f));
			}
		}
		start = Maths::FVector3::Normalize(start);
		const gp_Dir startDir(start.x, start.y, start.z);

		gp_Pnt profileCenter;
		if (!centerOfShape(p_profileFace.getShape(), profileCenter)) {
			throw Base::ValueError("the profile has no shape to wind around");
		}
		const Maths::FVector3 center(
			static_cast<float>(profileCenter.X()),
			static_cast<float>(profileCenter.Y()),
			static_cast<float>(profileCenter.Z()));
		const Maths::FVector3 base(axisBase.x, axisBase.y, axisBase.z);

		// The radius the profile sits at, measured from the axis along "start" - with the
		// factor of 100 FreeCAD puts in front of it: the profile has to stay small
		// compared to the curvature of the helix. The offset is scaled back with the
		// square of that factor below.
		const float axisOffset
			= 100.0f * Maths::FVector3::Dot(center - base, start);
		double radius = std::abs(axisOffset);
		const bool turned = axisOffset < 0.0f;
		const double startOffset = 10000.0
			* std::abs(
				(pathAngle <= 0.0 ? 1.0 : 0.0) * Maths::FVector3::Dot(center, axisVector)
				- Maths::FVector3::Dot(base, axisVector));

		if (radius < Precision::Confusion()) {
			// The profile sits on the axis: that only works while the axis is out of the
			// profile's plane, and it leaves the helix with a radius to fall back on.
			if (std::abs(Maths::FVector3::Dot(axisVector, normal)) < Precision::Confusion()) {
				throw Base::ValueError(
					"the helix would wind through its own profile and intersect itself");
			}
			radius = 1000.0;
		}

		const double radiusTop = growthMode ? radius + turns * growth
		                                    : radius + height * std::tan(toRadians(pathAngle));

		// The helix is built with its axis on +Z and its start at (radius, 0, 0); what
		// follows moves it onto the axis this feature was given, with its start in the
		// plane of the profile.
		TopoDS_Shape path = Part::TopoShape().makeSpiralHelix(
			radius, radiusTop, height, turns, p_breakAtTurn, leftHanded);
		if (path.IsNull()) {
			throw Base::ValueError("OCC could not build the helix");
		}

		const gp_Pnt origin(0.0, 0.0, 0.0);
		const gp_Dir asBuiltAxis(0.0, 0.0, 1.0);
		const gp_Dir asBuiltStart(1.0, 0.0, 0.0);
		gp_Trsf move;
		if (std::abs(startOffset) > 0.0) {
			move.SetTranslation(startOffset * gp_Vec(asBuiltAxis));
			path.Move(TopLoc_Location(move));
		}
		if (reversed) {
			// Undo the radius factor the offset above was scaled by.
			move.SetRotation(gp_Ax1(origin, asBuiltStart), kPi);
			path.Move(TopLoc_Location(move));
		}
		if (turned) {
			move.SetRotation(gp_Ax1(origin, asBuiltAxis), kPi);
			path.Move(TopLoc_Location(move));
		}
		move.SetTransformation(
			gp_Ax3(origin, asBuiltAxis, asBuiltStart),
			gp_Ax3(axisPoint, axisDir, startDir));
		path.Move(TopLoc_Location(move).Inverted());

		Part::TopoShape wire;
		wire.setShape(path);
		return wire;
	}

	bool HelixFeature::execute()
	{
		try {
			Part::TopoShape baseShape;
			if (m_baseFeature != nullptr) {
				baseShape = getBaseTopoShape();
			}

			if (!normalizeParameters()) {
				return false;
			}

			const Part::TopoShape profileFace = getProfileFace();
			if (profileFace.isNull() || profileFace.getShape().ShapeType() != TopAbs_FACE) {
				CORE_ERROR(
					"{0}: it has no profile sketch with a closed profile to sweep",
					GetName());
				return false;
			}
			// Only a planar profile is carried along a helix, the way FreeCAD's helix
			// only accepts one: the cap of the swept solid is built from the profile
			// itself, which needs it flat.
			{
				gp_Pln plane;
				if (!profileFace.findPlane(plane)) {
					CORE_ERROR(
						"{0}: its profile has to be planar to be swept along a helix",
						GetName());
					return false;
				}
			}
			if (!resolveAxis(profileFace)) {
				return false;
			}

			// A path that is broken at every whole turn keeps OCC from tripping over its
			// own start point, but a tapered helix has to be one piece or the solid comes
			// out invalid - which is the pair of cases FreeCAD's helix decides between.
			const Part::TopoShape path = helixPath(
				profileFace, std::abs(angle) < Precision::Confusion() ? 1.0 : 1000.0);
			if (path.isNull() || path.getShape().ShapeType() != TopAbs_WIRE) {
				CORE_ERROR("{0}: the helix path could not be built", GetName());
				return false;
			}

			// The sweep itself: a frenet pipe carrying the profile along the path, the
			// same call FreeCAD's helix makes.
			BRepOffsetAPI_MakePipe pipeMaker(
				TopoDS::Wire(path.getShape()),
				TopoDS::Face(profileFace.getShape()),
				GeomFill_IsFrenet,
				Standard_False);
			TopoDS_Shape swept = pipeMaker.Shape();
			if (swept.IsNull()) {
				CORE_ERROR("{0}: the sweep along the helix came out empty", GetName());
				return false;
			}
			BRepClass3d_SolidClassifier classifier(swept);
			classifier.PerformInfinitePoint(Precision::Confusion());
			if (classifier.State() == TopAbs_IN) {
				swept.Reverse();
			}

			// The tolerance the result is allowed: a helical sweep is an approximation,
			// and the fusion below needs it opened by this much to succeed - the same
			// correction, on the same scale, that FreeCAD applies.
			{
				Bnd_Box bounds;
				BRepBndLib::Add(path.getShape(), bounds);
				const double size = std::sqrt(bounds.SquareExtent());
				ShapeFix_ShapeTolerance toleranceFix;
				toleranceFix.LimitTolerance(swept, Precision::Confusion() * size * kTolerance);
			}
			// ...and a last try at making a valid solid out of it.
			if (swept.ShapeType() == TopAbs_SOLID) {
				ShapeFix_Solid solidFix;
				solidFix.Init(TopoDS::Solid(swept));
				if (solidFix.Perform()) {
					swept = solidFix.Solid();
				}
			}

			Part::TopoShape sweptShape;
			sweptShape.setShape(swept);
			if (sweptShape.isNull()) {
				CORE_ERROR("{0}: the helix came out empty", GetName());
				return false;
			}
			toolShape = sweptShape;
			// The preview is the material the helix adds or takes away, like the pad's
			// prism: the panel shows what is being built, not the result.
			getPreviewShape() = sweptShape;

			Part::TopoShape result;
			if (!baseShape.isNull()) {
				if (addSubType == 0) {
					result = baseShape.makeElementFuse(sweptShape);
				}
				else {
					result = baseShape.makeElementCut(sweptShape);
				}
			}
			else {
				if (addSubType == 1) {
					CORE_ERROR("{0}: the helix has no body to cut from", GetName());
					return false;
				}
				result = sweptShape;
			}
			setResultShape(result);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("{0}: the helix failed: {1}", GetName(), e.GetMessageString());
			return false;
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("{0}: the helix failed: {1}", GetName(), e.what());
			return false;
		}
	}
}
