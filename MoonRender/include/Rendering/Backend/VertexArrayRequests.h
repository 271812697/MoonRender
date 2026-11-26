#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <Rendering/Settings/EDataType.h>

namespace Rendering::Backend
{
	/** One attribute of a vertex layout, as the executor needs it. */
	struct VertexLayoutAttribute
	{
		::Rendering::Settings::EDataType type = ::Rendering::Settings::EDataType::FLOAT;
		uint8_t count = 4;
		bool normalized = false;
		uint32_t stride = 0;
		uint64_t offset = 0;
	};

}
