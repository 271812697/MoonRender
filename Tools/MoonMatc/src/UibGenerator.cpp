// The uniform side of the PerMaterial layout: see UibGenerator.h. Split out of Matc.cpp /
// ShaderGenerator.cpp - the type mapping, the std140 walk, the engine globals and the block
// declaration live here.

#include "UibGenerator.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "SibGenerator.h"
#include "TextUtils.h"

namespace Moon::Matc
{
	/** The GLSL name of a parameter type. The material files spell them the way filament's
	 * DSL does (float4, int3, ...); GLSL calls the same things vec4, ivec3, and sampling one
	 * a name wrong is a compile error rather than a silent default. */
	std::string GlslType(const std::string& p_type)
	{
		static const std::map<std::string, std::string> kTypes = {
			{ "bool", "bool" }, { "bool2", "bvec2" }, { "bool3", "bvec3" }, { "bool4", "bvec4" },
			{ "int", "int" }, { "int2", "ivec2" }, { "int3", "ivec3" }, { "int4", "ivec4" },
			{ "uint", "uint" }, { "uint2", "uvec2" }, { "uint3", "uvec3" }, { "uint4", "uvec4" },
			{ "float", "float" }, { "float2", "vec2" }, { "float3", "vec3" }, { "float4", "vec4" },
			{ "mat2", "mat2" }, { "mat3", "mat3" }, { "mat4", "mat4" },
			{ "sampler2D", "sampler2D" }, { "sampler2DArray", "sampler2DArray" },
			{ "samplerCube", "samplerCube" }, { "sampler3D", "sampler3D" },
		};
		const auto it = kTypes.find(p_type);
		return it != kTypes.end() ? it->second : p_type;
	}

	/** The std140 size and base alignment of a parameter type. Samplers never reach this:
	 * they are not allowed inside a uniform block and stay separate uniforms. */
	struct TypeLayout
	{
		uint32_t size;
		uint32_t alignment;
	};

	TypeLayout ParameterTypeLayout(const std::string& p_type)
	{
		static const std::map<std::string, TypeLayout> kTypes = {
			{ "bool", { 4, 4 } }, { "bool2", { 8, 8 } }, { "bool3", { 12, 16 } }, { "bool4", { 16, 16 } },
			{ "int", { 4, 4 } }, { "int2", { 8, 8 } }, { "int3", { 12, 16 } }, { "int4", { 16, 16 } },
			{ "uint", { 4, 4 } }, { "uint2", { 8, 8 } }, { "uint3", { 12, 16 } }, { "uint4", { 16, 16 } },
			{ "float", { 4, 4 } }, { "float2", { 8, 8 } }, { "float3", { 12, 16 } }, { "float4", { 16, 16 } },
			{ "mat2", { 32, 8 } }, { "mat3", { 48, 16 } }, { "mat4", { 64, 16 } },
		};
		const auto it = kTypes.find(p_type);
		return it != kTypes.end() ? it->second : TypeLayout{ 4, 4 };
	}

	uint32_t RoundUp(uint32_t p_value, uint32_t p_alignment)
	{
		return (p_value + p_alignment - 1) / p_alignment * p_alignment;
	}

	/** Assigns the std140 offsets of the parameter block and the texture units of the samplers.
	 *
	 * The block holds every non-sampler parameter in declaration order (engine-owned `ui:false`
	 * ones included: they are still uniforms). The generated GLSL declares the same block with
	 * `binding = 2`, so the driver's std140 layout has to agree with this computation; the
	 * runtime validates the offsets once when it links a program (see ShaderLoader). */
	void LayoutParameters(MaterialFile& p_material)
	{
		uint32_t offset = 0;
		uint32_t samplerBinding = 0;

		for (Parameter& parameter : p_material.parameters)
		{
			if (IsSamplerType(parameter.type))
			{
				parameter.binding = samplerBinding++;
				continue;
			}

			const TypeLayout layout = ParameterTypeLayout(parameter.type);
			const uint32_t count = parameter.count.empty()
				? 1u : static_cast<uint32_t>(std::stoul(parameter.count));

			if (count > 1)
			{
				// std140 arrays round the element size (and therefore the stride) up to 16.
				offset = RoundUp(offset, 16);
				parameter.offset = offset;
				parameter.stride = RoundUp(layout.size, 16);
				offset += parameter.stride * count;
			}
			else
			{
				offset = RoundUp(offset, layout.alignment);
				parameter.offset = offset;
				parameter.stride = layout.size;
				offset += layout.size;
			}
		}

		p_material.parameterBlockSize = RoundUp(offset, 16);
	}

