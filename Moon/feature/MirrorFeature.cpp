#include "MirrorFeature.h"

#include "core/component/TopoShapeActor.h"
#include "TopoShape.h"
#include "ElementNamingUtils.h"
#include "TransformHelper.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_Shape.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <tracy/Tracy.hpp>

namespace MOON
{
	MirrorFeature::MirrorFeature(const std::string& p_name)
		: Feature3D(p_name, "Mirror")
	{
	}

	MirrorFeature::~MirrorFeature()
	{
	}

	bool MirrorFeature::applySketchPlane(int p_plane)
	{
		SketcherObj* sketch = findBaseSketch();
		if (sketch == nullptr) {
			return false;
		}

		const Base::Vector3d origin = sketch->getPlaneOrigin();
		Base::Vector3d normal;
		switch (p_plane) {
			case PlaneSketchX:
				// The plane that holds the X axis of the sketch runs along it, so the
				// Y axis is its normal. FreeCAD mirrors across the horizontal axis of
				// a sketch by taking its vertical axis as that normal, which is the
				// same thing.
				normal = sketch->getPlaneYAxis();
				break;
			case PlaneSketchY:
				normal = sketch->getPlaneXAxis();
				break;
			default: {
				double n[3] = {0.0, 0.0, 1.0};
				sketch->getPlaneNormal(n);
				normal = Base::Vector3d(n[0], n[1], n[2]);
				break;
			}
		}
		if (normal.Length() < Precision::Confusion()) {
			return false;
		}

		plane = gp_Ax2(
			gp_Pnt(origin.x, origin.y, origin.z),
			gp_Dir(normal.x, normal.y, normal.z));
		planeType = p_plane;
		return true;
	}

	bool MirrorFeature::applyPlaneFromFace(const Part::TopoShape& p_face)
	{
		if (p_face.isNull()) {
			return false;
		}

		TopoDS_Shape shape = p_face.getShape();
		if (shape.ShapeType() != TopAbs_FACE) {
			if (!p_face.hasSubShape(TopAbs_FACE)) {
				return false;
			}
			shape = p_face.getSubShape(TopAbs_FACE, 1);
		}

		BRepAdaptor_Surface adapt(TopoDS::Face(shape));
		if (adapt.GetType() != GeomAbs_Plane) {
			CORE_WARN(
				"[Mirror] {0}: the picked face is not planar, it cannot be a mirror "
				"plane",
				GetName());
			return false;
		}

		// Any point of the plane will do as its location, so the centre of the face
		// is used; only the normal decides where the reflection lands.
		GProp_GProps props;
		BRepGProp::SurfaceProperties(shape, props);
		plane = gp_Ax2(props.CentreOfMass(), adapt.Plane().Axis().Direction());
		planeType = PlanePickedFace;
		return true;
	}

	bool MirrorFeature::execute()
	{
		try {
			Part::TopoShape baseShape = getBaseTopoShape();
			if (baseShape.isNull()) {
				CORE_ERROR(
					"[Mirror] {0}: there is no shape below it to mirror", GetName());
				return false;
			}

			// The plane belongs to the sketch the shape was built from, so it is read
			// again here rather than trusted from the last time the panel set it:
			// moving that sketch has to move the plane with it.
			if (planeType != PlanePickedFace) {
				applySketchPlane(planeType);
			}

			ZoneScopedN("Mirror");
			gp_Trsf reflection;
			reflection.SetMirror(plane);

			// The identity is the original, which the base holds already.
			std::vector<gp_Trsf> transformations;
			transformations.push_back(gp_Trsf());
			transformations.push_back(reflection);

			Part::TopoShape result;
			Part::TopoShape preview;
			if (mode == static_cast<int>(TransformMode::Feature)) {
				result = BuildFeatureModeResult(
					*this, originals, transformations, preview);
			}
			else {
				// The original and its image, fused into one shape. The index keeps
				// the elements of the image apart from the ones they come from.
				std::vector<Part::TopoShape> instances;
				instances.reserve(2);
				instances.push_back(baseShape);
				instances.push_back(baseShape.makeElementTransform(
					reflection, Data::indexSuffix(2).c_str()));
				// The preview shows the image itself; the original is part of the
				// shape that is already on screen.
				preview = instances.back();
				result.makeElementFuse(instances);
			}
			if (result.isNull()) {
				CORE_ERROR(
					"[Mirror] {0}: fusing the mirror image produced no shape", GetName());
				return false;
			}

			getPreviewShape() = preview.isNull() ? result : preview;
			setResultShape(result);
			CORE_INFO("[Mirror] {0}: mirrored and kept the original", GetName());
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("[Mirror] {0}: {1}", GetName(), e.GetMessageString());
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("[Mirror] {0}: {1}", GetName(), e.what());
		}
		catch (const std::exception& e) {
			CORE_ERROR("[Mirror] {0}: {1}", GetName(), e.what());
		}
		catch (...) {
			CORE_ERROR("[Mirror] {0}: unknown error", GetName());
		}
		return false;
	}
}
