#pragma once
#include <cstddef>
#include <Maths/FMatrix4.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>

namespace Core::Rendering
{
	/** The C++ mirror of Common/Buffers/EngineUBO.ovfxh.
	 *
	 * The layout has to match std140 exactly, because the shaders read this block by offset:
	 *  - `vec3` / `int` / `float` pack the way the members below do (ubo_ViewPos's third
	 *    component is followed immediately by ubo_CameraType, exactly as std140 places it),
	 *  - every matrix is 64 bytes and self-aligned.
	 * A field added here without the .ovfxh counterpart (or the other way round) silently
	 * shifts every uniform after it - the static_asserts below pin the offsets that matter.
	 *
	 * Written per frame by EngineBufferRenderFeature from the view's camera. */
	struct EngineUBO
	{
		Maths::FMatrix4 ubo_Model;
		Maths::FMatrix4 ubo_View;
		Maths::FMatrix4 ubo_Projection;
		Maths::FVector3 ubo_ViewPos;
		int             ubo_CameraType;      // 0 orthographic, 1 perspective
		float           ubo_Time;
		int             ubo_ScreenWidth;
		int             ubo_ScreenHeight;
		int             ubo_enableClip;
		Maths::FVector4 ubo_plane;
		Maths::FMatrix4 ubo_MirroPlaneMatrix;
		Maths::FMatrix4 ubo_UserMatrix;
	};

	static_assert(offsetof(EngineUBO, ubo_View) == 64, "std140: matrices are 64 bytes");
	static_assert(offsetof(EngineUBO, ubo_Projection) == 128, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_ViewPos) == 192, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_CameraType) == 204, "vec3 then int, std140");
	static_assert(offsetof(EngineUBO, ubo_Time) == 208, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_plane) == 224, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_MirroPlaneMatrix) == 240, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_UserMatrix) == 304, "std140 layout");
	static_assert(sizeof(EngineUBO) == 368, "the block the shaders declare");
}
