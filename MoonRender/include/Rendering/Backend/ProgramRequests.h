#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Rendering/Data/ProgramDescriptorBindings.h>
#include <Rendering/Settings/EShaderType.h>

namespace Rendering::Backend
{
	/** One stage of a program being built: its type and the source it was given.
	 *
	 * The source is owned by the request (on the heap) - it is what the executor compiles, and the
	 * caller drops its own copy right after Link (filament's Program carries the same blobs). */
	struct ShaderSourceSpec
	{
		::Rendering::Settings::EShaderType type = ::Rendering::Settings::EShaderType::VERTEX;
		std::shared_ptr<const std::string> source;
	};

	/** Builds a shader program on the side that owns the context (filament's createProgram).
	 *
	 * Everything the build needs travels with the command: the stages' sources and the layout the
	 * program's binding map comes from. The executor creates the shaders, compiles and links them,
	 * and declares the descriptor slots to the program - which is why the binding map is generated
	 * where GL is, while the numbers themselves stay bookkeeping on the recording side.
	 */
	struct CreateProgramRequest
	{
		uint32_t programHandleId = 0;
		std::shared_ptr<const std::vector<ShaderSourceSpec>> stages;
		std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> bindings;
	};

	/** Declares the program's descriptor slots to GL (glUniformBlockBinding / glUniform1i), on the
	 * side that owns the context. Filament does this inside createProgram; ours can also happen
	 * lazily on first bind, because a material's layout may be configured after the link. */
	struct DeclareProgramBindingsRequest
	{
		uint32_t programHandleId = 0;
		std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> bindings;
	};
}