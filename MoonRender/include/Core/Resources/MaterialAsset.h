#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Data/MaterialProperty.h>
#include "Core/API/ISerializable.h"

namespace Rendering::Data { class Material; }

namespace Core::Resources
{
	/** A .mat asset: it knows which package Material it uses and owns the instance that
	 * drawables consume. The package Material is loaded through the resource manager (and
	 * shared); the asset does not create a second MaterialDefinition. */
	class MaterialAsset : public API::ISerializable
	{
	public:
		MaterialAsset();
		explicit MaterialAsset(::Rendering::Data::Material* p_source);
		virtual ~MaterialAsset() = default;

		/** Points the asset at a package Material and recreates its instance. */
		void SetMaterial(::Rendering::Data::Material* p_material);
		bool HasMaterial() const { return m_material != nullptr; }
		::Rendering::Data::Material* MaterialOf() const { return m_material; }

		::Rendering::Data::MaterialInstance& Instance() const { return *m_instance; }
		::Rendering::Data::MaterialInstance* InstancePtr() const { return m_instance.get(); }
		operator ::Rendering::Data::MaterialInstance&() const { return Instance(); }

		/** Compatibility: the asset's drawable instance. */
		::Rendering::Data::MaterialInstance& DefaultInstance() const { return Instance(); }
		::Rendering::Data::MaterialInstance* DefaultInstancePtr() const { return InstancePtr(); }
		std::unique_ptr<::Rendering::Data::MaterialInstance> CreateInstance() const;

		// ---- parameter API -------------------------------------------------------------
		void SetProperty(const std::string p_name, const ::Rendering::Data::MaterialPropertyType& p_value)
		{
			Instance().SetProperty(p_name, p_value);
		}
		bool TrySetProperty(const std::string& p_name, const ::Rendering::Data::MaterialPropertyType& p_value)
		{
			return Instance().TrySetProperty(p_name, p_value);
		}
		bool HasProperty(const std::string& p_name) const { return Instance().HasProperty(p_name); }
		std::optional<::Rendering::Data::MaterialProperty> GetProperty(const std::string p_name) const
		{
			return Instance().GetProperty(p_name);
		}
		bool ResetPropertyToDefault(const std::string& p_name)
		{
			return Instance().ResetPropertyToDefault(p_name);
		}
		std::map<std::string, ::Rendering::Data::MaterialProperty> SnapshotProperties() const
		{
			return Instance().SnapshotProperties();
		}
		void ResetProperties() { Instance().ResetProperties(); }
		const ::Rendering::Data::MaterialDefinition* Definition() const { return Instance().Definition(); }

		// ---- state --------------------------------------------------------------------
		bool IsValid() const { return Instance().IsValid(); }
		void SetBlendable(bool v) { Instance().SetBlendable(v); }
		bool IsBlendable() const { return Instance().IsBlendable(); }
		void SetBlendMode(::Rendering::Settings::EBlendMode v) { Instance().SetBlendMode(v); }
		::Rendering::Settings::EBlendMode GetBlendMode() const { return Instance().GetBlendMode(); }
		void SetTransparent(bool v) { Instance().SetTransparent(v); }
		bool IsTransparent() const { return Instance().IsTransparent(); }
		void SetUserInterface(bool v) { Instance().SetUserInterface(v); }
		bool IsUserInterface() const { return Instance().IsUserInterface(); }
		void SetBackfaceCulling(bool v) { Instance().SetBackfaceCulling(v); }
		bool HasBackfaceCulling() const { return Instance().HasBackfaceCulling(); }
		void SetFrontfaceCulling(bool v) { Instance().SetFrontfaceCulling(v); }
		bool HasFrontfaceCulling() const { return Instance().HasFrontfaceCulling(); }
		void SetDepthTest(bool v) { Instance().SetDepthTest(v); }
		bool HasDepthTest() const { return Instance().HasDepthTest(); }
		void SetDepthWriting(bool v) { Instance().SetDepthWriting(v); }
		bool HasDepthWriting() const { return Instance().HasDepthWriting(); }
		void SetColorWriting(bool v) { Instance().SetColorWriting(v); }
		bool HasColorWriting() const { return Instance().HasColorWriting(); }
		void SetCastShadows(bool v) { Instance().SetCastShadows(v); }
		bool IsShadowCaster() const { return Instance().IsShadowCaster(); }
		void SetReceiveShadows(bool v) { Instance().SetReceiveShadows(v); }
		bool IsShadowReceiver() const { return Instance().IsShadowReceiver(); }
		void SetCapturedByReflectionProbes(bool v) { Instance().SetCapturedByReflectionProbes(v); }
		bool IsCapturedByReflectionProbes() const { return Instance().IsCapturedByReflectionProbes(); }
		void SetReceiveReflections(bool v) { Instance().SetReceiveReflections(v); }
		bool IsReflectionReceiver() const { return Instance().IsReflectionReceiver(); }
		void SetGPUInstances(int v) { Instance().SetGPUInstances(v); }
		int GetGPUInstances() const { return Instance().GetGPUInstances(); }
		void SetDrawOrder(int v) { Instance().SetDrawOrder(v); }
		int GetDrawOrder() const { return Instance().GetDrawOrder(); }
		void SetPolygonOffsetFill(bool v) { Instance().SetPolygonOffsetFill(v); }
		bool HasPolygonOffsetFill() const { return Instance().HasPolygonOffsetFill(); }
		void SetLineWidth(float v) { Instance().SetLineWidth(v); }
		float GetLineWidth() const { return Instance().GetLineWidth(); }
		bool SupportsOrthographic() const { return Instance().SupportsOrthographic(); }
		bool SupportsPerspective() const { return Instance().SupportsPerspective(); }
		void SetOrthographicSupport(bool v) { Instance().SetOrthographicSupport(v); }
		void SetPerspectiveSupport(bool v) { Instance().SetPerspectiveSupport(v); }
		::Rendering::Data::StateMask GenerateStateMask() const { return Instance().GenerateStateMask(); }

		void SetFeatures(const ::Rendering::Data::FeatureSet& f) { Instance().SetFeatures(f); }
		void AddFeature(const std::string& f) { Instance().AddFeature(f); }
		void RemoveFeature(const std::string& f) { Instance().RemoveFeature(f); }
		void EnableFeature(const std::string& f, bool v) { Instance().EnableFeature(f, v); }
		bool HasFeature(const std::string& f) const { return Instance().HasFeature(f); }
		bool SupportsFeature(const std::string& f) const { return Instance().SupportsFeature(f); }
		bool HasPass(const std::string& p) const { return Instance().HasPass(p); }
		const ::Rendering::Data::FeatureSet& GetFeatures() const { return Instance().GetFeatures(); }

		void LateUpdateTexture(const std::string& p_name, const std::string& p_path)
		{
			Instance().LateUpdateTexture(p_name, p_path);
		}

		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		std::string path;

	private:
		::Rendering::Data::Material* m_material = nullptr;
		std::unique_ptr<::Rendering::Data::MaterialInstance> m_instance;
	};
}
