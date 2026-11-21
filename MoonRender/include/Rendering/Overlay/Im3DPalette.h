#pragma once
#include <Eigen/Core>
#include <cstdint>

namespace MOON
{
	/** 3D 覆盖层的调色板。
	 *
	 * VertexFormat 的颜色按 ABGR 字节排（c[0] 是 alpha），与 Gizmo* shader 里的
	 * `aColor.abgr` 换序对应：四个分量依次是 alpha、blue、green、red。
	 * 写成 header 里的 inline 变量而不是每个 .cpp 各来一份 static，调色板只有一处定义。 */
	inline const Eigen::Vector4<uint8_t> Color_Gold = Eigen::Vector4<uint8_t>(255, 0, 215, 255);
	inline const Eigen::Vector4<uint8_t> Color_Red = Eigen::Vector4<uint8_t>(255, 0, 0, 255);
	inline const Eigen::Vector4<uint8_t> Color_Green = Eigen::Vector4<uint8_t>(255, 0, 255, 0);
	inline const Eigen::Vector4<uint8_t> Color_Blue = Eigen::Vector4<uint8_t>(255, 255, 0, 0);
	inline const Eigen::Vector4<uint8_t> Color_Brown = Eigen::Vector4<uint8_t>(255, 19, 69, 139);
	inline const Eigen::Vector4<uint8_t> Color_White = Eigen::Vector4<uint8_t>(255, 255, 255, 255);
	inline const Eigen::Vector4<uint8_t> Color_GB = Eigen::Vector4<uint8_t>(255, 255, 255, 0);
	inline const Eigen::Vector4<uint8_t> Color_AR = Eigen::Vector4<uint8_t>(255, 115, 115, 76);
	inline const Eigen::Vector4<uint8_t> Color_ARB = Eigen::Vector4<uint8_t>(255, 232, 162, 0);
}
