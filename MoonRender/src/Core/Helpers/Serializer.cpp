#include <tinyxml2.h>

#include <Core/ResourceManagement/TextureManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Core/ResourceManagement/MaterialManager.h>

#include <Core/Global/ServiceLocator.h>
#include <Core/Helpers/Serializer.h>

void Core::Helpers::Serializer::SerializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, bool p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const std::string& p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value.c_str());
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, float p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, double p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, uint32_t p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int64_t p_value)
{
	tinyxml2::XMLElement* element = p_doc.NewElement(p_name.c_str());
	element->SetText(p_value);
	p_node->InsertEndChild(element);
}

void Core::Helpers::Serializer::SerializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector2& p_value)
{
	tinyxml2::XMLNode* element = p_doc.NewElement(p_name.c_str());
	p_node->InsertEndChild(element);

	tinyxml2::XMLElement* x = p_doc.NewElement("x");
	x->SetText(p_value.x);
	element->InsertEndChild(x);

	tinyxml2::XMLElement* y = p_doc.NewElement("y");
	y->SetText(p_value.y);
	element->InsertEndChild(y);
}

void Core::Helpers::Serializer::SerializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector3& p_value)
{
	tinyxml2::XMLNode* element = p_doc.NewElement(p_name.c_str());
	p_node->InsertEndChild(element);

	tinyxml2::XMLElement* x = p_doc.NewElement("x");
	x->SetText(p_value.x);
	element->InsertEndChild(x);

	tinyxml2::XMLElement* y = p_doc.NewElement("y");
	y->SetText(p_value.y);
	element->InsertEndChild(y);

	tinyxml2::XMLElement* z = p_doc.NewElement("z");
	z->SetText(p_value.z);
	element->InsertEndChild(z);
}

void Core::Helpers::Serializer::SerializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FVector4& p_value)
{
	tinyxml2::XMLNode* element = p_doc.NewElement(p_name.c_str());
	p_node->InsertEndChild(element);

	tinyxml2::XMLElement* x = p_doc.NewElement("x");
	x->SetText(p_value.x);
	element->InsertEndChild(x);

	tinyxml2::XMLElement* y = p_doc.NewElement("y");
	y->SetText(p_value.y);
	element->InsertEndChild(y);

	tinyxml2::XMLElement* z = p_doc.NewElement("z");
	z->SetText(p_value.z);
	element->InsertEndChild(z);

	tinyxml2::XMLElement* w = p_doc.NewElement("w");
	w->SetText(p_value.w);
	element->InsertEndChild(w);
}

void Core::Helpers::Serializer::SerializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FMatrix4& p_value)
{
	tinyxml2::XMLNode* node = p_doc.NewElement(p_name.c_str());
	p_node->InsertEndChild(node);

	for (uint32_t i = 0; i < 16; ++i)
	{
		tinyxml2::XMLElement* element = p_doc.NewElement(("d" + std::to_string(i)).c_str());
		element->SetText(p_value.data[i]);
		node->InsertEndChild(element);
	}
}

void Core::Helpers::Serializer::SerializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, const Maths::FQuaternion& p_value)
{
	tinyxml2::XMLNode* element = p_doc.NewElement(p_name.c_str());
	p_node->InsertEndChild(element);

	tinyxml2::XMLElement* x = p_doc.NewElement("x");
	x->SetText(p_value.x);
	element->InsertEndChild(x);

	tinyxml2::XMLElement* y = p_doc.NewElement("y");
	y->SetText(p_value.y);
	element->InsertEndChild(y);

	tinyxml2::XMLElement* z = p_doc.NewElement("z");
	z->SetText(p_value.z);
	element->InsertEndChild(z);

	tinyxml2::XMLElement* w = p_doc.NewElement("w");
	w->SetText(p_value.w);
	element->InsertEndChild(w);
}


void Core::Helpers::Serializer::SerializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Resources::Model* p_value)
{
	SerializeString(p_doc, p_node, p_name.c_str(), p_value ? p_value->path : "?");
}

void Core::Helpers::Serializer::SerializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Resources::Texture* p_value)
{
	SerializeString(p_doc, p_node, p_name.c_str(), p_value ? p_value->path : "?");
}

