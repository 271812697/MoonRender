#include "feature/PipeFeature.h"
#include "feature/SketcherFeature.h"
#include "feature/SubShapeRef.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"
#include "TopoShape.h"

#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <string>
#include <vector>

namespace MOON
{
	namespace
	{
		/** The first wire inside a shape, whichever way it is nested (a sketch hands
		 * its wire over on its own, a compound wraps it). */
		TopoDS_Wire firstWireOf(const TopoDS_Shape& p_shape)
		{
			if (p_shape.IsNull()) {
				return TopoDS_Wire();
			}
			if (p_shape.ShapeType() == TopAbs_WIRE) {
				return TopoDS::Wire(p_shape);
			}
			for (TopExp_Explorer explorer(p_shape, TopAbs_WIRE);
				explorer.More();
				explorer.Next()) {
				return TopoDS::Wire(explorer.Current());
			}
			return TopoDS_Wire();
		}

		/** The cap that closes one end of a sweep.
		 *
		 * It is made of the end wires of *every* pipe the profile was swept with:
		 * with a profile that has a hole that is the outer wire and the hole wire
		 * together, and the face maker (the same "bullseye" the pad's profile uses)
		 * drills the one into the other - so the cap is an annulus rather than a
		 * disc, and the hole goes through the swept solid.
		 */
		Part::TopoShape makeEndCap(const std::vector<TopoDS_Shape>& p_wires)
		{
			std::vector<Part::TopoShape> wireShapes;
			wireShapes.reserve(p_wires.size());
			for (const TopoDS_Shape& wire : p_wires) {
				if (wire.IsNull()) {
					continue;
				}
				Part::TopoShape wireShape;
				wireShape.setShape(wire);
				wireShapes.push_back(wireShape);
			}
			if (wireShapes.empty()) {
				return Part::TopoShape();
			}
			Part::TopoShape wires;
			wires.makeElementCompound(
				wireShapes,
				nullptr,
				Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
			if (wires.isNull()) {
				return Part::TopoShape();
			}
			return wires.makeElementFace(nullptr, "Part::FaceMakerBullseye");
		}
	}

	PipeFeature::PipeFeature(const std::string& p_name, int p_addSubType)
		: FeatureBaseProfile(p_name, p_addSubType == 0 ? "Pipe" : "PipeCut")
	{
		this->addSubType = p_addSubType;
	}

	PipeFeature::~PipeFeature()
	{
	}

	const std::vector<PipeFeature::Mode>& PipeFeature::allModes()
	{
		static const std::vector<Mode> modes = {
			Mode::Standard, Mode::Fixed, Mode::Frenet, Mode::Binormal };
		return modes;
	}

	const char* PipeFeature::modeName(Mode p_mode)
	{
		switch (p_mode) {
		case Mode::Standard: return "Standard";
		case Mode::Fixed: return "Fixed";
		case Mode::Frenet: return "Frenet";
		case Mode::Binormal: return "Binormal";
		}
		return "Standard";
	}

	const char* PipeFeature::modeLabel(Mode p_mode)
	{
		// The labels FreeCAD's pipe task shows for the same choices.
		switch (p_mode) {
		case Mode::Standard: return "Standard (corrected Frenet)";
		case Mode::Fixed: return "Fixed";
		case Mode::Frenet: return "Frenet";
		case Mode::Binormal: return "Binormal";
		}
		return "Standard (corrected Frenet)";
	}

	PipeFeature::Mode PipeFeature::modeFromName(const std::string& p_name, Mode p_fallback)
	{
		for (Mode mode : allModes()) {
			if (p_name == modeName(mode)) {
				return mode;
			}
		}
		return p_fallback;
	}

	const std::vector<PipeFeature::Transition>& PipeFeature::allTransitions()
	{
		static const std::vector<Transition> transitions = {
			Transition::Transformed,
			Transition::RightCorner,
			Transition::RoundCorner };
		return transitions;
	}

