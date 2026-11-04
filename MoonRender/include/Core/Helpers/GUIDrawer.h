#pragma once
#include <Maths/FVector2.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>
#include <Maths/FQuaternion.h>

namespace Core::Resources
{
	class Material;
}

namespace Audio::Resources
{
	class Sound;
}

namespace Rendering::Resources
{
	class Model;
	class Texture;
}

namespace Core::Helpers
{

	class GUIDrawer
	{
	public:


		static const float _MIN_FLOAT;
		static const float _MAX_FLOAT;


		static void ProvideEmptyTexture(::Rendering::Resources::Texture& p_emptyTexture);


		template <typename T>
		static std::string GetFormat();

	private:
		static ::Rendering::Resources::Texture* __EMPTY_TEXTURE;
	};
}

#include "Core/Helpers/GUIDrawer.inl"