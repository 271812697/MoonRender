#pragma once
#include <vector>
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 点 / 线手柄类的编辑控件。
	 *
	 * 搬自 ImRenderer 的 lineEdit / scaleEdit / planeEdit（两份）/ axisRotateEdit /
	 * dragPointInPlane / dragPointByAxis / drawTranslate2D / drawLineSplit，以及它们共用的
	 * drawRayHitScreenPoint。这些接口都以"用户点出一个点、拖一条线"为语义，返回 true 表示
	 * 本帧把值改动了。
	 *
	 * 3D 的那几个用相机平面上的射线拾取点；2D 的那两个用的是独立的一套 hot/active
	 * （makeHot2D / makeActive2D，仍然留在渲染器里）。参数名沿用原实现。 */
	class PointHandleGizmo
	{
	public:
		explicit PointHandleGizmo(ImRenderer& p_renderer);

		bool lineEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color);
		bool scaleEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color, int& index);
		bool planeEdit(unsigned int id, Eigen::Vector3f& _origin, Eigen::Vector3f& _normal);
		bool planeEdit(unsigned int id, std::vector<Eigen::Vector3f>& threePoints, int& index);
		bool axisRotateEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, float& angle);
		bool dragPointInPlane(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& normal);
		bool dragPointByAxis(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& axis);
		bool translate2D(unsigned int id, Eigen::Vector2f& pos);
		bool lineSplit(unsigned int id, Eigen::Vector2f& a, Eigen::Vector2f& b);
		/** 把鼠标射线的落点画成一个小点（等待用户点下一个点时的提示）。 */
		void drawRayHitScreenPoint();

	private:
		ImRenderer& m_renderer;
		GizmoDrag m_drag;
	};
}
