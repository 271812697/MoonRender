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

}