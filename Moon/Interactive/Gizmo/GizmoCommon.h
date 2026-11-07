#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"

namespace MOON
{
	class ImRenderer;
	class GizmoDrag;
}

namespace MOON::Gizmo
{
	/** 操纵器手柄的屏幕像素尺寸：骨架高度、线宽与点径。
	 *
	 * 这两个值是所有手柄共用的观感常量，以前是 ImRenderer 的私有成员
	 * （gizmoHeightPixels / gizmoSizePixels），没有任何 setter。 */
	inline constexpr float kHandleHeightPixels = 64.0f;
	inline constexpr float kHandleLinePixels = 5.0f;

	/** 一条会被 hot/active 着色的操纵器轴：加粗箭头 + 激活时贯穿全场的辅助线。
	 * 取代原来的 ImRenderer::drawArrow(unsigned, origin, axis, worldHeight, color)。
	 * 参数名沿用原实现，方便逐行对照 review。 */
	void DrawAxisArrow(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
		const Eigen::Vector3f& _axis, float _worldHeight, Eigen::Vector4<uint8_t> _color);

	/** 把鼠标射线的落点画成一个小点 —— 等待用户点下一个点时的提示。
	 * 点/线手柄与曲线手柄都要用，所以放在公共绘制助手里。 */
	void DrawRayHitScreenPoint(ImRenderer& p_renderer);

	/** 平移手柄的轴：箭头，正对相机时淡出。 */
	void DrawAxisTranslationHandle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
		const Eigen::Vector3f& _axis, float _worldHeight, float _worldSize, Eigen::Vector4<uint8_t> _color);

	/** 平移手柄的平面块：半透明填充 + 边框。 */
	void DrawPlaneTranslationHandle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
		const Eigen::Vector3f& _normal, float _worldSize, Eigen::Vector4<uint8_t> _color);

	/** 旋转手柄的圆环：激活时画指针箭头与圆心标记。 */
	void DrawRotationRing(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
		const Eigen::Vector3f& _axis, float _worldRadius, Eigen::Vector4<uint8_t> _color);

	/** 旋转手柄的扇形圆环，背光侧的 alpha 会被压暗。
	 * p_drag 提供开始拖拽的方向与角度（原来的 gizmoStateVec3 / gizmoStateFloat）。 */
	void DrawAngleRing(ImRenderer& p_renderer, GizmoDrag& p_drag, unsigned int _id, const Eigen::Vector3f& _origin,
		const Eigen::Vector3f& _axis, float _worldRadius, float _angle, Eigen::Vector4<uint8_t> _color,
		float _minAlpha);
}
