#include <algorithm>
#include <format>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <vector>

#include <Rendering/Data/Material.h>
#include <Rendering/Data/MaterialBuilder.h>
#include <Rendering/Data/MaterialParser.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>

// ---- Material: the package material, its definition and variant program cache -----------------

Rendering::Data::Material::Material()
	: m_defaultInstance(std::make_unique<MaterialInstance>(*this, true))
{
}

Rendering::Data::Material::Material(Material* p_source)
	: Material()
{
	SetShader(p_source);
}

Rendering::Data::Material::Material(
	std::string p_path,
	std::shared_ptr<const MaterialDefinition> p_definition,
	Variants p_variants)
	: Material()
{
	path = std::move(p_path);
	m_definition = std::move(p_definition);
	SetVariants(std::move(p_variants));

	// A package material starts with no compiled programs; its passes/features still come from
	// the parser so feature queries work before the first variant is used.
	if (m_variants.empty() && m_definition != nullptr && m_definition->Parser() != nullptr)
	{
		for (const MaterialParser::Variant& variant : m_definition->Parser()->Variants())
		{
			m_passes.insert(variant.pass);
			for (const std::string& feature : variant.features)
			{
				if (feature.starts_with("VARIANT_"))
				{
					m_usesVariantKeys = true;
					m_variantKeys.insert(feature);
				}
				else
				{
					m_features.insert(feature);
				}
			}
		}
	}
}

Rendering::Data::Material::~Material() = default;

std::unique_ptr<Rendering::Data::MaterialInstance> Rendering::Data::Material::CreateInstance()
{
	return std::make_unique<MaterialInstance>(*this, false);
}

bool Rendering::Data::Material::HasShader() const
{
	return m_source != nullptr || m_definition != nullptr;
}

Rendering::Data::Material* Rendering::Data::Material::GetShader() const
{
	return m_source != nullptr ? m_source : const_cast<Material*>(this);
}

void Rendering::Data::Material::SetShader(Material* p_shader)
{
	if (p_shader == this)
	{
		return;
	}

	m_source = p_shader;
	if (p_shader != nullptr)
	{
		m_definition = p_shader->m_definition;
		m_variants = p_shader->m_variants;
		m_features = p_shader->m_features;
		m_passes = p_shader->m_passes;
		m_variantKeys = p_shader->m_variantKeys;
		m_declaredFeatures = p_shader->m_declaredFeatures;
		m_usesVariantKeys = p_shader->m_usesVariantKeys;
	}
	else
	{
		m_definition = nullptr;
		m_variants.clear();
		m_features.clear();
		m_passes.clear();
		m_variantKeys.clear();
		m_declaredFeatures.clear();
		m_usesVariantKeys = false;
	}

	m_defaultInstance->ResetProperties();
	ResetStateToShader();
}

const Rendering::Data::MaterialDefinition* Rendering::Data::Material::Definition() const
{
	return m_definition.get();
}

void Rendering::Data::Material::SetDefinition(
	std::shared_ptr<const MaterialDefinition> p_definition)
{
	m_definition = std::move(p_definition);
	m_defaultInstance->ResetProperties();
	ResetStateToShader();
}

