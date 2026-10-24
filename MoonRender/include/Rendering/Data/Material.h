#pragma once
#include <memory>
#include <string>
#include <vector>
#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Resources/Shader.h>

namespace Rendering::Data
{
	/** The per-material binding point lives with the other engine blocks (see
	 * Rendering/Settings/EBindingPoint.h). */
	using ::Rendering::Settings::kMaterialParameterBinding;

	/** The material definition: one compiled shader, the parameter defaults its file declared, and
	 * the render state the `material { }` block asks for.
	 *
	 * It is also its own default instance - `Material` derives from `MaterialInstance` with a null
	 * definition pointer, so every existing "hold a Material& and draw with it" call site keeps
	 * working, and `CreateInstance()` gives a lighter object for the cases that only need different
	 * parameter values or a different culling/depth state (Filament's MaterialInstance).
	 */
	class Material : public MaterialInstance
	{
	public:
		using PropertyMap = MaterialInstance::PropertyMap;

		Material(Rendering::Resources::Shader* p_shader = nullptr);
		/** Gives the parameter slice back to the pool. No GL call. */
		~Material() override;
		Material(const Material&) = delete;
		Material& operator=(const Material&) = delete;

		void SetShader(Rendering::Resources::Shader* p_shader);
		bool HasShader() const;
		Rendering::Resources::Shader*& GetShader() { return m_shader; }

		/** An override-only instance of this material: it answers from this definition for
		 * everything it does not override itself. */
		std::unique_ptr<MaterialInstance> CreateInstance();

		void SetFeatures(const Data::FeatureSet& p_features);
		void AddFeature(const std::string& p_feature);
		void RemoveFeature(const std::string& p_feature);
		void EnableFeature(const std::string& feature, bool flag);
		bool HasFeature(const std::string& p_feature) const;
		bool SupportsFeature(const std::string& p_feature) const;
		bool HasPass(const std::string& p_pass) const;
		const Data::FeatureSet& GetFeatures() const { return m_features; }

		void LateUpdateTexture(const std::string& p_name, const std::string& p_path);

		/** Applies the shader's material block (blending / culling / depth writing / the support
		 * flags) to this definition. The Set* calls that follow are the definition's own choice. */
		void ResetStateToShader();

	protected:
		Rendering::Resources::Shader* ShaderOf() const override { return m_shader; }
		const Data::FeatureSet& FeaturesOf() const override { return m_features; }

		// The engine-side material asset (Core::Resources::Material) derives from this class and
		// still writes the shader / feature set directly, so they stay protected rather than
		// private - that is the split's boundary: the definition's own data.
		Rendering::Resources::Shader* m_shader = nullptr;
		Data::FeatureSet m_features;
	};
}
