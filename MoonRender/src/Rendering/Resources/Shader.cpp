#include <format>
#include <ranges>
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

	if (!m_variants.contains(pass))
	{

		return *m_variants[{}][{}];
	}

	if (!m_variants[pass].contains(p_featureSet))
	{
	
		return *m_variants[pass][{}];
	}

	return *m_variants[pass][p_featureSet];
}

const Rendering::Data::FeatureSet& Rendering::Resources::Shader::GetFeatures() const
{
	return m_features;
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

	// Find all passes & features based on the compiled variants
	for (const auto& [pass, featureVariants] : m_variants)
	{
		m_passes.insert(pass);

		for (const auto& featureSet : featureVariants | std::views::keys)
		{
			m_features.insert(featureSet.begin(), featureSet.end());
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
