#pragma once
#include <map>
#include <cstdint>
#include <Rendering/Core/CompositeRenderer.h>
#include <Core/Rendering/HzbCuller.h>
#include <Rendering/Data/Frustum.h>
#include <Rendering/Entities/Drawable.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/HAL/ShaderStorageBuffer.h>
#include <Rendering/Resources/Mesh.h>

#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CCamera.h>
#include <Core/Rendering/EVisibilityFlags.h>
#include <Core/Resources/Material.h>
#include <Core/SceneSystem/Scene.h>

namespace Core::Rendering
{

	class SceneRenderer : public ::Rendering::Core::CompositeRenderer
	{
	public:
		enum class EOrderingMode
		{
			BACK_TO_FRONT,
			FRONT_TO_BACK,
		};

		template<EOrderingMode OrderingMode,bool BatchMaterial>
		struct DrawOrder
		{
			const int order;
			const uintptr_t materialKey;
			const float distance;
			bool operator<(const DrawOrder& p_other) const
			{
				if (order == p_other.order)
				{
					if constexpr (BatchMaterial)
					{
						if (materialKey != p_other.materialKey)
						{
							return materialKey < p_other.materialKey;
						}
					}

					if constexpr (OrderingMode == EOrderingMode::BACK_TO_FRONT)
					{
						return distance > p_other.distance;
					}
					else
					{
						return distance < p_other.distance;
					}
				}
				else
				{
					return order < p_other.order;
				}
			}
		};

		template<EOrderingMode OrderingMode, bool BatchMaterial = false>
		using DrawableMap = std::multimap<DrawOrder<OrderingMode, BatchMaterial>, ::Rendering::Entities::Drawable>;


		struct SceneDescriptor
		{
			Core::SceneSystem::Scene& scene;
			Tools::Utils::OptRef<const ::Rendering::Data::Frustum> frustumOverride;
			Tools::Utils::OptRef<::Rendering::Data::MaterialInstance> overrideMaterial;
			Tools::Utils::OptRef<::Rendering::Data::MaterialInstance> fallbackMaterial;
		};

		struct SceneParsingInput
		{
			Core::SceneSystem::Scene& scene;
		};

		struct SceneDrawablesDescriptor
		{
			std::vector<::Rendering::Entities::Drawable> drawables;
		};

		/** Per frame handle to the cached parsed drawables.
		 *
		 * Parsing builds one Drawable per mesh sub-range and every Drawable owns
		 * heap backed descriptor storage, so re-parsing 20k+ drawables each frame
		 * is dominated by allocation traffic. The parsed list is therefore cached
		 * in the renderer and only rebuilt when the scene content changes (see
		 * UpdateParsedDrawables); on a hit the per frame descriptor is just this
		 * pointer, so nothing is copied or allocated.
		 */
		struct SceneDrawablesHandle
		{
			const SceneDrawablesDescriptor* drawables = nullptr;
		};


		struct SceneDrawableDescriptor
		{
			Core::ECS::Actor& actor;
			EVisibilityFlags visibilityFlags = EVisibilityFlags::NONE;
			std::optional<::Rendering::Geometry::BoundingSphere> bounds;
			/** Source mesh of this drawable (used to match HZB occlusion results). */
			const ::Rendering::Resources::Mesh* sourceMesh = nullptr;
		};


		struct SceneFilteredDrawablesDescriptor
		{
			DrawableMap<EOrderingMode::FRONT_TO_BACK, true> opaques;
			DrawableMap<EOrderingMode::BACK_TO_FRONT> transparents;
			DrawableMap<EOrderingMode::FRONT_TO_BACK, true> lines;
			DrawableMap<EOrderingMode::BACK_TO_FRONT> ui;
		};

		struct SceneDrawablesFilteringInput
		{
			const ::Rendering::Entities::Camera& camera;
			Tools::Utils::OptRef<const ::Rendering::Data::Frustum> frustumOverride;
			Tools::Utils::OptRef<::Rendering::Data::MaterialInstance> overrideMaterial;
			Tools::Utils::OptRef<::Rendering::Data::MaterialInstance> fallbackMaterial;
			EVisibilityFlags requiredVisibilityFlags = EVisibilityFlags::NONE;
			bool includeUI = true; // Whether to include UI drawables in the filtering
			bool includeTransparent = true; // Whether to include transparent drawables in the filtering
			bool includeOpaque = true; // Whether to include opaque drawables in the filtering
		};


		SceneRenderer(::Rendering::Context::Driver& p_driver, bool p_stencilWrite = false);

		virtual void BeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor) override;


		virtual void DrawModelWithSingleMaterial(
			::Rendering::Data::PipelineState p_pso,
			::Rendering::Resources::Model& p_model,
			::Rendering::Data::Material& p_material,
			const Maths::FMatrix4& p_modelMatrix
		);

		void Resize(int width,int height);

		/** Features stripped from every parsed drawable, for a view that must not evaluate
		 * scene-space effects. The material preview removes DISTANCE_FADE: the preview's camera
		 * sits inside the fade range, and the shader dithers the surface into a dot pattern. */
		void SetExcludedFeatures(::Rendering::Data::FeatureSet p_features);

		SceneDrawablesDescriptor ParseScene(
			const SceneParsingInput& p_input
		);

		/** Rebuilds the cached parsed drawables when the scene content changed.
		 *
		 * The signature covers everything ParseScene reads: actor set/active
		 * flags, transforms, models and their meshes (material indices, sub
		 * ranges, index counts, primitive modes, bounds) and material renderer
		 * state (materials, visibility flags, user matrix).
		 */
		void UpdateParsedDrawables(Core::SceneSystem::Scene& p_scene);


		SceneFilteredDrawablesDescriptor FilterDrawables(
			const SceneDrawablesDescriptor& p_drawables,
			const SceneDrawablesFilteringInput& p_filteringInput
		);

		/** Hierarchical Z-buffer occlusion culler (filled by the HZB pass). */
		HzbCuller& GetHzbCuller() { return m_hzbCuller; }
		const HzbCuller& GetHzbCuller() const { return m_hzbCuller; }
		const HzbStats& GetHzbStats() const { return m_hzbCuller.GetStats(); }

		/** Number of drawables the HZB culler removed from the last filtered list. */
		uint32_t GetHzbSkippedDrawables() const { return m_hzbSkippedDrawables; }

	private:
		HzbCuller m_hzbCuller;
		
		uint32_t m_hzbSkippedDrawables = 0;

		/** Cached ParseScene output, see UpdateParsedDrawables. */
		SceneDrawablesDescriptor m_parsedDrawables;
		uint64_t m_parsedDrawablesHash = 0;
		bool m_parsedDrawablesValid = false;
		size_t m_lastParsedDrawableCount = 0;
		::Rendering::Data::FeatureSet m_excludedFeatures;
	};
}
