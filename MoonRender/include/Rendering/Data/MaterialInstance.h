#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <Rendering/Data/MaterialProperty.h>
#include <Rendering/Data/DescriptorSet.h>
#include <Rendering/Data/StateMask.h>
#include <Rendering/Data/UniformBufferPool.h>
#include <Rendering/Data/FeatureSet.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Settings/EBindingPoint.h>
#include <Rendering/Settings/EProjectionMode.h>

namespace Rendering::Data
{
	class Material;
	class MaterialDefinition;
	class MaterialInstance;

	struct MaterialSignatureSet
	{
		std::size_t bindSignature = 0;
		std::size_t stablePropertySignature = 0;
	};

	/** A bindable set of parameter and state overrides for one Material definition.
	 *
	 * Filament's split: Material owns the immutable MaterialDefinition, its default instance
	 * and the variants; MaterialInstance owns only its values and its descriptor set. */
	class MaterialInstance
	{
	public:
		/** One material's set of parameter and state overrides. */

		/** p_isDefault marks the instance a Material owns: it is seeded from the definition's
		 * declared defaults and engine state, and is what a plain instance falls back to. */
		explicit MaterialInstance(Material& p_definition, bool p_isDefault = false);
		MaterialInstance();
		virtual ~MaterialInstance();

		/** Reparents this instance to another material (a .mat asset points at a package
		 * material this way). */
		void SetMaterial(Material& p_material);
		/** Seeds the engine defaults and then the material's declared render state. */
		void ResetStateToMaterial();

		MaterialInstance(const MaterialInstance&) = delete;
		MaterialInstance& operator=(const MaterialInstance&) = delete;

		/** The Material this instance overrides. Null for a Material (it is its own definition). */
		Material* ParentMaterial() const { return m_material; }
		/** The package Material whose definition and variants this instance uses. */
		Material* MaterialOf() const;

		/** Bumped whenever this instance's parameter values change; the renderer hashes it to
		 * decide whether the instance's own uniform block needs uploading. */
		uint64_t Revision() const { return m_revision; }
		/** True for the instance a Material owns (filament's default instance). */
		bool IsDefaultInstance() const { return m_isDefaultInstance; }

		/** The immutable package data: block layout, sampler fields, state and variants. */
		const MaterialDefinition* Definition() const;

		/** Re-reads a texture assigned by path (the designer saves a path, not a handle). */
		void LateUpdateTexture(const std::string& p_name, const std::string& p_path);
		const Data::FeatureSet& Features() const { return FeaturesOf(); }

		/** Binds the program for p_pass / the drawable's feature set and the instance's parameter
		 * block. Returns the signatures the renderer uses to skip redundant uploads. */
		MaterialSignatureSet Bind(
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			const std::optional<MaterialSignatureSet>& p_previousMaterialSignature = std::nullopt
		);

		void Unbind(bool p_resetBoundProgram = false);

		/** Uploads the parameters whose signature changed. Textures are bound to their declared
		 * units here; the engine-global ones are bound per draw by the renderer instead. */
		void UploadProperties(
			bool uploadStableProperties,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			Rendering::HAL::Texture* p_flatNormalTexture = nullptr
		);

		/** Re-reads the property values of textures that were assigned by path. */
		void UpdateProperties();

		void SetProperty(const std::string p_name, const MaterialPropertyType& p_value);
		bool TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value);
		bool HasProperty(const std::string& p_name) const;
		bool ResetPropertyToDefault(const std::string& p_name);
		std::optional<MaterialProperty> GetProperty(const std::string p_name) const;

		/** Builds the name -> value view the material editor shows. This is a temporary view,
		 * not an instance-owned map: each value is read back from the uniform block or the
		 * descriptor set. */
		std::map<std::string, MaterialProperty> SnapshotProperties() const;

		/** Clears every parameter override, then re-seeds from the definition. */
		void ResetProperties();

		/** The texture/asset currently in a descriptor slot (used to inherit a material's
		 * defaults when a plain instance is created). */
		Rendering::HAL::TextureHandle* GetTexture(uint32_t p_binding) const
		{
			return m_descriptorSet.GetTexture(p_binding);
		}
		Rendering::Resources::Texture* GetTextureAsset(uint32_t p_binding) const
		{
			return m_descriptorSet.GetTextureAsset(p_binding);
		}
		const std::vector<uint8_t>& BufferData() const { return m_uniformData; }
		uint32_t BufferSize() const { return static_cast<uint32_t>(m_uniformData.size()); }

		void SetBlendable(bool p_blendable);
		void SetBlendMode(Rendering::Settings::EBlendMode p_blendMode);
		void SetTransparent(bool p_transparent);
		void SetBackfaceCulling(bool p_backfaceCulling);
		void SetFrontfaceCulling(bool p_frontfaceCulling);
		void SetPolygonOffsetFill(bool p_polygonOffsetFill);
		void SetDepthTest(bool p_depthTest);
		void SetDepthWriting(bool p_depthWriting);
		void SetColorWriting(bool p_colorWriting);
		void SetUserInterface(bool p_userInterface);
		void SetDrawOrder(int p_order);
		void SetGPUInstances(int p_instances);
		void SetCastShadows(bool p_castShadows);
		void SetReceiveShadows(bool p_receiveShadows);
		void SetCapturedByReflectionProbes(bool p_capturedBy);
		void SetReceiveReflections(bool p_receiveReflections);
		void SetLineWidth(float p_width);
		void SetOrthographicSupport(bool p_support);
		void SetPerspectiveSupport(bool p_support);

