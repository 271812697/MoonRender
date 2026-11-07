#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 球体编辑手柄：拖动球心平移，拖 6 个轴向点改半径。
	 * 搬自 ImRenderer::sphereEdit，参数名沿用原实现。 */
	class SphereEditGizmo
	{
	public:
		explicit SphereEditGizmo(ImRenderer& p_renderer);

		bool edit(unsigned int _id, Eigen::Vector3f& _translation_, float& r);

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