void Core::Helpers::Serializer::SerializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Data::Material* p_value)
{
	SerializeString(p_doc, p_node, p_name.c_str(), p_value ? p_value->path : "?");
}

void Core::Helpers::Serializer::DeserializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, bool& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryBoolText(&p_out);
}

void Core::Helpers::Serializer::SerializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Core::Resources::MaterialAsset* p_value)
{
	SerializeString(p_doc, p_node, p_name.c_str(), p_value ? p_value->path : "?");
}

void Core::Helpers::Serializer::SerializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Audio::Resources::Sound* p_value)
{

}

void Core::Helpers::Serializer::DeserializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, std::string& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
	{
		const char* result = element->GetText();
		p_out = result ? result : "";
	}
}

void Core::Helpers::Serializer::DeserializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, float& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryFloatText(&p_out);
}

void Core::Helpers::Serializer::DeserializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, double& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryDoubleText(&p_out);
}

void Core::Helpers::Serializer::DeserializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryIntText(&p_out);
}

void Core::Helpers::Serializer::DeserializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, uint32_t& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryUnsignedText(&p_out);
}

void Core::Helpers::Serializer::DeserializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, int64_t& p_out)
{
	if (auto element = p_node->FirstChildElement(p_name.c_str()); element)
		element->QueryInt64Text(&p_out);
}

void Core::Helpers::Serializer::DeserializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector2& p_out)
{
	if (auto node = p_node->FirstChildElement(p_name.c_str()); node)
	{
		if (auto element = node->FirstChildElement("x"); element)
			element->QueryFloatText(&p_out.x);

		if (auto element = node->FirstChildElement("y"); element)
			element->QueryFloatText(&p_out.y);
	}
}

void Core::Helpers::Serializer::DeserializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector3& p_out)
{
	if (auto node = p_node->FirstChildElement(p_name.c_str()); node)
	{
		if (auto element = node->FirstChildElement("x"); element)
			element->QueryFloatText(&p_out.x);

		if (auto element = node->FirstChildElement("y"); element)
			element->QueryFloatText(&p_out.y);

		if (auto element = node->FirstChildElement("z"); element)
			element->QueryFloatText(&p_out.z);
	}
}

void Core::Helpers::Serializer::DeserializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FVector4& p_out)
{
	if (auto node = p_node->FirstChildElement(p_name.c_str()); node)
	{
		if (auto element = node->FirstChildElement("x"); element)
			element->QueryFloatText(&p_out.x);

		if (auto element = node->FirstChildElement("y"); element)
			element->QueryFloatText(&p_out.y);

		if (auto element = node->FirstChildElement("z"); element)
			element->QueryFloatText(&p_out.z);

		if (auto element = node->FirstChildElement("w"); element)
			element->QueryFloatText(&p_out.w);
	}
}

void Core::Helpers::Serializer::DeserializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FMatrix4& p_out)
{
	if (auto node = p_node->FirstChildElement(p_name.c_str()); node)
	{
		for (uint32_t i = 0; i < 16; ++i)
		{
			if (auto element = node->FirstChildElement(("d" + std::to_string(i)).c_str()); element)
				element->QueryFloatText(&(p_out.data[i]));
		}
	}
}

void Core::Helpers::Serializer::DeserializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Maths::FQuaternion& p_out)
{
	if (auto node = p_node->FirstChildElement(p_name.c_str()); node)
	{
		if (auto element = node->FirstChildElement("x"); element)
			element->QueryFloatText(&p_out.x);

		if (auto element = node->FirstChildElement("y"); element)
			element->QueryFloatText(&p_out.y);

		if (auto element = node->FirstChildElement("z"); element)
			element->QueryFloatText(&p_out.z);

		if (auto element = node->FirstChildElement("w"); element)
			element->QueryFloatText(&p_out.w);
	}
}



void Core::Helpers::Serializer::DeserializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Resources::Model*& p_out)
{
	if (std::string path = DeserializeString(p_doc, p_node, p_name.c_str()); path != "?" && path != "")
		p_out = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ModelManager>().GetResource(path);
	else
		p_out = nullptr;
}

