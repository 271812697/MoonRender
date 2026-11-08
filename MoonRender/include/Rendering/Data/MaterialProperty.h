#pragma once
#include <variant>

#include <Maths/FMatrix3.h>
#include <Maths/FMatrix4.h>
#include <Maths/FVector2.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>
#include <Rendering/HAL/TextureHandle.h>

namespace Rendering::Resources
{
	class Texture;
}

/** The value a material parameter carries. This is a value type, not a container: an instance
 * writes declared parameters into the MaterialDefinition's uniform block (or its descriptor)
 * and keeps only the loose uniforms of a source-built shader in the descriptor set. */
namespace Rendering::Data
{
	using MaterialPropertyType = std::variant<
		std::monostate,
		bool,
		int,
		float,
		Maths::FVector2,
		Maths::FVector3,
		Maths::FVector4,
		Maths::FMatrix3,
		Maths::FMatrix4,
		Rendering::HAL::TextureHandle*,
		Rendering::Resources::Texture*
	>;

	struct MaterialProperty
	{
		MaterialPropertyType value;
	};
}