	const char* PipeFeature::transitionName(Transition p_transition)
	{
		switch (p_transition) {
		case Transition::Transformed: return "Transformed";
		case Transition::RightCorner: return "RightCorner";
		case Transition::RoundCorner: return "RoundCorner";
		}
		return "Transformed";
	}

	const char* PipeFeature::transitionLabel(Transition p_transition)
	{
		switch (p_transition) {
		case Transition::Transformed: return "Transformed";
		case Transition::RightCorner: return "Right corner";
		case Transition::RoundCorner: return "Round corner";
		}
		return "Transformed";
	}

	PipeFeature::Transition PipeFeature::transitionFromName(
		const std::string& p_name,
		Transition p_fallback)
	{
		for (Transition transition : allTransitions()) {
			if (p_name == transitionName(transition)) {
				return transition;
			}
		}
		return p_fallback;
	}

	Part::TopoShape PipeFeature::getToolShape()
	{
		return toolShape;
	}

	bool PipeFeature::isToolSubtractive() const
	{
		return addSubType == 1;
	}

	Part::TopoShape PipeFeature::resolveProfileShape()
	{
		if (mProfile == nullptr) {
			return Part::TopoShape();
		}
		// The sketch keeps the face it produced, carrying the placement of its plane;
		// the pose of the sketch feature is applied on top of it, exactly like the
		// pad's profile goes through here.
		Part::TopoShape face = mProfile->getSketcherObj()->getDoneFaceShape();
		Feature::applyWorldTransform(*mProfile, face);
		return face;
	}

	Part::TopoShape PipeFeature::resolveSpineWire()
	{
		if (spineFeature == nullptr) {
			return Part::TopoShape();
		}
		const std::vector<std::string>& references = getSubValues();
		if (references.empty()) {
			// No reference: the whole shape of the spine feature is the path, which
			// is what picking a path sketch means (the way FreeCAD reads a sketch
			// handed to it as the spine).
			const Part::TopoShape whole = spineFeature->getWorldTopoShape();
			Part::TopoShape wire;
			const TopoDS_Wire first = firstWireOf(whole.getShape());
			if (!first.IsNull()) {
				wire.setShape(first);
			}
			return wire;
		}

		// The references name the part of that feature the path is (an edge of the
		// body, the curves of a sketch): they are resolved against the spine feature,
		// not against the shape this pipe is built on.
		BRepBuilderAPI_MakeWire wireMaker;
		std::vector<std::vector<std::string>>& names = m_referenceNames;
		if (names.size() < references.size()) {
			names.resize(references.size());
		}
		for (int i = 0; i < static_cast<int>(references.size()); ++i) {
			Part::TopoShape resolved = ResolveSubShapeRef(
				*spineFeature, references[i], names[i], GetName());
			if (resolved.isNull()) {
				continue;
			}
			const TopoDS_Shape& shape = resolved.getShape();
			if (shape.ShapeType() == TopAbs_EDGE) {
				wireMaker.Add(TopoDS::Edge(shape));
			}
			else {
				const TopoDS_Wire wire = firstWireOf(shape);
				if (!wire.IsNull()) {
					wireMaker.Add(wire);
				}
			}
		}
		if (!wireMaker.IsDone()) {
			return Part::TopoShape();
		}
		Part::TopoShape wire;
		wire.setShape(wireMaker.Wire());
		return wire;
	}