Rendering::HAL::ShaderProgram& Rendering::Data::Material::GetVariant(
	std::optional<const std::string_view> p_pass,
	const Data::FeatureSet& p_featureSet)
{
	const MaterialParser* parser =
		m_definition != nullptr ? m_definition->Parser() : nullptr;
	std::string pass = std::string{ p_pass.value_or("") };

	if (m_variants.empty() && parser != nullptr && !parser->Variants().empty())
	{
		pass = p_pass.has_value() ? pass : parser->Variants().front().pass;
	}

	auto passIt = m_variants.find(pass);
	if (passIt == m_variants.end() && parser != nullptr)
	{
		// A package material compiles its variants lazily: the pass entry exists as soon as
		// the first feature combination for it is requested.
		passIt = m_variants.emplace(pass, FeatureVariants{}).first;
	}
	if (passIt == m_variants.end())
	{
		passIt = m_variants.begin();
	}
	if (passIt == m_variants.end())
	{
		throw std::runtime_error("material has no variant");
	}

	auto& variants = passIt->second;
	auto it = variants.find(p_featureSet);
	if (it != variants.end())
	{
		return *it->second;
	}

	if (parser != nullptr)
	{
		const MaterialParser::Variant* chosen = nullptr;
		size_t bestCommon = 0;
		size_t bestExtra = static_cast<size_t>(-1);
		for (const MaterialParser::Variant& candidate : parser->Variants())
		{
			if (candidate.pass != passIt->first)
			{
				continue;
			}
			size_t common = 0;
			for (const std::string& feature : candidate.features)
			{
				if (p_featureSet.contains(feature)) { ++common; }
			}
			const size_t extra = candidate.features.size() - common;
			if (chosen == nullptr || common > bestCommon
				|| (common == bestCommon && extra < bestExtra))
			{
				chosen = &candidate;
				bestCommon = common;
				bestExtra = extra;
			}
		}

		if (chosen == nullptr)
		{
			// The requested pass may not exist in this package (a fallback draw path). Use the
			// closest variant of any pass rather than failing the draw.
			for (const MaterialParser::Variant& candidate : parser->Variants())
			{
				if (chosen == nullptr || candidate.features.size() < chosen->features.size())
				{
					chosen = &candidate;
				}
			}
		}

		if (chosen != nullptr)
		{
			std::string error;
			auto program = Rendering::Resources::Loaders::ShaderLoader::CompileVariant(
				*parser, *chosen, &error);
			if (program == nullptr)
			{
				std::cerr << "[Material] " << path << ": " << error << std::endl;
				throw std::runtime_error("material variant failed to compile");
			}
			variants.emplace(p_featureSet, std::move(program));
			return *variants[p_featureSet];
		}
	}

	// Source-built material with a precompiled table: fall back to the closest program.
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

	if (it == variants.end())
	{
		throw std::runtime_error("material has no variant");
	}

	static std::set<std::string> reported;
	std::string requestKey = path + "|" + pass;
	for (const std::string& feature : p_featureSet) { requestKey += "|" + feature; }
	if (reported.insert(std::move(requestKey)).second)
	{
		std::cout << "[Material] " << path << " has no variant for pass " << pass
			<< " with " << p_featureSet.size() << " feature(s); using the closest one ("
			<< bestCommon << " match(es), " << bestExtra << " extra)." << std::endl;
	}

	return *it->second;
}

void Rendering::Data::Material::SetVariants(Variants&& p_variants)
{
	m_variants = std::move(p_variants);
	m_passes.clear();
	m_features.clear();
	m_usesVariantKeys = false;
	m_variantKeys.clear();

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
					m_variantKeys.insert(feature);
				}
				else
				{
					m_features.insert(feature);
				}
			}
		}
	}
}

const std::vector<Rendering::Data::Material::Parameter>&
Rendering::Data::Material::GetParameters() const
{
	static const std::vector<Parameter> empty;
	return m_definition != nullptr ? m_definition->Parameters() : empty;
}

bool Rendering::Data::Material::HasParameterTable() const
{
	return m_definition != nullptr && m_definition->HasParameterTable();
}

uint32_t Rendering::Data::Material::GetParameterBlockSize() const
{
	return m_definition != nullptr ? m_definition->ParameterBlockSize() : 0;
}

const Rendering::Data::Material::State& Rendering::Data::Material::GetState() const
{
	static const State empty;
	return m_definition != nullptr ? m_definition->GetState() : empty;
}

bool Rendering::Data::Material::HasState() const
{
	return m_definition != nullptr && m_definition->HasState();
}

const std::vector<std::string>& Rendering::Data::Material::GetDeclaredFeatures() const
{
	return m_declaredFeatures;
}

void Rendering::Data::Material::SetDeclaredFeatures(std::vector<std::string>&& p_features)
{
	m_declaredFeatures = std::move(p_features);
}

void Rendering::Data::Material::ResetStateToShader()
{
	m_defaultInstance->SetBlendable(false);
	m_defaultInstance->SetBlendMode(Rendering::Settings::EBlendMode::BLEND_OPAQUE);
	m_defaultInstance->SetBackfaceCulling(true);
	m_defaultInstance->SetFrontfaceCulling(false);
	m_defaultInstance->SetDepthWriting(true);

	if (HasState())
	{
		const State& state = GetState();
		if (state.blendable.has_value()) { m_defaultInstance->SetBlendable(state.blendable.value()); }
		if (state.blendMode.has_value())
		{
			m_defaultInstance->SetBlendMode(state.blendMode.value());
			m_defaultInstance->SetBlendable(
				state.blendMode.value() != Rendering::Settings::EBlendMode::BLEND_OPAQUE);
		}
		if (state.backfaceCulling.has_value()) { m_defaultInstance->SetBackfaceCulling(state.backfaceCulling.value()); }
		if (state.frontfaceCulling.has_value()) { m_defaultInstance->SetFrontfaceCulling(state.frontfaceCulling.value()); }
		if (state.depthWriting.has_value()) { m_defaultInstance->SetDepthWriting(state.depthWriting.value()); }
	}
}
