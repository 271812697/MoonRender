#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "JsonishParser.h"
#include "MaterialBuilder.h"

/** filament-matp's ParametersProcessor: the processor for the `material { }` block *itself*.
 *
 * The block has already been parsed into a JsonishObject (see MaterialParser / JsonishParser);
 * this class only walks the object. Each property has its own handler in mConfig;
 * `parameters : [ { ... } ]` is one of them, and its entries are read field by field from the
 * JsonishObject through the second table, mFields.
 *
 * A key that is in neither table is a render-state entry (blending / culling / depthWrite /
 * ...) and lands in MaterialBuilder::state, which is what the package's state chunk carries. */
namespace Moon::Matc
{
	class ParametersProcessor
	{
	public:
		/** One property of the material block: the key and its parsed value. */
		using PropertyProcessor = void (ParametersProcessor::*)(const std::string& p_key, const JsonishValue& p_value);

		/** One field of an entry in `parameters : [ { ... } ]`. */
		using FieldProcessor = void (*)(Parameter& p_parameter, const JsonishValue& p_value);

		/** Reads the body of `material { ... }` and writes it into p_builder. */
		void ProcessBlock(const JsonishObject& p_object, MaterialBuilder& p_builder);

		/** mConfig: every property the block may declare. */
		static const std::map<std::string, PropertyProcessor>& Properties();

		/** mFields: every field a parameter entry may carry. */
		static const std::vector<std::pair<const char*, FieldProcessor>>& Fields();

	private:
		// --- mConfig handlers ------------------------------------------------------------
		void OnName(const std::string& p_key, const JsonishValue& p_value);
		void OnShadingModel(const std::string& p_key, const JsonishValue& p_value);
		void OnDomain(const std::string& p_key, const JsonishValue& p_value);
		/** The two material switches that change the generated code rather than the state. */
		void OnCodeFlag(const std::string& p_key, const JsonishValue& p_value);
		/** `parameters : [ { ... } ]`: the array handler that dispatches each object. */
		void OnParameters(const std::string& p_key, const JsonishValue& p_value);
		/** One `{ ... }` entry of the parameters array. */
		void OnParameters(const JsonishObject& p_parameter);
		/** The name-list properties: features / defaultFeatures / variantFilter /
		 * vertexAttributes / passes. */
		void OnNameList(const std::string& p_key, const JsonishValue& p_value);

		/** Applies one property table entry; a key in neither table becomes a state entry. */
		void ApplyProperty(const std::string& p_key, const JsonishValue& p_value);

		MaterialBuilder* m_builder = nullptr;
	};
}
