#pragma once
#include <cstddef>
#include <cstdint>
#include <Maths/FMatrix4.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>

namespace Core::Rendering
{
	/** GL uniform-buffer binding points of the engine's per-view and per-renderable blocks.
	 * They mirror filament's descriptor sets: 0 = per view, 1 = per renderable (the reflection
	 * block sits on 1 in GL and the per-material block on 2, so the renderable one moved to 3 -
	 * GL has one binding-point namespace, filament's sets do not). */
	constexpr uint32_t kEngineUBOBinding = 0;
	constexpr uint32_t kObjectUBOBinding = 3;

	/** The C++ mirror of Common/Buffers/EngineUBO.ovfxh's per-view block.
	 *
	 * The layout has to match std140 exactly, because the shaders read this block by offset:
	 *  - `vec3` / `int` / `float` pack the way the members below do (ubo_ViewPos's third
	 *    component is followed immediately by ubo_CameraType, exactly as std140 places it),
	 *  - every matrix is 64 bytes and self-aligned.
	 * A field added here without the .ovfxh counterpart (or the other way round) silently
	 * shifts every uniform after it - the static_asserts below pin the offsets that matter.
	 *
	 * Written per view by EngineBufferRenderFeature. What changes per drawable (the model and
	 * user matrices) lives in ObjectUBO. */
	struct EngineUBO
	{
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
	};

	static_assert(offsetof(EngineUBO, ubo_Projection) == 64, "std140: matrices are 64 bytes");
	static_assert(offsetof(EngineUBO, ubo_ViewPos) == 128, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_CameraType) == 140, "vec3 then int, std140");
	static_assert(offsetof(EngineUBO, ubo_Time) == 144, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_ScreenWidth) == 148, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_ScreenHeight) == 152, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_enableClip) == 156, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_plane) == 160, "std140 layout");
	static_assert(offsetof(EngineUBO, ubo_MirroPlaneMatrix) == 176, "std140 layout");
	static_assert(sizeof(EngineUBO) == 240, "the block the shaders declare");

	/** The C++ mirror of the per-renderable block (filament's set 1): the two matrices that
	 * change between drawables, packed for the ObjectUBO block in the same .ovfxh. */
	struct ObjectUBO
	{
		Maths::FMatrix4 ubo_Model;
		Maths::FMatrix4 ubo_UserMatrix;
	};

	static_assert(offsetof(ObjectUBO, ubo_UserMatrix) == 64, "std140 layout");
	static_assert(sizeof(ObjectUBO) == 128, "the block the shaders declare");
}
