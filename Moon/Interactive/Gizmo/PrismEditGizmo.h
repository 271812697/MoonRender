#pragma once
#include <vector>
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 棱柱的编辑手柄：在平面上点出多边形，再沿法线拖出高度。
	 * 搬自 ImRenderer::PrismEdit，参数名沿用原实现。 */
	class PrismEditGizmo
	{
	public:
		explicit PrismEditGizmo(ImRenderer& p_renderer);

		/** polygon2D 是平面上点的二维坐标，polygon 由它算出的世界坐标回填。 */
		bool edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, float* _height,
			std::vector<Eigen::Vector3f>& polygon, std::vector<Eigen::Vector2f>& polygon2D, bool flip = false);

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
