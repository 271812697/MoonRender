#pragma once
#include <string>
#include <Maths/FVector2.h>
#include <Maths/FVector3.h>
#include <Maths/FVector4.h>
#include <Maths/FQuaternion.h>



/* Forward declaration */
namespace Rendering::Resources
{
	class Model;
	class Texture;
}

namespace Rendering::Data
{
	class Material;
}

namespace Audio::Resources
{
	class Sound;
}

namespace Core::Resources
{
	class MaterialAsset;
}

namespace tinyxml2
{
	class XMLDocument;
	class XMLNode;
}

namespace Core::Helpers
{
	class Serializer
	{
	public:
#pragma region SERIALIZATION_HELPERS
		static void SerializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, bool p_value);
		static void SerializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const std::string& p_value);
		static void SerializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, float p_value);
		static void SerializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, double p_value);
		static void SerializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int p_value);
		static void SerializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, uint32_t p_value);
		static void SerializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int64_t p_value);
		static void SerializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector2& p_value);
		static void SerializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector3& p_value);
		static void SerializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector4& p_value);
		static void SerializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FMatrix4& p_value);
		static void SerializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FQuaternion& p_value);

		static void SerializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Resources::Model* p_value);
		static void SerializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Resources::Texture* p_value);
		static void SerializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Data::Material* p_value);
		static void SerializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Core::Resources::MaterialAsset* p_value);
		static void SerializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Audio::Resources::Sound* p_value);
#pragma endregion

#pragma region DESERIALIZATION_HELPERS
		static void DeserializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, bool& p_out);
		static void DeserializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, std::string& p_out);
		static void DeserializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, float& p_out);
		static void DeserializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, double& p_out);
		static void DeserializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int& p_out);
		static void DeserializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, uint32_t& p_out);
		static void DeserializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int64_t& p_out);
		static void DeserializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector2& p_out);
		static void DeserializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector3& p_out);
		static void DeserializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector4& p_out);
		static void DeserializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FMatrix4& p_out);
		static void DeserializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FQuaternion& p_out);

		static void DeserializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Resources::Model*& p_out);
		static void DeserializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Resources::Texture*& p_out);
		static void DeserializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, ::Rendering::Data::Material*& p_out);
		static void DeserializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Core::Resources::MaterialAsset*& p_out);
		static void DeserializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Audio::Resources::Sound*& p_out);

		static bool DeserializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static std::string DeserializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static float DeserializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static double DeserializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static int DeserializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static unsigned DeserializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static int64_t DeserializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Maths::FVector2 DeserializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Maths::FVector3 DeserializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Maths::FVector4 DeserializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Maths::FMatrix4 DeserializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Maths::FQuaternion DeserializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);

		static ::Rendering::Resources::Model* DeserializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static ::Rendering::Resources::Texture* DeserializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static ::Rendering::Data::Material* DeserializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Core::Resources::MaterialAsset* DeserializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
		static Audio::Resources::Sound* DeserializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name);
#pragma endregion
	};
}
