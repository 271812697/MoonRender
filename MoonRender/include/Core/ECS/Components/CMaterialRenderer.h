#pragma once
#include <array>
#include <Core/ECS/Components/AComponent.h>
#include <Core/Rendering/EVisibilityFlags.h>
#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Resources/Mesh.h>
constexpr uint8_t kMaxMaterialCount = 0xFF;
namespace Core::ECS { class Actor; }
namespace Core::ECS::Components
{
	/**
	* A component that handle a material list, necessary for model rendering
	*/
	class CMaterialRenderer : public AComponent
	{
	public:
		/** What a drawable draws with: an instance, not the material definition. A definition is
		 * shared (one per shader), an instance carries the per-actor parameter overrides. */
		using MaterialList = std::array<::Rendering::Data::MaterialInstance*, kMaxMaterialCount>;

		/**
		* Constructor
		* @param p_owner
		*/
		CMaterialRenderer(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Fill the material renderer with the given material
		* @param p_material
		*/
		void FillWithMaterial(::Rendering::Data::MaterialInstance& p_material);

		/**
		* Defines the material to use for the given index
		* @param p_index
		* @param p_material
		*/
		void SetMaterialAtIndex(uint8_t p_index, ::Rendering::Data::MaterialInstance& p_material);

		/** Takes an instance the component owns and draws with at p_index. What a material drop onto
		 * an actor does: the shape must not write its per-shape values (the domain colour table, the
		 * pick colour) into the shared material asset, so it draws through an instance of it. */
		void SetMaterialInstanceAtIndex(uint8_t p_index, std::unique_ptr<::Rendering::Data::MaterialInstance> p_instance);

		/** Gives the same instance to every slot the renderer has: a drop names one material for the
		 * whole actor, and a mesh may split its sub-ranges over several material indices. */
		void FillWithMaterialInstance(std::unique_ptr<::Rendering::Data::MaterialInstance> p_instance);

		/**
		* Returns the material to use at index
		* @param p_index
		*/
		::Rendering::Data::MaterialInstance* GetMaterialAtIndex(uint8_t p_index);

		/**
		* Remove the material at index
		* @param p_index
		*/
		void RemoveMaterialAtIndex(uint8_t p_index);

		/**
		* Remove the material by instance
		* @param p_instance
		*/
		void RemoveMaterialByInstance(::Rendering::Data::MaterialInstance& p_instance);

		/**
		* Remove every materials
		*/
		void RemoveAllMaterials();

		/**
		* Update the material list by fetching model information
		*/
		void UpdateMaterialList();

		/**
		* Defines an element of the user matrix
		* @param p_row
		* @param p_column
		* @param p_value
		*/
		void SetUserMatrixElement(uint32_t p_row, uint32_t p_column, float p_value);

		/**
		* Returns an element of the user matrix
		* @param p_row
		* @param p_column
		*/
		float GetUserMatrixElement(uint32_t p_row, uint32_t p_column) const;

		/**
		* Returns the user matrix
		*/
		const Maths::FMatrix4& GetUserMatrix() const;

		/**
		* Returns the materials
		*/
		const MaterialList& GetMaterials() const;

		/**
		* Sets visibility flags
		* @param p_flags
		*/
		void SetVisibilityFlags(Core::Rendering::EVisibilityFlags p_flags);

		/**
		* Returns the visibility flags
		*/
		Core::Rendering::EVisibilityFlags GetVisibilityFlags() const;

		/**
		* Return true if the component has the given visibility flags
		* @param p_flags
		*/
		bool HasVisibilityFlags(Core::Rendering::EVisibilityFlags p_flags) const;

		/**
		* Serialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		/**
		* Deserialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/


	private:
		MaterialList m_materials;
		/** The instances created for this renderer (see SetMaterialInstanceAtIndex). */
		std::vector<std::unique_ptr<::Rendering::Data::MaterialInstance>> m_ownedInstances;

		std::array<std::string, kMaxMaterialCount> m_materialNames;
		Maths::FMatrix4 m_userMatrix;
		Rendering::EVisibilityFlags m_visibilityFlags = Rendering::EVisibilityFlags::ALL;
	};
}