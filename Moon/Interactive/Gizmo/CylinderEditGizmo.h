#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 圆柱 / 圆台的编辑手柄：中心平移、上下两圈共 8 个点改半径、两个箭头改高度、
	 * 端点球改轴向。
	 * 搬自 ImRenderer::clydinerEdit，参数名沿用原实现。 */
	class CylinderEditGizmo
	{
	public:
		explicit CylinderEditGizmo(ImRenderer& p_renderer);

		/** same 为真时上下半径联动。 */
		bool edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& height,
			float& _tr, float& _br, bool same = true);	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
