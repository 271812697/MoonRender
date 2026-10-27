#pragma once
#include <Rendering/Data/Describable.h>
#include <Rendering/Data/Material.h>
#include <Rendering/Resources/IMesh.h>
#include <Tools/Utils/OptRef.h>

namespace Rendering::Entities
{
	/**
	* Drawable entity
	*/
	struct Drawable : public Data::Describable
	{
		Tools::Utils::OptRef<Rendering::Resources::IMesh> mesh;
		Tools::Utils::OptRef<Rendering::Data::MaterialInstance> material;
		Data::StateMask stateMask;
		int subIndexBufferIndex = 0;
		Settings::EPrimitiveMode primitiveMode = Rendering::Settings::EPrimitiveMode::TRIANGLES;
		std::optional<std::string> pass = std::nullopt;
		std::optional<Data::FeatureSet> featureSetOverride = std::nullopt;
	};
}