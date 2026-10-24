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
		int GetDrawOrder() const { return m_drawOrder; }
		int GetGPUInstances() const { return m_gpuInstances; }
		float GetLineWidth() const { return m_lineWidth; }
		Rendering::Settings::EBlendMode GetBlendMode() const { return m_blendMode; }
		bool SupportsOrthographic() const { return m_supportOrthographic; }
		bool SupportsPerspective() const { return m_supportPerspective; }
		bool SupportsProjectionMode(Rendering::Settings::EProjectionMode p_mode) const;

		const Rendering::Data::StateMask GenerateStateMask() const;

		/** Every parameter of the definition, in declaration order (the instance's override is
		 * reported when it has one). */
		Rendering::HAL::Texture* TextureOf(const std::string& p_name) const;

	protected:
		/** A Material is its own default instance: it has no separate definition to forward to. */
		MaterialInstance() = default;

		/** The definition-side lookup. A Material overrides this with its own defaults; a plain
		 * instance forwards to its definition. */
		virtual const MaterialProperty* FindProperty(const std::string& p_name) const;
		/** The definition's shader (the one the variant comes from). */
		virtual Rendering::Resources::Shader* ShaderOf() const;
		/** The definition's feature set (the instance can still be drawn with an override). */
		virtual const Data::FeatureSet& FeaturesOf() const;

		/** Called by a Material after (re)binding to a shader: copies the definition's render
		 * state into this instance so the instance starts from it. */
		void CopyStateFrom(const MaterialInstance& p_source);
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
		size_t m_stablePropertySignatureVersion = 0ULL;
		size_t m_singleUsePropertySignatureVersion = 0ULL;
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> m_programInUse = std::nullopt;

		UniformBufferPool::Slice m_parameterSlice;
		std::vector<uint8_t> m_parameterData;

		bool m_supportOrthographic = true;
		bool m_supportPerspective = true;
		bool m_userInterface = false;
		bool m_blendable = false;
		Rendering::Settings::EBlendMode m_blendMode = Rendering::Settings::EBlendMode::BLEND_OPAQUE;
		bool m_transparent = false;
		bool m_backfaceCulling = true;
		bool m_frontfaceCulling = false;
		bool m_polygonOffsetFill = false;
		bool m_depthTest = true;
		bool m_depthWriting = true;
		bool m_colorWriting = true;
		bool m_castShadows = true;
		bool m_receiveShadows = true;
		bool m_capturedByReflectionProbes = true;
		bool m_receiveReflections = true;
		int m_gpuInstances = 1;
		int m_drawOrder = 1000;
		float m_lineWidth = 1.0f;
	};
}
