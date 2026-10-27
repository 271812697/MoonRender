#include <tinyxml2.h>
#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Resources/Material.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>

#include <Tools/Utils/PathParser.h>


Core::ECS::Components::CMaterialRenderer::CMaterialRenderer(ECS::Actor& p_owner) : AComponent(p_owner)
{
	m_materials.fill(nullptr);


	UpdateMaterialList();
}

std::string Core::ECS::Components::CMaterialRenderer::GetName()
{
	return "Material Renderer";
}

void Core::ECS::Components::CMaterialRenderer::FillWithMaterial(::Rendering::Data::MaterialInstance& p_material)
{
	for (uint8_t i = 0; i < m_materials.size(); ++i)
		m_materials[i] = &p_material;
}

void Core::ECS::Components::CMaterialRenderer::SetMaterialAtIndex(uint8_t p_index, ::Rendering::Data::MaterialInstance& p_material)
{
	m_materials[p_index] = &p_material;
}

void Core::ECS::Components::CMaterialRenderer::SetMaterialInstanceAtIndex(uint8_t p_index,
	std::unique_ptr<::Rendering::Data::MaterialInstance> p_instance)
{
	if (p_index >= m_materials.size() || p_instance == nullptr)
	{
		return;
	}

	m_materials[p_index] = p_instance.get();
	m_ownedInstances.push_back(std::move(p_instance));
}
void Core::ECS::Components::CMaterialRenderer::FillWithMaterialInstance(
	std::unique_ptr<::Rendering::Data::MaterialInstance> p_instance)
{
	if (p_instance == nullptr)
	{
		return;
	}

	::Rendering::Data::MaterialInstance* instance = p_instance.get();
	m_ownedInstances.push_back(std::move(p_instance));

	for (uint8_t i = 0; i < m_materials.size(); ++i)
	{
		m_materials[i] = instance;
	}
}
::Rendering::Data::MaterialInstance* Core::ECS::Components::CMaterialRenderer::GetMaterialAtIndex(uint8_t p_index)
{
	return m_materials.at(p_index);
}

void Core::ECS::Components::CMaterialRenderer::RemoveMaterialAtIndex(uint8_t p_index)
{
	if (p_index < m_materials.size())
	{
		m_materials[p_index] = nullptr;;
	}
}

void Core::ECS::Components::CMaterialRenderer::RemoveMaterialByInstance(::Rendering::Data::MaterialInstance& p_instance)
{
	for (uint8_t i = 0; i < m_materials.size(); ++i)
		if (m_materials[i] == &p_instance)
			m_materials[i] = nullptr;
}

void Core::ECS::Components::CMaterialRenderer::RemoveAllMaterials()
{
	for (uint8_t i = 0; i < m_materials.size(); ++i)
		m_materials[i] = nullptr;
}

const Maths::FMatrix4& Core::ECS::Components::CMaterialRenderer::GetUserMatrix() const
{
	return m_userMatrix;
}

const Core::ECS::Components::CMaterialRenderer::MaterialList& Core::ECS::Components::CMaterialRenderer::GetMaterials() const
{
	return m_materials;
}

void Core::ECS::Components::CMaterialRenderer::SetVisibilityFlags(Core::Rendering::EVisibilityFlags p_flags)
{
	m_visibilityFlags = p_flags;
}

Core::Rendering::EVisibilityFlags Core::ECS::Components::CMaterialRenderer::GetVisibilityFlags() const
{
	return m_visibilityFlags;
}

bool Core::ECS::Components::CMaterialRenderer::HasVisibilityFlags(Core::Rendering::EVisibilityFlags p_flags) const
{
	return Core::Rendering::SatisfiesVisibility(m_visibilityFlags, p_flags);
}

void Core::ECS::Components::CMaterialRenderer::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	tinyxml2::XMLNode* materialsNode = p_doc.NewElement("materials");
	p_node->InsertEndChild(materialsNode);

	auto modelRenderer = owner.GetComponent<CModelRenderer>();
	uint8_t matnum=modelRenderer->GetModel()->GetMaterialNames().size();
	
	uint8_t elementsToSerialize = modelRenderer->GetModel() ? std::min(matnum, kMaxMaterialCount) : 0;

	for (uint8_t i = 0; i < elementsToSerialize; ++i)
	{
		// Only a material asset (a definition) has a path to serialize. A per-actor instance is
		// rebuilt by whoever created it, so it is skipped rather than written as a dangling name.
		if (auto* asset = dynamic_cast<Core::Resources::Material*>(m_materials[i]))
		{
			Core::Helpers::Serializer::SerializeMaterial(p_doc, materialsNode, "material", asset);
		}
	}

	Core::Helpers::Serializer::SerializeUint32(p_doc, p_node, "visibility_flags", reinterpret_cast<uint32_t&>(m_visibilityFlags));
}

void Core::ECS::Components::CMaterialRenderer::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	tinyxml2::XMLNode* materialsRoot = p_node->FirstChildElement("materials");
	if (materialsRoot)
	{
		tinyxml2::XMLElement* currentMaterial = materialsRoot->FirstChildElement("material");

		uint8_t materialIndex = 0;

		while (currentMaterial)
		{
			if (auto material = Global::ServiceLocator::Get<ResourceManagement::MaterialManager>()[currentMaterial->GetText()])
				m_materials[materialIndex] = material;

			currentMaterial = currentMaterial->NextSiblingElement("material");
			++materialIndex;
		}
	}

	UpdateMaterialList();

	Core::Helpers::Serializer::DeserializeUint32(p_doc, p_node, "visibility_flags", reinterpret_cast<uint32_t&>(m_visibilityFlags));
}




void Core::ECS::Components::CMaterialRenderer::UpdateMaterialList()
{
	if (auto modelRenderer = owner.GetComponent<CModelRenderer>(); modelRenderer && modelRenderer->GetModel())
	{
		uint8_t materialIndex = 0;

		for (const std::string& materialName : modelRenderer->GetModel()->GetMaterialNames())
		{
			m_materialNames[materialIndex++] = materialName;
		}

		for (uint8_t i = materialIndex; i < kMaxMaterialCount; ++i)
			m_materialNames[i] = "";
	}


}

void Core::ECS::Components::CMaterialRenderer::SetUserMatrixElement(uint32_t p_row, uint32_t p_column, float p_value)
{
	if (p_row < 4 && p_column < 4)
		m_userMatrix.data[4 * p_row + p_column] = p_value;
}

float Core::ECS::Components::CMaterialRenderer::GetUserMatrixElement(uint32_t p_row, uint32_t p_column) const
{
	if (p_row < 4 && p_column < 4)
		return m_userMatrix.data[4 * p_row + p_column];
	else
		return 0.0f;
}
