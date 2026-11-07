#pragma once
#include <vector>
#include <utility>
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 曲线编辑手柄：在一张跟随相机的平面上点出一串点，并可逐点拖动。
	 * 搬自 ImRenderer::CurveEdit 的两个重载（纯点集 / 点 + 切线），参数名沿用原实现。 */
	class CurveEditGizmo
	{
	public:
		explicit CurveEditGizmo(ImRenderer& p_renderer);

		bool edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin,
			std::vector<Eigen::Vector3f>& polygon);
		bool edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin,
			std::vector<std::pair<Eigen::Vector3f, Eigen::Vector3f>>& polygon);

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
