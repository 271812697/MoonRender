#include "feature/LoftFeature.h"
#include "feature/SketcherFeature.h"
#include "feature/SubShapeRef.h"
#include "core/log.h"
#include "TopoShape.h"

#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <string>
#include <vector>

namespace MOON
{
	namespace
	{
		/** The face that caps one end of a loft: the end wires of every skin the loft is
		 * made of, handed to the same face maker the pad and the pipe use - so a section
		 * with a hole comes out as an annulus and the hole runs through the loft. */
		Part::TopoShape capFromWires(const std::vector<Part::TopoShape>& p_wires)
		{
			std::vector<Part::TopoShape> wires;
			wires.reserve(p_wires.size());
			for (const Part::TopoShape& wire : p_wires) {
				if (!wire.isNull()) {
					wires.push_back(wire);
				}
			}
			if (wires.empty()) {
				return Part::TopoShape();
			}
			Part::TopoShape compound;
			compound.makeElementCompound(
				wires,
				nullptr,
				Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
			if (compound.isNull()) {
				return Part::TopoShape();
			}
			return compound.makeElementFace(nullptr, "Part::FaceMakerBullseye");
		}
	}

	LoftFeature::LoftFeature(const std::string& p_name, int p_addSubType)
		: FeatureBaseProfile(p_name, p_addSubType == 0 ? "Loft" : "LoftCut")
	{
		this->addSubType = p_addSubType;
	}

	LoftFeature::~LoftFeature()
	{
	}

	Part::TopoShape LoftFeature::getToolShape()
	{
		return toolShape;
	}

	bool LoftFeature::isToolSubtractive() const
	{
		return addSubType == 1;
	}

	std::vector<Feature*> LoftFeature::allSections() const
	{
		std::vector<Feature*> all;
		all.reserve(sections.size() + 1);
		// The profile is the first section, the way FreeCAD's loft reads its Profile
		// property before it walks Sections.
		if (mProfile != nullptr) {
			all.push_back(mProfile);
		}
		for (Feature* section : sections) {
			all.push_back(section);
		}
		return all;
	}

