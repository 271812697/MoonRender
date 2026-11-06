#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Rendering/Data/FeatureSet.h>
#include <Rendering/Data/MaterialDefinition.h>
#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Data/StateMask.h>
#include <Rendering/HAL/ShaderProgram.h>

namespace Rendering::Data
{
	/** The per-material binding point lives with the other engine blocks (see
	 * Rendering/Settings/EBindingPoint.h). */
	using ::Rendering::Settings::kMaterialParameterBinding;

	/** A material: the immutable MaterialDefinition plus the default MaterialInstance.
	 *
	 * This is filament's split: Material is not an instance. It holds the definition (the
	 * package's parameter/sampler layout, render state and shader variants) and owns the
	 * default instance; CreateInstance() gives an independent descriptor set + state overrides.
	 * The forwarding methods below keep the existing call sites ("hold a Material& and set a
	 * parameter on it") working by talking to the default instance. */
	class Material
	{
	public:
		using Parameter = MaterialParameter;
		using State = MaterialState;
		using FeatureVariants = std::unordered_map<
			Data::FeatureSet,
			std::shared_ptr<Rendering::HAL::ShaderProgram>,
			Data::FeatureSetHash,
			Data::FeatureSetEqual>;
		using Variants = std::unordered_map<std::string, FeatureVariants>;

		Material();
		explicit Material(Material* p_source);
		Material(
			std::string p_path,
			std::shared_ptr<const MaterialDefinition> p_definition,
			Variants p_variants);
		virtual ~Material();
		Material(const Material&) = delete;
		Material& operator=(const Material&) = delete;

		/** Shares another material's definition and variant program cache. A .mat override
		 * points at the package material this way. */
		void SetMaterial(Material* p_shader);
		bool HasMaterial() const;
		Material* MaterialOf() const;

		const MaterialDefinition* Definition() const;
		void SetDefinition(std::shared_ptr<const MaterialDefinition> p_definition);

		Rendering::HAL::ShaderProgram& GetVariant(
			std::optional<const std::string_view> p_pass = std::nullopt,
			const Data::FeatureSet& p_featureSet = {});
		const Variants& GetVariants() const { return m_variants; }
		void SetVariants(Variants&& p_variants);
		const Data::FeatureSet& GetFeatures() const { return m_features; }
		const std::unordered_set<std::string>& GetPasses() const { return m_passes; }
		const std::vector<Parameter>& GetParameters() const;
		bool HasParameterTable() const;
		uint32_t GetParameterBlockSize() const;
		const State& GetState() const;
		bool HasState() const;
		bool UsesVariantKeys() const { return m_usesVariantKeys; }
		const std::unordered_set<std::string>& GetVariantKeys() const { return m_variantKeys; }
		const std::vector<std::string>& GetDeclaredFeatures() const;
		void SetDeclaredFeatures(std::vector<std::string>&& p_features);

		/** The default instance every Material owns (filament's getDefaultInstance()). */
		MaterialInstance& DefaultInstance() const { return *m_defaultInstance; }
		MaterialInstance* DefaultInstancePtr() const { return m_defaultInstance.get(); }

		/** An independent instance: its own descriptor set and state overrides, sharing this
		 * material's definition and variant programs. */
		std::unique_ptr<MaterialInstance> CreateInstance();

		/** Applies the shader's material block to the default instance. */
		void ResetStateToMaterial();

