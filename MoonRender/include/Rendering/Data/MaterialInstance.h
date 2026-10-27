#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <Maths/FMatrix3.h>
#include <Rendering/Data/StateMask.h>
#include <Rendering/Data/UniformBufferPool.h>
#include <Rendering/Data/FeatureSet.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/Shader.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Settings/EBindingPoint.h>
#include <Rendering/Settings/EProjectionMode.h>

namespace Rendering::Data
{
	class Material;
	class MaterialInstance;

	/** The value type a material parameter can carry. Shared by the definition (the defaults the
	 * material file or the DSL declares) and the instances (the overrides). */
	using MaterialPropertyType = std::variant<
		std::monostate,
		bool,
		int,
		float,
		Maths::FVector2,
		Maths::FVector3,
		Maths::FVector4,
		Maths::FMatrix3,
		Maths::FMatrix4,
		Rendering::HAL::TextureHandle*,	// Texture handle
		Rendering::Resources::Texture*	// Texture asset (serializable)
	>;

	struct MaterialProperty
	{
		MaterialPropertyType value;
		bool singleUse = false;
	};

	struct MaterialSignatureSet
	{
		std::size_t bindSignature = 0;
		std::size_t stablePropertySignature = 0;
		std::size_t singleUsePropertySignature = 0;
	};

	/** A bindable set of parameter and state overrides for one Material definition.
	 *
	 * Filament splits the two: `Material` is the compiled program plus its defaults, and
	 * `MaterialInstance` is what a drawable actually draws with - it can override every parameter
	 * and a handful of render states (culling, depth write, polygon offset, ...). We had one class
	 * doing both, which meant every actor that needed a different `domainColorTex` or line width
	 * had to own a full material (its own shader pointer, its own feature set, its own state).
	 *
	 * A Material is its own default instance (m_definition == nullptr means "I am the definition"),
	 * which is Filament's getDefaultInstance() and what keeps the existing call sites - they all
	 * hold a Material& - compiling and behaving exactly as before.
	 */
	class MaterialInstance
	{
	public:
		using PropertyMap = std::map<std::string, MaterialProperty>;

		explicit MaterialInstance(Material& p_definition);
		virtual ~MaterialInstance();

		MaterialInstance(const MaterialInstance&) = delete;
		MaterialInstance& operator=(const MaterialInstance&) = delete;

		/** The definition this instance overrides. Null for a Material (it is its own definition). */
		Material* Definition() const { return m_definition; }

		/** Bumped whenever this object's parameter values change. An instance compares it against
		 * the value it last uploaded, which is how an edit on the definition reaches the instances
		 * that inherit from it (they re-upload instead of using stale data). */
		uint64_t Revision() const { return m_revision; }

		/** The definition's shader and feature set: what a variant is selected from. A drawable
		 * holds an instance now, so the renderer asks these two questions through this pair
		 * instead of through Material. */
		Rendering::Resources::Shader* Shader() const { return ShaderOf(); }
		Rendering::Resources::Shader* GetShader() const { return ShaderOf(); }

		/** Re-reads a texture assigned by path (the designer saves a path, not a handle). */
		void LateUpdateTexture(const std::string& p_name, const std::string& p_path);
		const Data::FeatureSet& Features() const { return FeaturesOf(); }

		/** The program for a pass and a feature set, resolved through the definition's shader. */
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> GetVariant(
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_override = std::nullopt) const;

		/** Binds the program for p_pass / the drawable's feature set and the instance's parameter
		 * block. Returns the signatures the renderer uses to skip redundant uploads. */
		MaterialSignatureSet Bind(
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			std::optional<MaterialSignatureSet> p_previousMaterialSignature = std::nullopt
		);

		void Unbind(bool p_resetBoundProgram = false);

		/** Uploads the parameters whose signature changed. Textures are bound to their declared
		 * units here; the engine-global ones are bound per draw by the renderer instead. */
		void UploadProperties(
			bool uploadStableProperties,
			bool uploadSingleUseProperties,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr,
			Rendering::HAL::Texture* p_brdfLut = nullptr,
			Rendering::HAL::Texture* p_flatNormalTexture = nullptr
		);

		/** Re-reads the property values of textures that were assigned by path. */
		void UpdateProperties();

		void SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);
		bool TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);
		bool HasProperty(const std::string& p_name) const;
		bool ResetPropertyToDefault(const std::string& p_name);
		Tools::Utils::OptRef<const MaterialProperty> GetProperty(const std::string p_name) const;
		PropertyMap& GetProperties() { return m_properties; }
		const PropertyMap& Properties() const { return m_properties; }

		/** Binds a view-owned texture (DFG LUT, environment cubes) for the next draw. */
		void BindGlobalTexture(const std::string& p_name, Rendering::HAL::Texture* p_texture);

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
		/** A Material is its own default instance: it has no separate definition to forward to, and
		 * the values it holds are what a plain instance falls back to. */
		MaterialInstance();

		/** The definition-side lookup. A Material overrides this with its own defaults; a plain
		 * instance forwards to its definition. */
		virtual const MaterialProperty* FindProperty(const std::string& p_name) const;
		/** The definition's shader (the one the variant comes from). */
		virtual Rendering::Resources::Shader* ShaderOf() const;
		/** The feature set this instance draws with: its own, seeded from the definition. */
		const Data::FeatureSet& FeaturesOf() const { return m_features; }

		void InvalidatePropertySignature();

		void BindParameterBlock();
		void UploadParameterBlock(
			bool p_uploadStableProperties,
			bool p_uploadSingleUseProperties,
			Rendering::HAL::Texture* p_emptyTexture2D,
			Rendering::HAL::Texture* p_emptyTextureCube,
			Rendering::HAL::Texture* p_brdfLut,
			Rendering::HAL::Texture* p_flatNormalTexture
		);
		void EnsureParameterSlice();
		void ReleaseParameterSlice();
		void WriteBlockValue(const std::string& p_type, const MaterialPropertyType& p_value, uint32_t p_offset);
		MaterialSignatureSet CalculateSignature(
			Rendering::HAL::ShaderProgram& p_selectedProgram,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr
		);

	protected:
		Material* m_definition = nullptr;
		std::map<std::string, std::string> m_lateLoadTextures;
		PropertyMap m_properties;
		Data::FeatureSet m_features;
		uint64_t m_revision = 0;
		uint64_t m_seenDefinitionRevision = 0;
		size_t m_stablePropertySignatureVersion = 0ULL;
		size_t m_singleUsePropertySignatureVersion = 0ULL;
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> m_programInUse = std::nullopt;

		UniformBufferPool::Slice m_parameterSlice;
		std::vector<uint8_t> m_parameterData;

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