	std::vector<Part::TopoShape> LoftFeature::sectionWires(
		Feature& p_section,
		const std::vector<std::string>& p_names,
		int p_index)
	{
		const std::vector<std::string>& references = getSubValues();
		Part::TopoShape shape;
		if (p_index >= 0 && p_index < static_cast<int>(references.size())
			&& !references[p_index].empty()) {
			// The section was picked as a part of that feature (a face, a closed edge):
			// the reference is resolved against it, exactly like a pipe's path.
			std::vector<std::string> names = p_names;
			shape = ResolveSubShapeRef(p_section, references[p_index], names, GetName());
		}
		else {
			// The whole shape of the section, its pose applied - a sketch hands its
			// wires over here.
			shape = p_section.getWorldTopoShape();
		}
		if (shape.isNull()) {
			return {};
		}

		std::vector<Part::TopoShape> wires = shape.getSubTopoShapes(TopAbs_WIRE);
		if (wires.empty()) {
			// No wire in it: its free edges (the curves of an open sketch) are made into
			// wires, which is the fallback FreeCAD's loft takes as well.
			const std::vector<Part::TopoShape> edges
				= shape.getSubTopoShapes(TopAbs_EDGE, TopAbs_WIRE);
			if (!edges.empty()) {
				Part::TopoShape compound;
				compound.makeElementCompound(
					edges,
					nullptr,
					Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
				if (!compound.isNull()) {
					wires = compound.makeElementWires().getSubTopoShapes(TopAbs_WIRE);
				}
			}
		}
		if (wires.empty()) {
			// Still nothing: the section is a point, which is how a loft ends in a tip.
			wires = shape.getSubTopoShapes(TopAbs_VERTEX);
		}
		return wires;
	}

	bool LoftFeature::execute()
	{
		try {
			Part::TopoShape baseShape;
			if (m_baseFeature != nullptr) {
				baseShape = getBaseTopoShape();
			}

			const std::vector<Feature*> all = allSections();
			if (all.size() < 2) {
				CORE_ERROR(
					"{0}: a loft needs at least two sections - the profile and one "
					"section to loft it through",
					GetName());
				return false;
			}

			// The references live in the feature's own sub-value block: the i-th entry
			// belongs to the i-th section, the profile first (see the header).
			if (m_referenceNames.size() < all.size()) {
				m_referenceNames.resize(all.size());
			}

			// Every section brings the same number of wires, so the loft is one skin per
			// wire index - a section with a hole gives the outer wire and the hole wire,
			// and each of them is lofted on its own and sewn with the rest below.
			std::vector<std::vector<Part::TopoShape>> wireSections;
			for (int i = 0; i < static_cast<int>(all.size()); ++i) {
				if (all[i] == nullptr) {
					CORE_ERROR("{0}: one of its sections is gone", GetName());
					return false;
				}
				const std::vector<Part::TopoShape> wires
					= sectionWires(*all[i], m_referenceNames[i], i);
				if (wires.empty()) {
					CORE_ERROR(
						"{0}: the section '{1}' has nothing to loft through",
						GetName(),
						all[i]->GetName());
					return false;
				}
				if (wireSections.empty()) {
					wireSections.resize(wires.size());
				}
				else if (wireSections.size() != wires.size()) {
					CORE_ERROR(
						"{0}: every section needs the same amount of wires or vertices "
						"as the profile - '{1}' has {2}, the profile has {3}",
						GetName(),
						all[i]->GetName(),
						wires.size(),
						wireSections.size());
					return false;
				}
				for (int w = 0; w < static_cast<int>(wires.size()); ++w) {
					wireSections[w].push_back(wires[w]);
				}
			}

			// A ring closes on itself, so it has no ends to cap - and it needs three
			// sections at least, which is what FreeCAD's loft checks as well.
			const bool ringClosed = closed && all.size() >= 3;

			std::vector<Part::TopoShape> shells;
			shells.reserve(wireSections.size());
			for (std::vector<Part::TopoShape>& sectionWiresForSkin : wireSections) {
				shells.push_back(Part::TopoShape().makeElementLoft(
					sectionWiresForSkin,
					Part::IsSolid::notSolid,
					ruled ? Part::IsRuled::ruled : Part::IsRuled::notRuled,
					ringClosed ? Part::IsClosed::closed : Part::IsClosed::notClosed));
				if (shells.back().isNull()) {
					CORE_ERROR("{0}: the loft could not be built through those sections", GetName());
					return false;
				}
			}

			// The ends are capped, unless the loft runs in a ring or ends in a point.
			std::vector<Part::TopoShape> caps;
			if (!ringClosed) {
				const bool endsAreWires
					= wireSections[0].front().getShape().ShapeType() != TopAbs_VERTEX;
				if (endsAreWires) {
					std::vector<Part::TopoShape> frontWires;
					std::vector<Part::TopoShape> backWires;
					for (const std::vector<Part::TopoShape>& sectionWiresForSkin : wireSections) {
						frontWires.push_back(sectionWiresForSkin.front());
						backWires.push_back(sectionWiresForSkin.back());
					}
					caps.push_back(capFromWires(frontWires));
					caps.push_back(capFromWires(backWires));
				}
			}

			// Sew the skins and the caps: what comes out is the closed boundary of the
			// lofted solid.
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
				CORE_ERROR("{0}: the loft did not sew into a closed shape", GetName());
				return false;
			}

			// And out of that boundary, the solid itself. A shell that has the infinite
			// point inside it is inside out, which is what the classifier is asked.
			std::vector<Part::TopoShape> solidParts;
			if (sewn.ShapeType() == TopAbs_SOLID) {
				Part::TopoShape single;
				single.setShape(sewn);
				solidParts.push_back(single);
			}
			else {
				Part::TopoShape sewnShape;
				sewnShape.setShape(sewn);
				for (Part::TopoShape& shellShape : sewnShape.getSubTopoShapes(TopAbs_SHELL)) {
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
					"{0}: the sections do not close a solid, so there is nothing to add",
					GetName());
				return false;
			}

			Part::TopoShape lofted = solidParts.front();
			for (int i = 1; i < static_cast<int>(solidParts.size()); ++i) {
				// A section of several separate regions lofts into several solids: they
				// are one tool for the boolean below.
				lofted = lofted.makeElementFuse(solidParts[i]);
			}
			if (lofted.isNull()) {
				CORE_ERROR("{0}: the loft came out empty", GetName());
				return false;
			}
			toolShape = lofted;
			// The preview is the material the loft adds or takes away, like the pad's
			// prism: the panel shows what is being built, not the result.
			getPreviewShape() = lofted;

			Part::TopoShape result;
			if (!baseShape.isNull()) {
				if (addSubType == 0) {
					result = baseShape.makeElementFuse(lofted);
				}
				else {
					result = baseShape.makeElementCut(lofted);
				}
			}
			else {
				if (addSubType == 1) {
					CORE_ERROR("{0}: the loft has no body to cut from", GetName());
					return false;
				}
				result = lofted;
			}
			setResultShape(result);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("{0}: the loft failed: {1}", GetName(), e.GetMessageString());
			return false;
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("{0}: the loft failed: {1}", GetName(), e.what());
			return false;
		}
	}
}
