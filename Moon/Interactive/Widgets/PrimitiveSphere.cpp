#include "Interactive/Widgets/PrimitiveSphere.h"
#include "Interactive/Im3DRenderer.h"

#include "Interactive/MathUtil/MathUtil.h"
#include "core/component/CTopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "feature/Feature.h"
#include "TopoShape.h"

#include <BRepPrimAPI_MakeSphere.hxx>

namespace MOON {
	
	PrimitiveSphere::PrimitiveSphere(const std::string& name) :PrimitiveShape(name)
	{
		translation = { 0,0,0 };
		radius = 1.0f;
	}
	PrimitiveSphere::~PrimitiveSphere()
	{
	}
	void PrimitiveSphere::onUpdate()
	{
		renderer->sphereEdit(renderer->makeId("Sphere"), translation, radius);
	}

	void PrimitiveSphere::createTopoShape()
	{	
		BRepPrimAPI_MakeSphere mkSphere(
			radius
		);
		TopoDS_Shape ResultShape = mkSphere.Shape();
	
		auto topoActor = new Feature3D("SphereFeature", "Sphere");
		const auto& topoComp = topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();

		topo.setShape(ResultShape,false);

		Eigen::Matrix4f mat3 = Coord3(translation, Eigen::Matrix3f::Identity(), { 1,1,1 });
		Eigen::Matrix4f mat =  mat3;
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