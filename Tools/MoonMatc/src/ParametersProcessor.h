#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "MaterialBuilder.h"

/** filament-matp's ParametersProcessor: the processor for the `material { }` block *itself*.
 *
 * The block is a list of properties (`name`, `shadingModel`, `features`, `passes`, `parameters`,
 * ...). Each property has its own handler in mConfig; `parameters : [ { ... } ]` is only one of
 * them, and its entries are read field by field through the second table, mParameters.
 *
 * A key that is in neither table is a render-state entry (blending / culling / depthWrite / ...)
 * and lands in MaterialBuilder::state, which is what the package's state chunk carries. */
namespace Moon::Matc
{
	class ParametersProcessor
	{
	public:
		/** One property of the material block: the key and the text after the colon. */
		using PropertyProcessor = void (ParametersProcessor::*)(const std::string& p_key, const std::string& p_value);

		/** One field of an entry in `parameters : [ { ... } ]`. */
		using FieldProcessor = void (*)(Parameter& p_parameter, const std::string& p_value);

		/** Reads the body of `material { ... }` and writes it into p_builder. */
		void ProcessBlock(const std::string& p_body, MaterialBuilder& p_builder);

		/** mConfig: every property the block may declare. */
		static const std::map<std::string, PropertyProcessor>& Properties();

		/** mParameters: every field a parameter entry may carry. */
		static const std::vector<std::pair<const char*, FieldProcessor>>& Fields();

	private:
		// --- mConfig handlers ------------------------------------------------------------
		void OnName(const std::string& p_key, const std::string& p_value);
		void OnShadingModel(const std::string& p_key, const std::string& p_value);
		void OnDomain(const std::string& p_key, const std::string& p_value);
		/** The two material switches that change the generated code rather than the state. */
		void OnCodeFlag(const std::string& p_key, const std::string& p_value);
		/** `parameters : [ { ... } ]` - the per-entry fields come from Fields(). */
		void OnParameters(const std::string& p_key, const std::string& p_value);
		/** The name-list properties: features / defaultFeatures / variantFilter /
		 * vertexAttributes / passes. */
		void OnNameList(const std::string& p_key, const std::string& p_value);

		/** Applies one property table entry; a key in neither table becomes a state entry. */
		void ApplyProperty(const std::string& p_key, const std::string& p_value);

		/** Reads one `{ ... }` object of the parameters array. */
		void ProcessParameterObject(const std::string& p_object) const;

		/** The value of `field` inside p_object, or empty when the object does not carry it. */
		static std::string ValueOf(const std::string& p_object, const std::string& p_field);

		MaterialBuilder* m_builder = nullptr;
	};
}
