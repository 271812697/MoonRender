#pragma once
#include <vector>
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 包围盒编辑手柄：中心点平移、6 个面心缩放、外框旋转。
	 * 搬自 ImRenderer::boxEdit 的两个重载。参数名沿用原实现。 */
	class BoxEditGizmo
	{
	public:
		explicit BoxEditGizmo(ImRenderer& p_renderer);

		bool edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_,
			Eigen::Vector3f& _scale_);
		/** 带开关的版本：平移 / 旋转 / 面拖动各自可禁用，mode 回填这次操作的类型
		 * （0 = 平移，1 = 面拖动，2 = 旋转）。 */
		bool edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_,
			Eigen::Vector3f& _scale_, bool translationFlag, bool rotateFlag, bool faceMeFlag, int& mode);

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