		/** Copies the whole render state (every state field, not the parameters or textures)
		 * from p_other. A .mat's per-drawable instance is created this way, so a state the asset
		 * configured cannot be dropped by a hand-maintained copy list. */
		void CopyStateFrom(const MaterialInstance& p_other);

		bool IsValid() const;
		bool IsBlendable() const;
		bool IsTransparent() const;
		bool IsUserInterface() const;
		bool HasBackfaceCulling() const;
		bool HasFrontfaceCulling() const;
		bool HasDepthTest() const;
		bool HasDepthWriting() const;
		bool HasColorWriting() const;
		bool IsShadowCaster() const;
		bool IsShadowReceiver() const;
		bool IsCapturedByReflectionProbes() const;
		bool IsReflectionReceiver() const;
		bool HasPolygonOffsetFill() const;
		// Defined out of line: the fallback needs Material to be complete, and Material derives from
		// this class (it is only forward declared here).
		int GetDrawOrder() const;
		int GetGPUInstances() const;
		float GetLineWidth() const;
		Rendering::Settings::EBlendMode GetBlendMode() const;
		bool SupportsOrthographic() const;
		bool SupportsPerspective() const;
		bool SupportsProjectionMode(Rendering::Settings::EProjectionMode p_mode) const;

		/** Whether the definition's shader was compiled with this pass (the picking / shadow
		 * passes ask this through the instance the drawable holds). */
		bool HasPass(const std::string& p_pass) const;

		// Filament keeps features on the material; here an instance may need a different combination
		// (that used to be Drawable::featureSetOverride), so the set lives on the instance and a plain
		// instance starts from the definition's.
		void SetFeatures(const Data::FeatureSet& p_features);
		void AddFeature(const std::string& p_feature);
		void RemoveFeature(const std::string& p_feature);
		void EnableFeature(const std::string& feature, bool flag);
		bool HasFeature(const std::string& p_feature) const;
		bool SupportsFeature(const std::string& p_feature) const;
		const Data::FeatureSet& GetFeatures() const { return m_features; }

		const Rendering::Data::StateMask GenerateStateMask() const;

		/** Every parameter of the definition, in declaration order (the instance's override is
		 * reported when it has one). */
		Rendering::HAL::Texture* TextureOf(const std::string& p_name) const;

	protected:
		struct LooseUniform;
		/** The feature set this instance draws with: its own, seeded from the definition. */
		const Data::FeatureSet& FeaturesOf() const { return m_features; }

		void InvalidatePropertySignature();

		void BindParameterBlock(const Rendering::HAL::ShaderProgram& p_program);
		void UploadParameterBlock(
			bool p_uploadStableProperties,
			Rendering::HAL::Texture* p_emptyTexture2D,
			Rendering::HAL::Texture* p_emptyTextureCube,
			Rendering::HAL::Texture* p_flatNormalTexture
		);
		void EnsureDescriptorSet() const;
		void ReleaseDescriptorSet();
		void ResetBlockToDefinition();
		void InitializeDefaultState();
		LooseUniform* FindLooseUniform(const std::string& p_name);
		const LooseUniform* FindLooseUniform(const std::string& p_name) const;
		void SetLooseUniform(const std::string& p_name, const MaterialPropertyType& p_value);
		MaterialSignatureSet CalculateSignature(
			Rendering::HAL::ShaderProgram& p_selectedProgram,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr
		);

	protected:
		Material* m_material = nullptr;
		bool m_isDefaultInstance = false;
		Data::FeatureSet m_features;
		uint64_t m_revision = 0;
		size_t m_stablePropertySignatureVersion = 0ULL;
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> m_programInUse = std::nullopt;

		/** The PerMaterial binding set: one uniform-block range plus a texture per sampler. The
		 * definition's layout validates every insertion; Bind() binds the whole set. */
		mutable DescriptorSet m_descriptorSet;
		/** CPU side of the PerMaterial UBO: SetProperty writes here by std140 offset; upload
		 * happens once per dirty commit into the descriptor's GL buffer range. */
		mutable std::vector<uint8_t> m_uniformData;
		mutable UniformBufferPool::Slice m_uniformSlice;
		struct LooseUniform
		{
			std::string name;
			MaterialPropertyType value;
		};
		/** Source-built shaders have no package block; these uniforms are uploaded through the
		 * program's reflected uniform locations instead of a UBO range. */
		std::vector<LooseUniform> m_looseUniforms;

		// Render state, Filament's way: the definition holds the values its material block declared
		// (and the engine defaults for the rest); a plain instance stores only the fields it
		// overrides and answers everything else from its definition. An empty optional means "not
		// overridden here", so a definition that changes later is seen by the instances that did
		// not pin that field - a shader reload no longer leaves stale state behind.
		std::optional<bool> m_supportOrthographic;
		std::optional<bool> m_supportPerspective;
		std::optional<bool> m_userInterface;
		std::optional<bool> m_blendable;
		std::optional<Rendering::Settings::EBlendMode> m_blendMode;
		std::optional<bool> m_transparent;
		std::optional<bool> m_backfaceCulling;
		std::optional<bool> m_frontfaceCulling;
		std::optional<bool> m_polygonOffsetFill;
		std::optional<bool> m_depthTest;
		std::optional<bool> m_depthWriting;
		std::optional<bool> m_colorWriting;
		std::optional<bool> m_castShadows;
		std::optional<bool> m_receiveShadows;
		std::optional<bool> m_capturedByReflectionProbes;
		std::optional<bool> m_receiveReflections;
		std::optional<int> m_gpuInstances;
		std::optional<int> m_drawOrder;
		std::optional<float> m_lineWidth;
	};
}
