#include <algorithm>
#include <format>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <vector>
#include <Rendering/Resources/Shader.h>

namespace
{
	void ValidateVariants(const Rendering::Resources::Shader::Variants& p_variants)
	{
	}
}

Rendering::HAL::ShaderProgram& Rendering::Resources::Shader::GetVariant(std::optional<const std::string_view> p_pass, const Data::FeatureSet& p_featureSet)
{
	const std::string pass = std::string{ p_pass.value_or("") };

	// Never dereference a missing entry: a fixed-feature package has no empty-set fallback, and
	// a request for a set the package was not compiled with used to end in a null program.
	auto passIt = m_variants.find(pass);
	if (passIt == m_variants.end() || passIt->second.empty())
	{
		passIt = m_variants.begin();
	}
	if (passIt == m_variants.end() || passIt->second.empty())
	{
		throw std::runtime_error("shader has no variant");
	}

	auto& variants = passIt->second;
	auto it = variants.find(p_featureSet);
	if (it == variants.end())
	{
		// No exact variant. Falling back to "any" (the first one, historically) is how a material
		// that asked for lighting could end up on a variant compiled without it: the CAD faces
		// rendered dark because the pick landed on the empty set. Take the variant that shares the
		// most features with the request instead, preferring the one with the fewest extras.
		size_t bestCommon = 0;
		size_t bestExtra = static_cast<size_t>(-1);
		for (auto candidate = variants.begin(); candidate != variants.end(); ++candidate)
		{
			size_t common = 0;
			for (const std::string& feature : candidate->first)
			{
				if (p_featureSet.contains(feature)) { ++common; }
			}
			const size_t extra = candidate->first.size() - common;
			if (common > bestCommon || (common == bestCommon && extra < bestExtra))
			{
				bestCommon = common;
				bestExtra = extra;
				it = candidate;
			}
		}

		// Keyed by the request, not just the shader: the material preview renders first and a
		// per-shader key would silence the scene's own miss behind it.
		static std::set<std::string> reported;
		std::string requestKey = path + "|" + pass;
		for (const std::string& feature : p_featureSet) { requestKey += "|" + feature; }
		if (reported.insert(std::move(requestKey)).second)
		{
			std::cout << "[Shader] " << path << " has no variant for pass " << pass
				<< " with " << p_featureSet.size() << " feature(s); using the closest one ("
				<< bestCommon << " match(es), " << bestExtra << " extra)." << std::endl;
			std::cout << "[Shader]   wanted:";
			for (const std::string& feature : p_featureSet) { std::cout << " " << feature; }
			std::cout << std::endl;
			std::cout << "[Shader]   chose:";
			for (const std::string& feature : it->first) { std::cout << " " << feature; }
			std::cout << std::endl;
		}
	}

	return *it->second;
}

const Rendering::Data::FeatureSet& Rendering::Resources::Shader::GetFeatures() const
{
	return m_features;
}

const std::vector<std::string>& Rendering::Resources::Shader::GetDeclaredFeatures() const
{
	return m_declaredFeatures;
}

void Rendering::Resources::Shader::SetDeclaredFeatures(std::vector<std::string>&& p_features)
{
	m_declaredFeatures = std::move(p_features);
}

const std::unordered_set<std::string>& Rendering::Resources::Shader::GetPasses() const
{
	return m_passes;
}

Rendering::Resources::Shader::Shader(
	const std::string p_path,
	Variants&& p_variants,
	std::optional<std::vector<Parameter>> p_parameters
) : path(p_path)
{
	SetVariants(std::move(p_variants));

	if (p_parameters.has_value())
	{
		SetParameters(std::move(p_parameters.value()));
	}
}

void Rendering::Resources::Shader::SetVariants(Variants&& p_variants)
{
	ValidateVariants(p_variants);
	m_variants = std::move(p_variants);

	m_passes.clear();
	m_features.clear();
	m_usesVariantKeys = false;
	m_variantKeys.clear();

	// Find all passes & features based on the compiled variants
	for (const auto& [pass, featureVariants] : m_variants)
	{
		m_passes.insert(pass);

		for (const auto& featureSet : featureVariants | std::views::keys)
		{
			for (const std::string& feature : featureSet)
			{
				if (feature.starts_with("VARIANT_"))
				{
					m_usesVariantKeys = true;
					// Every key that exists in at least one variant: the scene's per-frame variant
					// bits are filtered against it, so asking for a bit this shader never compiles
					// cannot make the exact lookup miss and land on a variant without lighting.
					m_variantKeys.insert(feature);
					continue;
				}
				m_features.insert(feature);
			}
		}
	}
}

const std::unordered_set<std::string>& Rendering::Resources::Shader::GetVariantKeys() const
{
	return m_variantKeys;
}

const Rendering::Resources::Shader::Variants& Rendering::Resources::Shader::GetVariants() const
{
	return m_variants;
}

const std::vector<Rendering::Resources::Shader::Parameter>& Rendering::Resources::Shader::GetParameters() const
{
	return m_parameters;
}

bool Rendering::Resources::Shader::UsesVariantKeys() const
{
	return m_usesVariantKeys;
}

bool Rendering::Resources::Shader::HasParameterTable() const
{
	return m_hasParameterTable;
}

void Rendering::Resources::Shader::SetParameters(std::vector<Parameter>&& p_parameters)
{
	m_parameters = std::move(p_parameters);
	m_hasParameterTable = true;
}

bool Rendering::Resources::Shader::HasParameterBlock() const
{
	return m_parameterBlockSize > 0;
}

uint32_t Rendering::Resources::Shader::GetParameterBlockSize() const
{
	return m_parameterBlockSize;
}

void Rendering::Resources::Shader::SetParameterBlockSize(uint32_t p_size)
{
	m_parameterBlockSize = p_size;
}

const Rendering::Resources::Shader::State& Rendering::Resources::Shader::GetState() const
{
	return m_state;
}

bool Rendering::Resources::Shader::HasState() const
{
	return m_hasState;
}

void Rendering::Resources::Shader::SetState(State p_state)
{
	m_state = std::move(p_state);
	m_hasState = true;
}
