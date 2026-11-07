#pragma once
#include "Interactive/Widgets/PrimitiveShape.h"
#include "Interactive/Gizmo/CylinderEditGizmo.h"
#include <Eigen/Core>

namespace MOON
{
	class PrimitiveCone : public PrimitiveShape
	{
	public:
		PrimitiveCone(const std::string& name);
		virtual ~PrimitiveCone()override;
		virtual void onUpdate()override;
		virtual void createTopoShape()override;
	private:
		Eigen::Vector3f translation;
		Eigen::Vector3f normal;
		float height;
		float radiusTop;
		float radiusBottom;
		/** 圆台/圆锥手柄：与圆柱共用同一套操纵器（上下半径不联动）。 */
		CylinderEditGizmo m_edit;
	};
}