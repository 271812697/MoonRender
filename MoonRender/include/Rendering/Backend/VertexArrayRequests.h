#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <Rendering/Settings/EDataType.h>
#include <Rendering/Settings/VertexAttribute.h>

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

	/** How many bytes one component of p_type takes. */
	inline uint32_t VertexDataTypeSize(::Rendering::Settings::EDataType p_type)
	{
		switch (p_type)
		{
		case ::Rendering::Settings::EDataType::BYTE:
		case ::Rendering::Settings::EDataType::UNSIGNED_BYTE:
			return 1;
		case ::Rendering::Settings::EDataType::SHORT:
		case ::Rendering::Settings::EDataType::UNSIGNED_SHORT:
			return 2;
		case ::Rendering::Settings::EDataType::INT:
		case ::Rendering::Settings::EDataType::UNSIGNED_INT:
		case ::Rendering::Settings::EDataType::FLOAT:
			return 4;
		case ::Rendering::Settings::EDataType::DOUBLE:
			return 8;
		default:
			return 0;
		}
	}

	/** Turns a vertex layout description into the shape the executor reads it in: one entry per
	 * attribute, with the stride and the offset already resolved.
	 *
	 * The recording side knows these numbers - the layout is the mesh's, built when the mesh is
	 * created - and the executor only feeds them to glVertexAttribPointer. Both the mesh's
	 * VertexArrayObject and the 2D overlay's own array come through here, so the arithmetic cannot
	 * drift between them (a hand-rolled copy once assumed four bytes per component, and the overlay's
	 * colours were then read from the offset of the next vertex). */
	inline std::shared_ptr<const std::vector<VertexLayoutAttribute>> BuildVertexLayout(
		::Rendering::Settings::VertexAttributeLayout p_attributes)
	{
		auto attributes = std::make_shared<std::vector<VertexLayoutAttribute>>();
		attributes->reserve(p_attributes.size());

		uint32_t stride = 0;
		for (const ::Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			stride += VertexDataTypeSize(attribute.type) * attribute.count;
		}

		uint64_t offset = 0;
		for (const ::Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			VertexLayoutAttribute entry;
			entry.type = attribute.type;
			entry.count = attribute.count;
			entry.normalized = attribute.normalized;
			entry.stride = stride;
			entry.offset = offset;
			attributes->push_back(entry);
			offset += VertexDataTypeSize(attribute.type) * attribute.count;
		}
		return attributes;
	}
}
