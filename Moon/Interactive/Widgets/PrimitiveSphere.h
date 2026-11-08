#pragma once
#include "Interactive/Widgets/PrimitiveShape.h"
#include "Interactive/Gizmo/SphereEditGizmo.h"
#include <Eigen/Core>
namespace MOON
{
	class PrimitiveSphere : public PrimitiveShape
	{
	public:
		PrimitiveSphere(const std::string& name);
		virtual ~PrimitiveSphere()override;
		virtual void onUpdate()override;
		virtual void createTopoShape()override;
	private:
		Eigen::Vector3f translation;
		float radius;
		/** 球体手柄：拖拽状态与绘制都在操纵器里，widget 只管把值交给它。 */
		SphereEditGizmo m_edit;
	};
}