	bool PipeFeature::execute()
	{
		try {
			Part::TopoShape baseShape;
			if (m_baseFeature != nullptr) {
				baseShape = getBaseTopoShape();
			}

			const Part::TopoShape profileFace = resolveProfileShape();
			if (profileFace.isNull()
				|| profileFace.getShape().ShapeType() != TopAbs_FACE) {
				CORE_ERROR(
					"{0}: it has no profile sketch with a closed profile to sweep",
					GetName());
				return false;
			}
			const Part::TopoShape spineShape = resolveSpineWire();
			if (spineShape.isNull()
				|| spineShape.getShape().ShapeType() != TopAbs_WIRE) {
				if (spineFeature == nullptr) {
					CORE_ERROR(
						"{0}: it has no path to sweep along - pick a path sketch, or an "
						"edge of the body below",
						GetName());
				}
				else {
					// The path is configured, but its shape is not there: the chain is
					// rebuilt by walking the links, and this feature is reached from its
					// base before the path it was given has been rebuilt. The pass that
					// follows the document order does the work, so nothing is missing
					// from the document - saying "pick a path" here was misleading.
					CORE_WARN(
						"{0}: the shape of its path ({1}) is not built yet; this pass is "
						"skipped",
						GetName(),
						spineFeature->GetName());
				}
				return false;
			}

			// Every wire of the profile is swept by a pipe of its own.
			//
			// OCC's pipe carries *one* wire, and `Add()`ing several wires to it means
			// "one more section to morph into", not "one more boundary of this
			// section" - which is why a profile with a hole cannot be handed to a
			// single pipe. FreeCAD's pipe reads the same input the same way (its
			// "wiresections"): the outer wire is swept by one pipe, the hole by a
			// second one, and the two shells plus the two end caps are sewn together
			// into the one solid below.
			std::vector<TopoDS_Wire> profileWires;
			for (TopExp_Explorer explorer(profileFace.getShape(), TopAbs_WIRE);
				explorer.More();
				explorer.Next()) {
				profileWires.push_back(TopoDS::Wire(explorer.Current()));
			}
			if (profileWires.empty()) {
				CORE_ERROR("{0}: the profile has no wire to sweep", GetName());
				return false;
			}

			std::vector<Part::TopoShape> shells;
			std::vector<TopoDS_Shape> frontWires;
			std::vector<TopoDS_Shape> backWires;
			bool endsOpen = false;
			for (const TopoDS_Wire& wire : profileWires) {
				// The sweep itself: OCC carries the wire along the path, and the mode
				// and transition decide how it is oriented and what happens at the
				// corners of the path - the same settings, and the same calls, as
				// FreeCAD's pipe sets up.
				BRepOffsetAPI_MakePipeShell pipeMaker(TopoDS::Wire(spineShape.getShape()));
				pipeMaker.SetTolerance(Precision::Confusion());
				switch (transition) {
				case Transition::Transformed:
					pipeMaker.SetTransitionMode(BRepBuilderAPI_Transformed);
					break;
				case Transition::RightCorner:
					pipeMaker.SetTransitionMode(BRepBuilderAPI_RightCorner);
					break;
				case Transition::RoundCorner:
					pipeMaker.SetTransitionMode(BRepBuilderAPI_RoundCorner);
					break;
				}
				switch (mode) {
				case Mode::Standard:
					// The corrected Frenet trihedron, which is what the algorithm
					// does unless it is told otherwise.
					break;
				case Mode::Fixed:
					pipeMaker.SetMode(
						gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
					break;
				case Mode::Frenet:
					pipeMaker.SetMode(true);
					break;
				case Mode::Binormal:
					pipeMaker.SetMode(gp_Dir(binormal.x, binormal.y, binormal.z));
					break;
				}
				pipeMaker.Add(wire, Standard_False, Standard_False);
				pipeMaker.Build();
				if (!pipeMaker.IsDone()) {
					CORE_ERROR(
						"{0}: the sweep could not be built along that path", GetName());
					return false;
				}
				Part::TopoShape shell;
				shell.setShape(pipeMaker.Shape());
				if (shell.isNull()) {
					CORE_ERROR("{0}: the sweep came out empty", GetName());
					return false;
				}
				shells.push_back(shell);
				if (!shell.getShape().Closed()) {
					// The pipe is open at its ends: the sections there are what the
					// caps are built from.
					endsOpen = true;
					TopTools_ListOfShape sections;
					pipeMaker.Simulate(2, sections);
					if (!sections.IsEmpty()) {
						frontWires.push_back(sections.First());
						backWires.push_back(sections.Last());
					}
				}
			}

			// The caps close the sweep; with a hole in the profile the two wires of
			// the end belong to the same face, which is what leaves the hole open.
			std::vector<Part::TopoShape> caps;
			if (endsOpen && !frontWires.empty()
				&& frontWires.size() == backWires.size()) {
				caps.push_back(makeEndCap(frontWires));
				caps.push_back(makeEndCap(backWires));
			}
			else if (endsOpen) {
				CORE_WARN(
					"{0}: the ends of the sweep could not be read, so it stays open "
					"there",
					GetName());
			}

			// Sew the shells and the caps: what comes out is the closed boundary of
			// the swept solid (for a hole, one shell made of the outer surface, the
			// hole's surface and the two annular caps).
			BRepBuilderAPI_Sewing sewer(Precision::Confusion());
			for (const Part::TopoShape& shell : shells) {
				sewer.Add(shell.getShape());
			}
			for (const Part::TopoShape& cap : caps) {
				if (!cap.isNull()) {
					sewer.Add(cap.getShape());
				}
			}
			sewer.Perform();
			const TopoDS_Shape sewn = sewer.SewedShape();
			if (sewn.IsNull()) {
				CORE_ERROR("{0}: the sweep did not sew into a closed shape", GetName());
				return false;
			}

			// And out of that boundary, the solid itself. A shell that has the
			// infinite point inside it is inside out (the sweep of a hole comes out
			// facing the other way), which is what the classifier is asked - the same
			// check FreeCAD's pipe runs on its shells.
			std::vector<Part::TopoShape> solidParts;
			if (sewn.ShapeType() == TopAbs_SOLID) {
				Part::TopoShape single;
				single.setShape(sewn);
				solidParts.push_back(single);
			}
			else {
				Part::TopoShape sewnShape;
				sewnShape.setShape(sewn);
				for (Part::TopoShape& shellShape
					: sewnShape.getSubTopoShapes(TopAbs_SHELL)) {
					Part::TopoShape solid = shellShape.makeElementSolid();
					if (solid.isNull()) {
						continue;
					}
					BRepClass3d_SolidClassifier classifier(solid.getShape());
					classifier.PerformInfinitePoint(Precision::Confusion());
					if (classifier.State() == TopAbs_IN) {
						solid.setShape(solid.getShape().Reversed(), false);
					}
					solidParts.push_back(solid);
				}
			}
			if (solidParts.empty()) {
				CORE_ERROR(
					"{0}: the profile is not a closed wire, so the sweep has no solid "
					"to add",
					GetName());
				return false;
			}

			Part::TopoShape swept = solidParts.front();
			for (int i = 1; i < static_cast<int>(solidParts.size()); ++i) {
				// A section of several separate regions sweeps into several solids:
				// they are one tool for the boolean below.
				swept = swept.makeElementFuse(solidParts[i]);
			}
			if (swept.isNull()) {
				CORE_ERROR("{0}: the sweep came out empty", GetName());
				return false;
			}
			toolShape = swept;
			// The preview is the material the pipe adds or takes away, like the
			// pad's prism: the panel shows what is being built, not the result.
			getPreviewShape() = swept;

			Part::TopoShape result;
			if (!baseShape.isNull()) {
				if (addSubType == 0) {
					// The base goes in first, the same order the pad uses: the shape it
					// hands back is what lets the next feature merge with the result.
					result = baseShape.makeElementFuse(swept);
				}
				else {
					result = baseShape.makeElementCut(swept);
				}
			}
			else {
				if (addSubType == 1) {
					CORE_ERROR(
						"{0}: the pipe has no body to cut from", GetName());
					return false;
				}
				result = swept;
			}
			setResultShape(result);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("{0}: the sweep failed: {1}", GetName(), e.GetMessageString());
			return false;
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("{0}: the sweep failed: {1}", GetName(), e.what());
			return false;
		}
	}
}
