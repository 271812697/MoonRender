#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 剖切平面的手柄：球心平移、圆环沿轴平移、4 个点改平面尺寸、双锥改法线方向。
	 * 搬自 ImRenderer::planeClip，参数名沿用原实现。 */
	class PlaneClipGizmo
	{
	public:
		explicit PlaneClipGizmo(ImRenderer& p_renderer);

		/** opeartorNormal 允许在双锥上拖动改法线，drawCircleFlag 控制是否画半透明圆面。 */
		bool edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& _scale,
			const Eigen::Vector3f& minBox, const Eigen::Vector3f& maxBox, bool opeartorNormal, bool drawCircleFlag);

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