void Core::Helpers::Serializer::DeserializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Resources::Texture*& p_out)
{
	if (std::string path = DeserializeString(p_doc, p_node, p_name.c_str()); path != "?" && path != "")
		p_out = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path);
	else
		p_out = nullptr;
}

void Core::Helpers::Serializer::DeserializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Rendering::Data::Material*& p_out)
{
	if (std::string path = DeserializeString(p_doc, p_node, p_name.c_str()); path != "?" && path != "")
		p_out = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ShaderManager>().GetResource(path);
	else
		p_out = nullptr;
}

void Core::Helpers::Serializer::DeserializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Core::Resources::MaterialAsset*& p_out)
{
	if (std::string path = DeserializeString(p_doc, p_node, p_name.c_str()); path != "?" && path != "")
		p_out = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().GetResource(path);
	else
		p_out = nullptr;
}

void Core::Helpers::Serializer::DeserializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name, Audio::Resources::Sound*& p_out)
{

}

bool Core::Helpers::Serializer::DeserializeBoolean(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	bool result;
	DeserializeBoolean(p_doc, p_node, p_name, result);
	return result;
}

std::string Core::Helpers::Serializer::DeserializeString(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	std::string result;
	DeserializeString(p_doc, p_node, p_name, result);
	return result;
}

float Core::Helpers::Serializer::DeserializeFloat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	float result;
	DeserializeFloat(p_doc, p_node, p_name, result);
	return result;
}

double Core::Helpers::Serializer::DeserializeDouble(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	double result;
	DeserializeDouble(p_doc, p_node, p_name, result);
	return result;
}

int Core::Helpers::Serializer::DeserializeInt(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	int result;
	DeserializeInt(p_doc, p_node, p_name, result);
	return result;
}

uint32_t Core::Helpers::Serializer::DeserializeUint32(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	uint32_t result;
	DeserializeUint32(p_doc, p_node, p_name, result);
	return result;
}

int64_t Core::Helpers::Serializer::DeserializeInt64(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	int64_t result;
	DeserializeInt64(p_doc, p_node, p_name, result);
	return result;
}

Maths::FVector2 Core::Helpers::Serializer::DeserializeVec2(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Maths::FVector2 result;
	DeserializeVec2(p_doc, p_node, p_name, result);
	return result;
}

Maths::FVector3 Core::Helpers::Serializer::DeserializeVec3(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Maths::FVector3 result;
	DeserializeVec3(p_doc, p_node, p_name, result);
	return result;
}

Maths::FVector4 Core::Helpers::Serializer::DeserializeVec4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Maths::FVector4 result;
	DeserializeVec4(p_doc, p_node, p_name, result);
	return result;
}

Maths::FMatrix4 Core::Helpers::Serializer::DeserializeMat4(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Maths::FMatrix4 result;
	DeserializeMat4(p_doc, p_node, p_name, result);
	return result;
}

Maths::FQuaternion Core::Helpers::Serializer::DeserializeQuat(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Maths::FQuaternion result;
	DeserializeQuat(p_doc, p_node, p_name, result);
	return result;
}


Rendering::Resources::Model* Core::Helpers::Serializer::DeserializeModel(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Rendering::Resources::Model* result;
	DeserializeModel(p_doc, p_node, p_name, result);
	return result;
}

Rendering::Resources::Texture* Core::Helpers::Serializer::DeserializeTexture(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Rendering::Resources::Texture* result;
	DeserializeTexture(p_doc, p_node, p_name, result);
	return result;
}

Rendering::Data::Material* Core::Helpers::Serializer::DeserializeShader(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Rendering::Data::Material* result;
	DeserializeShader(p_doc, p_node, p_name, result);
	return result;
}

Core::Resources::MaterialAsset* Core::Helpers::Serializer::DeserializeMaterial(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Core::Resources::MaterialAsset* result;
	DeserializeMaterial(p_doc, p_node, p_name, result);
	return result;
}

Audio::Resources::Sound* Core::Helpers::Serializer::DeserializeSound(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node, const std::string& p_name)
{
	Audio::Resources::Sound* result;
	DeserializeSound(p_doc, p_node, p_name, result);
	return result;
}