		// ---- parameter / texture API, forwarded to the default instance -------------------
		void SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse = false)
		{
			m_defaultInstance->SetProperty(p_name, p_value, p_singleUse);
		}
		bool TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse = false)
		{
			return m_defaultInstance->TrySetProperty(p_name, p_value, p_singleUse);
		}
		bool HasProperty(const std::string& p_name) const { return m_defaultInstance->HasProperty(p_name); }
		bool ResetPropertyToDefault(const std::string& p_name)
		{
			return m_defaultInstance->ResetPropertyToDefault(p_name);
		}
		std::optional<MaterialProperty> GetProperty(const std::string p_name) const
		{
			return m_defaultInstance->GetProperty(p_name);
		}
		std::map<std::string, MaterialProperty> SnapshotProperties() const
		{
			return m_defaultInstance->SnapshotProperties();
		}
		void ResetProperties() { m_defaultInstance->ResetProperties(); }
		void UpdateProperties() { m_defaultInstance->UpdateProperties(); }
		void LateUpdateTexture(const std::string& p_name, const std::string& p_path)
		{
			m_defaultInstance->LateUpdateTexture(p_name, p_path);
		}
		void BindGlobalTexture(const std::string& p_name, Rendering::HAL::Texture* p_texture)
		{
			m_defaultInstance->BindGlobalTexture(p_name, p_texture);
		}

		// ---- variant / draw API ---------------------------------------------------------
		uint64_t Revision() const { return m_defaultInstance->Revision(); }
		MaterialSignatureSet Bind(
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			std::optional<MaterialSignatureSet> p_previousMaterialSignature = std::nullopt)
		{
			return m_defaultInstance->Bind(p_emptyTexture2D, p_emptyTextureCube, p_pass,
				p_featureSetOverride, p_previousMaterialSignature);
		}
		void Unbind(bool p_resetBoundProgram = false) { m_defaultInstance->Unbind(p_resetBoundProgram); }
		void UploadProperties(
			bool uploadStableProperties,
			bool uploadSingleUseProperties,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			Rendering::HAL::Texture* p_flatNormalTexture = nullptr)
		{
			m_defaultInstance->UploadProperties(uploadStableProperties, uploadSingleUseProperties,
				p_emptyTexture2D, p_emptyTextureCube, p_flatNormalTexture);
		}

		// ---- features -------------------------------------------------------------------
		const Data::FeatureSet& Features() const { return m_defaultInstance->Features(); }
		void SetFeatures(const Data::FeatureSet& p_features) { m_defaultInstance->SetFeatures(p_features); }
		void AddFeature(const std::string& p_feature) { m_defaultInstance->AddFeature(p_feature); }
		void RemoveFeature(const std::string& p_feature) { m_defaultInstance->RemoveFeature(p_feature); }
		void EnableFeature(const std::string& p_feature, bool p_flag) { m_defaultInstance->EnableFeature(p_feature, p_flag); }
		bool HasFeature(const std::string& p_feature) const { return m_defaultInstance->HasFeature(p_feature); }
		bool SupportsFeature(const std::string& p_feature) const { return m_defaultInstance->SupportsFeature(p_feature); }
		bool HasPass(const std::string& p_pass) const { return m_defaultInstance->HasPass(p_pass); }

		// ---- render state ---------------------------------------------------------------
		bool IsValid() const { return m_defaultInstance->IsValid(); }
		bool IsBlendable() const { return m_defaultInstance->IsBlendable(); }
		bool IsTransparent() const { return m_defaultInstance->IsTransparent(); }
		bool IsUserInterface() const { return m_defaultInstance->IsUserInterface(); }
		bool HasBackfaceCulling() const { return m_defaultInstance->HasBackfaceCulling(); }
		bool HasFrontfaceCulling() const { return m_defaultInstance->HasFrontfaceCulling(); }
		bool HasDepthTest() const { return m_defaultInstance->HasDepthTest(); }
		bool HasDepthWriting() const { return m_defaultInstance->HasDepthWriting(); }
		bool HasColorWriting() const { return m_defaultInstance->HasColorWriting(); }
		bool IsShadowCaster() const { return m_defaultInstance->IsShadowCaster(); }
		bool IsShadowReceiver() const { return m_defaultInstance->IsShadowReceiver(); }
		bool IsCapturedByReflectionProbes() const { return m_defaultInstance->IsCapturedByReflectionProbes(); }
		bool IsReflectionReceiver() const { return m_defaultInstance->IsReflectionReceiver(); }
		bool HasPolygonOffsetFill() const { return m_defaultInstance->HasPolygonOffsetFill(); }
		int GetDrawOrder() const { return m_defaultInstance->GetDrawOrder(); }
		int GetGPUInstances() const { return m_defaultInstance->GetGPUInstances(); }
		float GetLineWidth() const { return m_defaultInstance->GetLineWidth(); }
		Rendering::Settings::EBlendMode GetBlendMode() const { return m_defaultInstance->GetBlendMode(); }
		bool SupportsOrthographic() const { return m_defaultInstance->SupportsOrthographic(); }
		bool SupportsPerspective() const { return m_defaultInstance->SupportsPerspective(); }
		bool SupportsProjectionMode(Rendering::Settings::EProjectionMode p_mode) const
		{
			return m_defaultInstance->SupportsProjectionMode(p_mode);
		}
		Rendering::Data::StateMask GenerateStateMask() const { return m_defaultInstance->GenerateStateMask(); }
		Rendering::HAL::Texture* TextureOf(const std::string& p_name) const { return m_defaultInstance->TextureOf(p_name); }

		void SetBlendable(bool p_blendable) { m_defaultInstance->SetBlendable(p_blendable); }
		void SetBlendMode(Rendering::Settings::EBlendMode p_blendMode) { m_defaultInstance->SetBlendMode(p_blendMode); }
		void SetTransparent(bool p_transparent) { m_defaultInstance->SetTransparent(p_transparent); }
		void SetBackfaceCulling(bool p_backfaceCulling) { m_defaultInstance->SetBackfaceCulling(p_backfaceCulling); }
		void SetFrontfaceCulling(bool p_frontfaceCulling) { m_defaultInstance->SetFrontfaceCulling(p_frontfaceCulling); }
		void SetPolygonOffsetFill(bool p_polygonOffsetFill) { m_defaultInstance->SetPolygonOffsetFill(p_polygonOffsetFill); }
		void SetDepthTest(bool p_depthTest) { m_defaultInstance->SetDepthTest(p_depthTest); }
		void SetDepthWriting(bool p_depthWriting) { m_defaultInstance->SetDepthWriting(p_depthWriting); }
		void SetColorWriting(bool p_colorWriting) { m_defaultInstance->SetColorWriting(p_colorWriting); }
		void SetUserInterface(bool p_userInterface) { m_defaultInstance->SetUserInterface(p_userInterface); }
		void SetDrawOrder(int p_order) { m_defaultInstance->SetDrawOrder(p_order); }
		void SetGPUInstances(int p_instances) { m_defaultInstance->SetGPUInstances(p_instances); }
		void SetCastShadows(bool p_castShadows) { m_defaultInstance->SetCastShadows(p_castShadows); }
		void SetReceiveShadows(bool p_receiveShadows) { m_defaultInstance->SetReceiveShadows(p_receiveShadows); }
		void SetCapturedByReflectionProbes(bool p_capturedBy)
		{
			m_defaultInstance->SetCapturedByReflectionProbes(p_capturedBy);
		}
		void SetReceiveReflections(bool p_receiveReflections) { m_defaultInstance->SetReceiveReflections(p_receiveReflections); }
		void SetLineWidth(float p_width) { m_defaultInstance->SetLineWidth(p_width); }
		void SetOrthographicSupport(bool p_support) { m_defaultInstance->SetOrthographicSupport(p_support); }
		void SetPerspectiveSupport(bool p_support) { m_defaultInstance->SetPerspectiveSupport(p_support); }

	private:
		std::shared_ptr<const MaterialDefinition> m_definition;
		std::unique_ptr<MaterialInstance> m_defaultInstance;

	public:
		/** The package/material asset path (the .shader key for a package material). */
		std::string path;

	private:
		Material* m_source = nullptr;
		Variants m_variants;
		Data::FeatureSet m_features;
		std::unordered_set<std::string> m_passes;
		std::unordered_set<std::string> m_variantKeys;
		std::vector<std::string> m_declaredFeatures;
		bool m_usesVariantKeys = false;
	};
}
