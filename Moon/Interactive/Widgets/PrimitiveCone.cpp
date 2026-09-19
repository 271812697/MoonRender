#include "Interactive/Widgets/PrimitiveCone.h"
#include "Interactive/Im3DRenderer.h"
#include "Qtimgui/imgui/imgui.h"

#include "Interactive/MathUtil/MathUtil.h"
#include "renderer/SceneView.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include "core/component/CTopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "feature/Feature.h"
#include "TopoShape.h"

#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Precision.hxx>

namespace MOON {
	
	PrimitiveCone::PrimitiveCone(const std::string& name) :PrimitiveShape(name)
	{
		translation = { 0,0,0 };
		normal = { 0,0,1 };
		height = 1.0f;
		radiusTop = 0.1f;
		radiusBottom = 0.5f;
	}
	PrimitiveCone::~PrimitiveCone()
	{
	}
	void PrimitiveCone::onUpdate()
	{
		renderer->clydinerEdit(renderer->makeId("cone"), translation, normal, height, radiusTop, radiusBottom,false);
	}

	void PrimitiveCone::createTopoShape()
	{	
		TopoDS_Shape ResultShape;
		if (std::abs(radiusTop - radiusBottom) < Precision::Confusion()) {
			BRepPrimAPI_MakeCylinder mkCylinder(radiusTop, height);
			ResultShape = mkCylinder.Shape();
		}
		else
		{
			BRepPrimAPI_MakeCone mkCone(radiusBottom, radiusTop, height);
			ResultShape = mkCone.Shape();
		}
		auto topoActor = new Feature3D("ConeFeature", "Cone");
		const auto& topoComp = topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();

		topo.setShape(ResultShape, false);
		Eigen::Matrix4f mat1 = Coord3(Eigen::Vector3f(0, 0, -1) * height * 0.5, Eigen::Matrix3f::Identity(), { 1,1,1 });

		Eigen::Matrix4f mat2 = Coord3({ 0,0,0 }, RotationMatrixZ(normal), { 1,1,1 });
		Eigen::Matrix4f mat3 = Coord3(translation, Eigen::Matrix3f::Identity(), { 1,1,1 });
		Eigen::Matrix4f mat = mat3 * mat2 * mat1;
		Base::Matrix4D mm(
			mat(0, 0), mat(0, 1), mat(0, 2), mat(0, 3),
			mat(1, 0), mat(1, 1), mat(1, 2), mat(1, 3),
			mat(2, 0), mat(2, 1), mat(2, 2), mat(2, 3),
			mat(3, 0), mat(3, 1), mat(3, 2), mat(3, 3)
		);
		topo.setTransform(mm);
		topoActor->makeDone();
	}
}