#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON
{
	class ImRenderer;

	/** 变换操纵器：三轴平移 / 三个平面块 / 绕轴旋转。
	 *
	 * 以前是 ImRenderer::translation / rotation / drawManpulate 加一族 gizmo*Draw/Behavior。
	 * 现在它自己持有拖拽记忆和"世界/局部""平移/旋转"这两份交互模式，渲染器只提供画图、
	 * 射线数据和 hot/active 仲裁。
	 *
	 * 模式键（T / R / S / L）不再由 ImRenderer::newFrame 处理：帧内的按键边沿状态由渲染器
	 * 维护（wasKeyPressed），本类在每次进入交互前同步一次，语义与原来一致。
	 * 参数名沿用原实现。 */
	class TransformGizmo
	{
	public:
		explicit TransformGizmo(ImRenderer& p_renderer);

		/** 世界坐标系还是物体坐标系；与 Ctrl+L 等价。 */
		void setLocal(bool p_local) { m_local = p_local; }
		bool isLocal() const { return m_local; }
		GizmoMode mode() const { return m_mode; }
		void setMode(GizmoMode p_mode) { m_mode = p_mode; }

		bool translation(unsigned int _id, Eigen::Vector3f& _translation_, bool _local = false);
		bool rotation(unsigned int _id, Eigen::Matrix3f& _rotation_, bool _local = false);
		/** 平移 + 旋转的组合入口（原来的 drawManpulate）。 */
		bool manipulate(unsigned _id, Eigen::Matrix4f& model);
		bool manipulate(const char* _id, Eigen::Matrix4f& model);

	private:
		/** 读 T / R / S / L 的按键边沿，更新自己的模式。 */
		void syncModeFromKeys();

		ImRenderer& m_renderer;
		GizmoDrag m_drag;
		GizmoMode m_mode = GizmoModeTranslation;
		bool m_local = false;
	};
}
