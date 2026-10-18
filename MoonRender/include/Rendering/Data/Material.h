#pragma once
#include <any>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include <Maths/FMatrix3.h>
#include <Rendering/Data/StateMask.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/Resources/Shader.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Settings/EProjectionMode.h>

namespace Rendering::Data
{
	/** GL uniform-buffer binding point of the per-material parameter block: filament's
	 * PER_MATERIAL descriptor set, whose first binding is the material's uniform buffer. MoonMatc
	 * generates `layout(std140, binding = 2)` for the block; keep the two in sync. */
	constexpr uint32_t kMaterialParameterBinding = 2;

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
		bool singleUse;
	};
	struct MaterialSignatureSet
	{
		std::size_t bindSignature;
		std::size_t stablePropertySignature;
		std::size_t singleUsePropertySignature;
	};

	class Material
	{
	public:
		using PropertyMap = std::map<std::string, MaterialProperty>;

		Material(Rendering::Resources::Shader* p_shader = nullptr);
		void SetShader(Rendering::Resources::Shader* p_shader);
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> GetVariant(
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_override = std::nullopt
		) const;
		
		void UploadProperties(
			bool uploadStableProperties,
			bool uploadSingleUseProperties,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr
		);
		void UpdateProperties();
		MaterialSignatureSet Bind(
			HAL::Texture* p_emptyTexture2D = nullptr,
			HAL::Texture* p_emptyTextureCube = nullptr,
			std::optional<const std::string_view> p_pass = std::nullopt,
			Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			std::optional<MaterialSignatureSet> p_previousMaterialSignature = std::nullopt
		);
		void Unbind(bool p_resetBoundProgram = false) ;

		bool HasProperty(const std::string& p_name) const;
		void SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);
		void LateUpdateTexture(const std::string &p_name,const std::string& p_path);
		bool TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);
		Tools::Utils::OptRef<const MaterialProperty> GetProperty(const std::string p_name) const;
		Rendering::Resources::Shader*& GetShader();
		bool HasShader() const;
		bool IsValid() const;
		void SetOrthographicSupport(bool p_supportOrthographic);
		void SetPerspectiveSupport(bool p_supportPerspective);
		void SetDrawOrder(int p_order);
		void SetBlendable(bool p_blendable);
		void SetUserInterface(bool p_userInterface);
		void SetBackfaceCulling(bool p_backfaceCulling);
		void SetFrontfaceCulling(bool p_frontfaceCulling);
		void SetPolygonOffsetFill(bool p_polygonOffsetFill);
		void SetDepthTest(bool p_depthTest);
		void SetDepthWriting(bool p_depthWriting);
		void SetColorWriting(bool p_colorWriting);
		void SetCastShadows(bool p_castShadows);
		void SetReceiveShadows(bool p_receiveShadows);
		void SetCapturedByReflectionProbes(bool p_capturedByReflectionProbes);
		void SetReceiveReflections(bool p_receiveReflections);
		void SetGPUInstances(int p_instances);
		int GetDrawOrder() const;
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
		int GetGPUInstances() const;
		const StateMask GenerateStateMask() const;
		PropertyMap& GetProperties();
		Data::FeatureSet& GetFeatures();
		void SetFeatures(const Data::FeatureSet& p_features);
		void AddFeature(const std::string& p_feature);
		void RemoveFeature(const std::string& p_feature);
		void EnableFeature(const std::string& feature,bool flag);
		bool HasFeature(const std::string& p_feature) const;
		bool SupportsFeature(const std::string& p_feature) const;
		bool HasPass(const std::string& p_pass) const;
		bool SupportsOrthographic() const;
		bool SupportsPerspective() const;
		bool SupportsProjectionMode(Rendering::Settings::EProjectionMode p_projectionMode) const;
		void SetLineWidth(float p_width);
		void SetTransparent(bool transparent);
	protected:
		void InvalidatePropertySignature();

		/** Sets the definition-owned render state (blending, culling, depth writing) from the
		 * shader's material block, or from the engine's defaults when the shader came from
		 * source. Called when a material (re)binds to a shader, so the instance starts from the
		 * definition and the Set* calls that follow are the instance's overrides. */
		void ResetStateToShader();

		/** Binds the per-material parameter UBO (creating it on first use). Called by Bind on
		 * every draw: the binding is global GL state, so it has to be set again after another
		 * material bound its own buffer. */
		void BindParameterBlock();

		/** Fills the std140 block from the properties, uploads it, binds it and binds the
		 * samplers to their declared texture units. Used instead of the per-name upload when the
		 * shader has a parameter block. */
		void UploadParameterBlock(
			bool p_uploadStableProperties,
			bool p_uploadSingleUseProperties,
			Rendering::HAL::Texture* p_emptyTexture2D,
			Rendering::HAL::Texture* p_emptyTextureCube
		);

		/** Creates or resizes the parameter buffer to the shader's block size. */
		void EnsureParameterBuffer();
		/** Writes one value (a scalar, vector or matrix - not a whole array) at p_offset. */
		void WriteBlockValue(const std::string& p_type, const MaterialPropertyType& p_value, uint32_t p_offset);

		MaterialSignatureSet CalculateSignature(
			Rendering::HAL::ShaderProgram& p_selectedProgram,
			Rendering::HAL::Texture* p_emptyTexture2D = nullptr,
			Rendering::HAL::Texture* p_emptyTextureCube = nullptr
		);

	protected:
		Rendering::Resources::Shader* m_shader = nullptr;
		std::map<std::string, std::string>lateLoadTextures;
		PropertyMap m_properties;
		Data::FeatureSet m_features;
		size_t m_stablePropertySignatureVersion = 0ULL;
		size_t m_singleUsePropertySignatureVersion = 0ULL;
		Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> m_programInUse = std::nullopt;

		/** The per-material descriptor: one std140 UBO filled from the properties by offset, and
		 * the CPU staging buffer it is built in. Null until the shader has a parameter block. */
		std::unique_ptr<Rendering::HAL::UniformBuffer> m_parameterBuffer;
		uint32_t m_parameterBufferSize = 0;
		std::vector<uint8_t> m_parameterData;

		bool m_supportOrthographic = true;
		bool m_supportPerspective = true;
		bool m_userInterface = false;

		bool m_blendable = false;
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
		float lineWitdh = 1.0f;
	};
}
