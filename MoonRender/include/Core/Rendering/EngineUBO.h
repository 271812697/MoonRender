#pragma once
#include <cstddef>
#include <cstdint>
#include <Maths/FMatrix4.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>
#include <Rendering/Settings/EBindingPoint.h>

namespace Core::Rendering
{
	/** The binding points themselves live next to the other engine blocks (see
	 * EBindingPoint.h); these aliases keep the names this header used before. */
	using ::Rendering::Settings::kEngineUBOBinding;
	using ::Rendering::Settings::kObjectUBOBinding;
	using ::Rendering::Settings::kReflectionUBOBinding;

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

	/** The C++ mirror of Common/Buffers/ReflectionUBO.ovfxh - the per-view reflection probe
	 * data that IBL.ovfxh reads (position/box for parallax correction and the IBL brightness).
	 *
	 * The engine binds this every frame with the defaults the IBL path expects (brightness 1,
	 * no box projection, not local) and a real ReflectionRenderFeature overrides it per probe.
	 * Without it the block is unbound: its brightness reads as zero and every Standard material
	 * loses its environment light. */
	struct ReflectionUBO
	{
		Maths::FVector4 position;
		Maths::FMatrix4 rotation;
		Maths::FVector4 boxCenter;
		Maths::FVector4 boxHalfExtents;
		float brightness;
		int boxProjection;
		int local;
		int padding; // std140 rounds the block up to 16 bytes; keep the C++ mirror the same size
	};

	static_assert(offsetof(ReflectionUBO, rotation) == 16, "vec4 then mat4, std140");
	static_assert(offsetof(ReflectionUBO, boxCenter) == 80, "std140 layout");
	static_assert(offsetof(ReflectionUBO, boxHalfExtents) == 96, "std140 layout");
	static_assert(offsetof(ReflectionUBO, brightness) == 112, "std140 layout");
	static_assert(offsetof(ReflectionUBO, boxProjection) == 116, "std140 layout");
	static_assert(offsetof(ReflectionUBO, local) == 120, "std140 layout");
	static_assert(sizeof(ReflectionUBO) == 128, "the block the shaders declare");
}