	/** The resources the lit surface prelude reads, added to the material's parameter table.
	 *
	 * filament keeps these outside the material: they are frame/per-view data, declared in the
	 * surface prelude from the engine's own interface blocks. Here the uniform machinery is the
	 * material parameter block plus one texture unit per sampler, so the cheapest way to give a
	 * lit material working declarations (and a runtime that uploads them by name) is to put them
	 * in the same table, marked `ui : false` so the designer never shows them.
	 *
	 * Names are the ones the renderer already sets: the sky pass's IBL cubes, the DFG LUT, the
	 * shadow pass's map and matrix. A material that declares one of them itself keeps its own
	 * declaration - the injection only fills the gap. */
	void InjectEngineGlobals(MaterialFile& p_material)
	{
		if (p_material.shadingModel == "unlit" || p_material.domain != "surface")
		{
			return;
		}

		struct Entry
		{
			const char* name;
			const char* type;
		};
		static constexpr Entry kEntries[] = {
			{ "_IrradianceCube",   "samplerCube" },
			{ "_PrefilterCube",    "samplerCube" },
			{ "_BRDFLut",          "sampler2D" },
			{ "_ShadowMap",        "sampler2D" },
			{ "_LightSpaceMatrix", "mat4" },
		};

		for (const Entry& entry : kEntries)
		{
			const bool declared = std::ranges::any_of(p_material.parameters,
				[&entry](const Parameter& p_parameter) { return p_parameter.name == entry.name; });
			if (declared)
			{
				continue;
			}

			Parameter parameter;
			parameter.name = entry.name;
			parameter.type = entry.type;
			parameter.ui = false;
			p_material.parameters.push_back(std::move(parameter));
		}
	}

	/** The material's parameters, as the descriptors the entry point reads.
	 *
	 * The non-sampler parameters are one std140 uniform block - the engine's per-material
	 * descriptor: a single UBO at binding point 2, which a material instance fills by offset.
	 * Samplers cannot be block members, so they stay separate uniforms with a fixed texture unit
	 * each (`layout(binding = N)`), the same idea applied to the sampler descriptors. Declared
	 * for both stages - a parameter only one of them reads is dropped by the compiler.
	 *
	 * The declared initialisers move to the runtime: a block member cannot have one, and the
	 * material's property defaults (parsed from the same declarations, see SHDRPROP) are what
	 * fills the buffer. */
	std::string ParametersPrelude(const std::vector<Parameter>& p_parameters, uint32_t p_blockSize)
	{
		std::ostringstream out;

		if (p_blockSize > 0)
		{
			out << "layout(std140, binding = 2) uniform MaterialParams\n{\n";
			for (const Parameter& parameter : p_parameters)
			{
				if (IsSamplerType(parameter.type))
				{
					continue;
				}
				out << "    " << GlslType(parameter.type) << " " << parameter.name;
				if (!parameter.count.empty())
				{
					out << "[" << parameter.count << "]";
				}
				out << ";\n";
			}
			out << "};\n";
		}

		for (const Parameter& parameter : p_parameters)
		{
			if (!IsSamplerType(parameter.type))
			{
				continue;
			}
			out << "layout(binding = " << parameter.binding << ") uniform "
				<< GlslType(parameter.type) << " " << parameter.name << ";\n";
		}

		out << "\n";
		return out.str();
	}

	UboBlockInfo BuildUniformBlock(const MaterialBuilder& p_material)
	{
		UboBlockInfo block;
		block.size = p_material.parameterBlockSize;
		for (const Parameter& parameter : p_material.parameters)
		{
			if (IsSamplerType(parameter.type)) { continue; }

			UniformInfo uniform;
			uniform.name = parameter.name;
			uniform.type = GlslType(parameter.type);
			uniform.offset = parameter.offset;
			uniform.stride = parameter.stride;
			uniform.count = parameter.count.empty()
				? 1u : static_cast<uint32_t>(std::stoul(parameter.count));
			block.uniforms.push_back(std::move(uniform));
		}
		return block;
	}
}
