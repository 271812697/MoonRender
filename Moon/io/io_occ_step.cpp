#include <tracy/Tracy.hpp>
#include "TopoShape.h"
#include "Core/SceneSystem/Scene.h"
#include "feature/Feature.h"
#include "core/component/CTopoShape.h"
#include "core/JobSystem.h"
#include "io_occ_step.h"
#include <Core/ResourceManagement/ShaderManager.h>

#include "core/Global/ServiceLocator.h"
#include "editor/View/sceneview/viewerwidget.h"
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <renderer/SceneView.h>
namespace MOON {
    // ?? STEP ????????
    namespace IO {
        struct LoadedDomain {

			std::vector<::Rendering::Geometry::VertexPositionNormal> vertex;
            std::vector<uint32_t> indices;
        };
		static bool LoadDomainsFromFile(const std::string& path, std::vector<LoadedDomain>& out)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in.is_open()) return false;

			uint64_t domainCount = 0;
			in.read(reinterpret_cast<char*>(&domainCount), sizeof(domainCount));
			if (!in) return false;
			out.reserve((size_t)domainCount);

			for (uint64_t d = 0; d < domainCount; d++) {
				uint64_t ptSize = 0, nmlSize = 0, triSize = 0;
				in.read(reinterpret_cast<char*>(&ptSize), sizeof(ptSize));
				in.read(reinterpret_cast<char*>(&nmlSize), sizeof(nmlSize));
				in.read(reinterpret_cast<char*>(&triSize), sizeof(triSize));
				if (!in) return false;
				if (ptSize == 0) {
					std::cout << "[Mesh] domain " << d << " has no vertex, file may be corrupted: "
						<< path << std::endl;
					return false;
				}

				LoadedDomain dom;
				dom.vertex.resize((size_t)ptSize);
				dom.indices.resize((size_t)triSize * 3);

				// ?????? float3,????? read
				std::vector<double> raw((size_t)(ptSize + nmlSize) * 3);
				in.read(reinterpret_cast<char*>(raw.data()),
					(std::streamsize)(raw.size() * sizeof(double)));
				if (!in) return false;

				for (size_t i = 0; i < ptSize; i++) {
					dom.vertex[i].position = {
						(float)raw[i * 3 + 0], (float)raw[i * 3 + 1], (float)raw[i * 3 + 2]
					};
				}

				in.read(reinterpret_cast<char*>(dom.indices.data()),
					(std::streamsize)(dom.indices.size() * sizeof(uint32_t)));
				if (!in) return false;

				// ??????:????? GPU ????????,????????,
				// ?????????????(????,????????)?
				{
					std::vector<uint32_t> safeIndices;
					safeIndices.reserve(dom.indices.size());
					for (size_t t = 0; t + 2 < dom.indices.size(); t += 3) {
						const uint32_t i0 = dom.indices[t];
						const uint32_t i1 = dom.indices[t + 1];
						const uint32_t i2 = dom.indices[t + 2];
						if (i0 >= ptSize || i1 >= ptSize || i2 >= ptSize) {
							continue;
						}
						safeIndices.push_back(i0);
						safeIndices.push_back(i1);
						safeIndices.push_back(i2);
					}
					if (safeIndices.size() != dom.indices.size()) {
						std::cout << "[Mesh] domain " << d << ": dropped "
							<< (dom.indices.size() - safeIndices.size()) / 3
							<< " out-of-range triangle(s)" << std::endl;
						dom.indices.swap(safeIndices);
					}
				}

				// ??:?? nmlSize == ptSize ?????????????
				// ????(???????)???????????,???????
				// (0,0,0),? Standard.mat ? normalize(0) ??? NaN,???????
				if (nmlSize == ptSize) {
					for (size_t i = 0; i < ptSize; i++) {
						dom.vertex[i].normals = {
							(float)raw[(ptSize + i) * 3 + 0],
							(float)raw[(ptSize + i) * 3 + 1],
							(float)raw[(ptSize + i) * 3 + 2]
						};
					}
				}
				else {
					std::cout << "[Mesh] domain " << d << ": normals rebuilt from faces (nmlSize="
						<< nmlSize << ", vertexCount=" << ptSize << ")" << std::endl;

					std::vector<Maths::FVector3> accumulated(
						(size_t)ptSize,
						Maths::FVector3{ 0.0f, 0.0f, 0.0f }
					);
					for (size_t t = 0; t + 2 < dom.indices.size(); t += 3) {
						const uint32_t i0 = dom.indices[t];
						const uint32_t i1 = dom.indices[t + 1];
						const uint32_t i2 = dom.indices[t + 2];
						if (i0 >= ptSize || i1 >= ptSize || i2 >= ptSize) {
							continue;
						}
						const Maths::FVector3 e1
							= dom.vertex[i1].position - dom.vertex[i0].position;
						const Maths::FVector3 e2
							= dom.vertex[i2].position - dom.vertex[i0].position;
						const Maths::FVector3 faceNormal = Maths::FVector3::Cross(e1, e2);
						accumulated[i0] += faceNormal;
						accumulated[i1] += faceNormal;
						accumulated[i2] += faceNormal;
					}
					for (size_t i = 0; i < ptSize; i++) {
						dom.vertex[i].normals = accumulated[i].Length() > 1e-20f
							? Maths::FVector3::Normalize(accumulated[i])
							: Maths::FVector3{ 0.0f, 0.0f, 1.0f };
					}
				}
				out.push_back(std::move(dom));
			}
			return true;
		}

        void ReadSTEP(const char* filePath, Core::SceneSystem::Scene* scene) {
            auto topoActor = new Feature3D("Feature", "Feature");
            // Where the shape came from is what a document writes for it: a shape on its
            // own is not something a document can hold, so the file is kept on the
            // feature and it is read again when the document is opened.
            topoActor->SetSourcePath(filePath);
            Core::ECS::Components::CTopoShape* topoComp=topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
            static MOON::System::JobSystem::Context ctx;
            static std::string path;
            path = filePath;
	        auto lamda=[=](JobDispatchArgs arg) { 
                ZoneScopedN("ReadSTEP");
                Part::TopoShape& topo = topoComp->GetTopoShape();
                topo.importStep(path.c_str());
                topoActor->makeDone();
		    };
	        MOON::System::JobSystem::Execute(ctx,lamda);
        }
        void IO::ReadSTEPBin(const char* filePath, Core::SceneSystem::Scene* scene)
        {
			std::string  path(filePath);
			std::vector<LoadedDomain> domains;
			if (!LoadDomainsFromFile(path, domains) || domains.empty()) {
				std::cout << "[Mesh] ????: " << path << std::endl;
				return;
			}
			auto&parent=scene->CreateActor();
			GetViewerWidget.addActorToTreeView(&parent);
			if (domains.size() > 0) {
				auto tempMat = new Core::Resources::Material();


				tempMat->SetShader(GetShaderService[":Shaders\\Standard.mat"]);
				// Overrides after SetShader: the state now starts from the material block.
				tempMat->SetBackfaceCulling(false);
				tempMat->SetCastShadows(false);
				tempMat->SetReceiveShadows(false);
				tempMat->AddFeature("VERTEX_POS_NORMAL");
				// IBL: the three pieces Filament's standard model reads.
				tempMat->SetProperty("_IrradianceCube", GetSceneView.GetRenderer().GetIrradianceCube());
				tempMat->SetProperty("_PrefilterCube", GetSceneView.GetRenderer().GetPrefilterCube());
				tempMat->SetProperty("_BRDFLut", GetSceneView.GetRenderer().GetBrdfTexture());
				tempMat->SetProperty("u_Albedo", Maths::FVector4{ 1.0, 1.0, 1.0, 1.0 });

				tempMat->SetProperty("u_AlphaClippingThreshold", 1.0f);
				tempMat->SetProperty("u_Roughness", 0.3f);
				tempMat->SetProperty("u_Metallic", 0.8f);
				// Emission
				tempMat->SetProperty("u_EmissiveIntensity", 1.0f);
				tempMat->SetProperty("u_EmissiveColor", Maths::FVector3{ 0.0f,0.0f,0.0f });


				for (int i = 0;i < domains.size();i++) {
					auto& actor = scene->CreateActor("Domain_" + std::to_string(i));
					actor.SetParent(parent);
					auto faceModel = new ::Rendering::Resources::Model(std::string("_faceModel") + std::to_string(i));

					auto& modelRender = actor.AddComponent<Core::ECS::Components::CModelRenderer>();
					modelRender.SetModel(faceModel);
					auto mesh = new ::Rendering::Resources::Mesh(domains[i].vertex,
						domains[i].indices,
						0,
						::Rendering::Settings::EPrimitiveMode::TRIANGLES);

					modelRender.GetModel()->AddMesh(mesh);
					modelRender.GetModel()->computeBoxAndShpere();

					auto& matRender = actor.AddComponent<::Core::ECS::Components::CMaterialRenderer>();
					matRender.SetMaterialAtIndex(0, *tempMat);
					matRender.UpdateMaterialList();
					GetViewerWidget.addActorToTreeView(&actor);

				}
			}

        }
    }
}
