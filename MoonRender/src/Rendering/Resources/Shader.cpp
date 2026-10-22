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
		it = variants.find(Data::FeatureSet{});
		if (it == variants.end())
		{
			it = variants.begin();
		}

		static std::set<std::string> reported;
		if (reported.insert(path + "|" + pass).second)
		{
			std::cout << "[Shader] " << path << " has no variant for pass " << pass
				<< " with " << p_featureSet.size() << " feature(s); using a fallback." << std::endl;
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
					continue;
				}
				m_features.insert(feature);
			}
		}
	}
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
