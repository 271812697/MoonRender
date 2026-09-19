#include "Interactive/Widgets/PrimitiveCylinder.h"
#include "Interactive/Im3DRenderer.h"

#include "Interactive/MathUtil/MathUtil.h"

#include "core/component/CTopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "feature/Feature.h"
#include "TopoShape.h"


#include <BRepPrimAPI_MakeCylinder.hxx>

namespace MOON {
	
	PrimitiveCylinder::PrimitiveCylinder(const std::string& name) :PrimitiveShape(name)
	{
		translation = { 0,0,0 };
		normal = { 0,0,1 };
		height = 1.0f;
		radiusTop = 0.5f;
		radiusBottom = 0.5f;
	}
	PrimitiveCylinder::~PrimitiveCylinder()
	{
	}
	void PrimitiveCylinder::onUpdate()
	{
		renderer->clydinerEdit(renderer->makeId("cylinder"),translation,normal,height,radiusTop,radiusBottom);
		

	}

	void PrimitiveCylinder::createTopoShape()
	{	
		
		BRepPrimAPI_MakeCylinder mkCylinder(radiusTop, height);
		TopoDS_Shape ResultShape = mkCylinder.Shape();
	
		auto topoActor = new Feature3D("CylinderFearture", "Cylinder");
		const auto& topoComp = topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();

		topo.setShape(ResultShape,false);
		Eigen::Matrix4f mat1 = Coord3( Eigen::Vector3f(0,0,-1)*height * 0.5, Eigen::Matrix3f::Identity(), { 1,1,1 });
		
		Eigen::Matrix4f mat2 = Coord3({ 0,0,0 }, RotationMatrixZ(normal), {1,1,1});
		Eigen::Matrix4f mat3 = Coord3(translation, Eigen::Matrix3f::Identity(), { 1,1,1 });
		Eigen::Matrix4f mat = mat3*mat2* mat1;
		Base::Matrix4D mm(
			mat(0,0), mat(0, 1),mat(0, 2), mat(0, 3),
			mat(1, 0), mat(1, 1), mat(1, 2), mat(1, 3),
			mat(2, 0), mat(2, 1), mat(2, 2), mat(2, 3),
			mat(3, 0), mat(3, 1), mat(3, 2), mat(3, 3)
		);
		topo.setTransform(mm);
		topoActor->makeDone();
	}
}