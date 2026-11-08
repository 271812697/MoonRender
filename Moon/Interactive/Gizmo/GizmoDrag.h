#pragma once
#include <Eigen/Core>
#include "Interactive/Im3DType.h"

namespace MOON
{
	class ImRenderer;

	/** 一次拖拽的记忆 + 拖拽原语。
	 *
	 * 这些状态原来是 ImRenderer 的私有成员（gizmoStateVec3 / gizmoVec2 / gizmoCursor /
	 * gizmoNormal / gizmoStateFloat / gizmoStateMat3），被所有操纵器共用：谁在拖、从哪儿
	 * 开始拖、按下那一刻的光标与参考方向都记在那里。现在每个操纵器各持一份，于是两个同类
	 * 手柄互不干扰；而 hot / active 的唯一性仍然由 ImRenderer 仲裁 —— 全局只有一个激活
	 * 手柄这件事属于绘制框架，不属于某个操纵器。
	 *
	 * 每个原语只做两件事：射线求交 + hot/active 状态转移。命中并正在拖拽时返回 true 并把
	 * 位移/角度写回 _out_，其余情况返回 false。参数名沿用原实现。 */
	class GizmoDrag
	{
	public:
		/** 一个固定在原点的小球，在平面上拖动。 */
		bool spherePlaneTranslation(ImRenderer& p_renderer, unsigned _id, const Eigen::Vector3f& _origin,
			float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		/** 一个圆环，沿自身轴拖动。 */
		bool circleAxisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		/** 一个小球，沿给定轴（_normal）拖动。 */
		bool sphereAxisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		/** 让一个点沿以 _origin 为心的圆运动（圆所在平面的法线是 axis）。 */
		bool sphereRotateInCircle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			float _radius, const Eigen::Vector3f& axis, Eigen::Vector3f* _out_, bool inPlane = true);
		/** 拖拽线性手柄，用鼠标位移转出新的方向。 */
		bool operateNormal(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			const Eigen::Vector3f& _end, float _worldSize, Eigen::Vector3f* _out_);
		/** 沿轴平移：把射线与轴的最近点差值写回。 */
		bool axisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			const Eigen::Vector3f& _axis, float _snap, float _worldHeight, float _worldSize,
			Eigen::Vector3f* _out_);
		/** 平面内平移：把射线与平面的交点差值写回。 */
		bool planeTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			const Eigen::Vector3f& _normal, float _snap, float _worldSize, Eigen::Vector3f* _out_);
		/** 绕轴旋转：把射线在视平面上的投影夹角写回。 */
		bool axisAngle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin,
			const Eigen::Vector3f& _axis, float _snap, float _worldRadius, float _worldSize, float* _out_);

		/** 拖拽记忆：各操纵器在需要时直接读写自己那一份。注释是它对应的旧成员。 */
		Eigen::Vector3f& position() { return m_position; }             // gizmoStateVec3
		Eigen::Vector2f& offset() { return m_offset; }                 // gizmoVec2
		Eigen::Vector2f& cursor() { return m_cursor; }                 // gizmoCursor
		Eigen::Vector3f& normal() { return m_normal; }                 // gizmoNormal
		Eigen::Vector3f& startDirection() { return m_startDirection; } // gizmoStateStartAngle
		float& angle() { return m_angle; }                             // gizmoStateFloat
		Eigen::Matrix3f& basis() { return m_basis; }                   // gizmoStateMat3

	private:
		Eigen::Vector3f m_position = Eigen::Vector3f::Zero();
		Eigen::Vector2f m_offset = Eigen::Vector2f::Zero();
		Eigen::Vector2f m_cursor = Eigen::Vector2f::Zero();
		Eigen::Vector3f m_normal = Eigen::Vector3f::Zero();
		Eigen::Vector3f m_startDirection = Eigen::Vector3f::Zero();
		float m_angle = 0.0f;
		Eigen::Matrix3f m_basis = Eigen::Matrix3f::Identity();
	};
}
