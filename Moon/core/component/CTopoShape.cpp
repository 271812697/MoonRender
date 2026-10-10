#include <tinyxml2.h>
#include <tracy/Tracy.hpp>
#include <Core/ECS/Actor.h>
#include <map>
#include <algorithm>
#include <cmath>
#include "core/component/CTopoShape.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include "TopoShape.h"
#include "Core/ECS/Components/CBatchMeshTriangle.h"
#include "Core/ECS/Components/CBatchMeshLine.h"
#include "Core/ResourceManagement/ModelManager.h"
#include "Interactive/Im3DRenderer.h"

#include "renderer/SceneView.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "core/JobSystem.h"
#include "Tools.h"
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>

namespace
{
	Maths::FVector3 HsvToRgb(float p_hue, float p_saturation, float p_value)
	{
		const int i = static_cast<int>(p_hue * 6.0f);
		const float f = p_hue * 6.0f - i;
		const float p = p_value * (1.0f - p_saturation);
		const float q = p_value * (1.0f - f * p_saturation);
		const float t = p_value * (1.0f - (1.0f - f) * p_saturation);
		switch (i % 6)
		{
		case 0: return { p_value, t, p };
		case 1: return { q, p_value, p };
		case 2: return { p, p_value, t };
		case 3: return { p, q, p_value };
		case 4: return { t, p, p_value };
		default: return { p_value, p, q };
		}
	}

	// Light per-solid color: faces outside any solid get a neutral gray, and
	// each solid gets a pastel hue spaced by the golden angle so neighboring
	// solids stay visually distinct.
	Maths::FVector4 DomainColorForSolid(int p_solidIndex)
	{
		if (p_solidIndex < 0) {
			return Maths::FVector4{ 0.85f, 0.85f, 0.88f, 1.0f };
		}
        if (p_solidIndex==0) {
            return Maths::FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
		const float hue = std::fmod(p_solidIndex * 137.508f, 360.0f) / 360.0f;
		const auto rgb = HsvToRgb(hue, 0.38f, 0.95f);
		return Maths::FVector4{ rgb.x, rgb.y, rgb.z, 1.0f };
	}
}

namespace Core::ECS::Components
{
	class CTopoShape::CTopoShapeInternal {
	public:
		CTopoShapeInternal(CTopoShape* self) :mSelf(self){

		}
		~CTopoShapeInternal() {
		}
	private:
		friend class CTopoShape;
        HighLightOption highOption;
		/** What GetColor() answers while there is no material to read the colour
		 * back from; SetColor() keeps it up to date (see ApplyColor). */
		Maths::FVector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		CTopoShape* mSelf = nullptr;
		Part::TopoShape mTopoShape;
        std::vector<std::pair<int, int>>childMeshInfos;
        /*
        Topology tree:
        - faceActors / edgeActors keep the leaf actors created during
          discretization. They used to live directly under the "Face"/"Edge"
          render children; now they are parented under Solid/Shell groups.
        - faceSolidShell / edgeShell map each face/edge to its ancestor
          solid/shell indices so leaves can be grouped topologically.
        - shellGroups / topoGroupList track the group actors for cleanup.
        */
        std::vector<Core::ECS::Actor*> faceActors;
        std::vector<Core::ECS::Actor*> edgeActors;
        std::vector<Core::ECS::Actor*> vertexActors;
        std::vector<std::pair<int, int>>faceSolidShell;
        std::vector<int>shellSolid;
        // All ancestor shells per global edge index. An edge shared by several
        // shells (e.g. the boundary between two solids) gets one leaf actor per
        // shell so the TreeView shows the real topology.
        std::vector<std::vector<int>>edgeShells;
        // The same for vertices: one leaf actor per owning shell, and the range of
        // the point mesh each vertex occupies (a vertex is one point, so the range
        // is one index - kept as a range so the visibility filtering reads like the
        // edge one).
        std::vector<std::vector<Core::ECS::Actor*>> vertexLeafActors;
        std::vector<std::pair<int, int>> vertexIndexRanges;
        std::vector<std::vector<int>> vertexShells;
        /** Where each vertex sits, in the shape's own coordinates: what the hover
         * and the selection of a vertex draw their marker at. */
        std::vector<Maths::FVector3> vertexPositions;
        std::map<int, Core::ECS::Actor*>shellGroups;
        std::map<int, Core::ECS::Actor*>solidGroups;
        std::map<std::string, Core::ECS::Actor*>fallbackGroups;
        std::vector<Core::ECS::Actor*>topoGroupList;
        // Edge visibility: the line mesh keeps one index range per valid edge;
        // updateEdgeMesh re-uploads only the indices of edges whose leaf actors
        // are active (IsActive already accounts for inactive ancestors).
        ::Rendering::Resources::Mesh* lineMesh = nullptr;
        /** The batched vertices of the shape: one point per topological vertex, on
         * the "AllVertices" render child of the actor (see updateVertex below). */
        ::Rendering::Resources::Mesh* vertexMesh = nullptr;
        std::vector<std::pair<int, int>>edgeIndexRanges;
        std::vector<std::vector<Core::ECS::Actor*>>edgeLeafActors;
        /*
        use these two vectors because we may skip some empty domains
         when we generate childMeshInfos       
        */
        std::vector<int>domainIndexToFaceChildIndex;
        //std::vector<int>domainIndexToEdgeChildIndex;
        bool updateFace = false;
        bool updateEdge = false;
        bool updateVertex = false;
        bool updateChildMesh = false;
        bool hoverLine = false;
        bool hoverVertex = false;
        int hoverVertexId = -1;
        std::vector<int> selectedVertexIds;
		std::vector<int>curOpaqueChildMeshIndex;
        std::vector<int>curTransparentChildMeshIndex;
        std::vector<std::pair<int, int>> curTransparentChildMesh;
        std::vector<std::pair<int, int>>curOpaqueChildMesh;
        std::vector<Eigen::Vector3f> hoverLineSeg;
        std::vector<std::vector<Eigen::Vector3f>>selectLineSeg;
	};
	CTopoShape::CTopoShape(ECS::Actor& p_owner) : AComponent(p_owner),mInternal(new CTopoShapeInternal(this))
	{
        //switchHighLightMode(HighLightOption::Mode mode)
	}

	CTopoShape::~CTopoShape()
	{
		delete mInternal;
	}

	std::string CTopoShape::GetName()
	{
		return "CTopoShape";
	}

    HighLightOption& CTopoShape::getHightLightOption()
    {
        return mInternal->highOption;
    }

	namespace
	{
		/** The material the faces of an actor's shape are drawn with: the one TopoActor()
		 * makes for its "AllFaces" render anchor, or null while the actor has none. */
		Core::Resources::Material* FaceMaterialOf(Core::ECS::Actor& p_owner)
		{
			Core::ECS::Actor* faces = p_owner.GetChild("AllFaces");
			if (faces == nullptr) {
				return nullptr;
			}
			auto* renderer = faces->GetComponent<Core::ECS::Components::CMaterialRenderer>();
			return renderer != nullptr ? renderer->GetMaterialAtIndex(0) : nullptr;
		}
	}

	Maths::FVector4 CTopoShape::GetColor() const
	{
		// What is drawn is the material, and the material panel edits that material
		// directly, so the colour is read back from it when there is one: what a panel
		// shows and what the shape carries stay one and the same value.
		if (Core::Resources::Material* material = FaceMaterialOf(owner)) {
			if (auto property = material->GetProperty("u_Albedo"); property.has_value()) {
				if (std::holds_alternative<Maths::FVector4>(property.value().value)) {
					return std::get<Maths::FVector4>(property.value().value);
				}
			}
		}
		return mInternal->color;
	}

	void CTopoShape::SetColor(const Maths::FVector4& p_color)
	{
		mInternal->color = p_color;
		ApplyColor();
	}

	void CTopoShape::ApplyColor()
	{
		Core::Resources::Material* material = FaceMaterialOf(owner);
		if (material == nullptr) {
			// The render anchors are not up yet; TopoActor() calls this again once
			// they are, and the colour is kept until then.
			return;
		}
		material->SetProperty("u_Albedo", mInternal->color);
	}

    void CTopoShape::switchHighLightMode(HighLightOption::Mode mode)
    {
        if (mode == HighLightOption::Mode::Color) {
            if (mInternal->highOption.mode == HighLightOption::Mode::Transparent) {
                setChildsMeshTransParent({});
            }
        }
        mInternal->highOption.mode = mode;
    }

	void CTopoShape::OnUpdate(float p_deltaTime)
	{
        auto& instance=MOON::ImRenderer::instance();
        // What the overlays below draw is stored in the shape's own space, the way
        // the batched meshes are: those are drawn with the actor's matrix, so the
        // markers have to follow it too or a moved body would show them where the
        // shape was built.
        const Maths::FMatrix4 world = owner.transform.GetWorldMatrix();
        const auto toWorld = [&world](const Maths::FVector3& p) {
            const Maths::FVector3 w = Maths::FMatrix4::MulPoint(world, p);
            return Eigen::Vector3f(w.x, w.y, w.z);
            };
        if (mInternal->hoverLine) {
            std::vector<Eigen::Vector3f> segments;
            segments.reserve(mInternal->hoverLineSeg.size());
            for (const auto& p : mInternal->hoverLineSeg) {
                segments.push_back(toWorld({ p.x(), p.y(), p.z() }));
            }
            instance.drawLineList(segments, 3.0f, Eigen::Vector4<uint8_t>(255, 0, 180, 255));
        }
        for (int i = 0;i < mInternal->selectLineSeg.size();i++) {
            std::vector<Eigen::Vector3f> segments;
            segments.reserve(mInternal->selectLineSeg[i].size());
            for (const auto& p : mInternal->selectLineSeg[i]) {
                segments.push_back(toWorld({ p.x(), p.y(), p.z() }));
            }
            instance.drawLineList(segments, 3.0f, Eigen::Vector4<uint8_t>(255, 0, 130, 255));
        }
        // The vertex dot itself is drawn by its batch; this is the marker that says
        // which dot is hovered or picked - the same two colors the edges use.
        const auto drawVertexMarker =
            [&](int vertexId, const Eigen::Vector4<uint8_t>& color) {
                if (vertexId < 0
                    || vertexId >= static_cast<int>(mInternal->vertexPositions.size())) {
                    return;
                }
                instance.drawPoint(
                    toWorld(mInternal->vertexPositions[vertexId]), 14.0f, color);
            };
        if (mInternal->hoverVertex) {
            drawVertexMarker(
                mInternal->hoverVertexId, Eigen::Vector4<uint8_t>(255, 0, 180, 255));
        }
        for (int vertexId : mInternal->selectedVertexIds) {
            drawVertexMarker(vertexId, Eigen::Vector4<uint8_t>(255, 0, 130, 255));
        }
        // A feature whose shape never came out - a thickness the kernel refused, say -
        // has no topology to build: no face domains, no edges, no vertices. Everything
        // below here creates actors, batches and BVHs for the shape, and doing that for
        // a shape that is not there is how an empty one used to take the process down.
        if (mInternal->mTopoShape.isNull()) {
            mInternal->updateFace = false;
            mInternal->updateEdge = false;
            mInternal->updateVertex = false;
            mInternal->updateChildMesh = false;
            return;
        }

        if (mInternal->updateFace|| mInternal->updateEdge) {
            ZoneScoped;
            auto& view = GetService(::Editor::Panels::SceneView);
            auto& renderer = view.GetRenderer();
            auto scene = view.GetScene();
            rebuildTopologyTree();
           
            if (mInternal->updateFace) { 
                mInternal->updateFace = false; 
                mInternal->childMeshInfos.clear();
                std::vector<Data::ComplexGeoData::Domain> domains;
                {
                    
                    ZoneScopedN("GetDomain");
                    mInternal->mTopoShape.getDomainfaces(domains, mInternal->mTopoShape.getAccuracy());
                    //the follow code will crash in release mode
                    /*
                    
                    static MOON::System::JobSystem::Context locctx;
                    TopoDS_Shape shape=mInternal->mTopoShape.getShape();
                    if (!shape.IsNull()) {
                        auto defaultAngularDeflection = [](double linearTolerance)
                            {
                                // Default OCC angular deflection is 0.5 radians, or about 28.6 degrees.
                                // That is a bit coarser than necessary for performance, so we default to at
                                // most 0.1 radians, or 5.7 degrees. We also do not go finer than 0.005, or
                                // roughly 0.28 degree angular resolution, to avoid performance tanking
                                // completely at very fine resolutions.
                                return std::min(0.1, linearTolerance * 5 + 0.005);
                            };

                         BRepMesh_IncrementalMesh aMesh(shape, mInternal->mTopoShape.getAccuracy(),
                                                 Standard_False,
                                                
                                                defaultAngularDeflection(mInternal->mTopoShape.getAccuracy()),
                                                 false);
                        

                         TopTools_IndexedMapOfShape faceMap;
                         TopExp::MapShapes(shape, TopAbs_FACE, faceMap); 
                         std::size_t countFaces = faceMap.Extent();
                         domains.resize(countFaces);
                         auto lamda = [&](JobDispatchArgs arg) {
                             int i = arg.jobIndex + 1;
                             const TopoDS_Face& face = TopoDS::Face(faceMap(i));
                             std::vector<gp_Pnt> points;
                             std::vector<gp_Vec> normals;
                             std::vector<Poly_Triangle> facets;
                             if (!Part::Tools::getTriangulation(face, points, normals, facets)) {
                                 // For a face that cannot be meshed append an empty domain.
                                 // It's important for some algorithms (e.g. color mapping) that the numbers of
                                 // faces and domains match
                                 Data::ComplexGeoData::Domain domain;
                                 domains[arg.jobIndex]=domain;
                             }
                             else {
                                 Data::ComplexGeoData::Domain domain;
                                 // copy the points
                                 domain.points.reserve(points.size());
                                 domain.normals.reserve(points.size());
                                 for (const auto& it : points) {
                                     Standard_Real X, Y, Z;
                                     it.Coord(X, Y, Z);
                                     domain.points.emplace_back(X, Y, Z);
                                 }
                                 for (const auto& it : normals) {
                                     Standard_Real X, Y, Z;
                                     it.Coord(X, Y, Z);
                                     domain.normals.emplace_back(X, Y, Z);
                                 }
                                 // copy the triangles
                                 domain.facets.reserve(facets.size());
                                 for (const auto& it : facets) {
                                     Standard_Integer N1, N2, N3;
                                     it.Get(N1, N2, N3);

                                     Data::ComplexGeoData::Facet tria;
                                     tria.I1 = N1;
                                     tria.I2 = N2;
                                     tria.I3 = N3;
                                     domain.facets.push_back(tria);
                                 }

                                 domains[arg.jobIndex]=domain;
                             }

                          };
                         MOON::System::JobSystem::Dispatch(locctx, countFaces, 10, lamda);
                         MOON::System::JobSystem::Wait(locctx);
                    }                    
                    */

                }
               
                std::vector<::Rendering::Geometry::VertexBVH> faceVertices;
                std::vector<unsigned int>indices;

                std::vector<Maths::FVector4>domainColor;
                unsigned int vertexOffset = 0;
                unsigned int indexOffset = 0;
                
                std::vector<::Rendering::Geometry::bbox>domainBoxs;
                std::vector<uint32_t>domainRange;
                auto faceChild = owner.GetChild("AllFaces");
                int domainIndex = -1;
                mInternal->domainIndexToFaceChildIndex.resize(domains.size(),-1);
                std::vector<int>DomainIndexToi(domains.size(), -1);
                std::vector<int>domainId;
                std::vector<std::pair<int,int>>domainVertexNum;
                std::vector<std::pair<int, int>>domainIndexNum; 
                int numDomains =0 ;
                //int vertexOffset = 0;
                //int indexOffset = 0;
                ZoneScopedN("Batch");
                for (int i = 0; i < domains.size(); i++) {
                    if (domains[i].facets.size() > 0) {
                        domainIndex++;
                        mInternal->domainIndexToFaceChildIndex[i] = domainIndex;
                        DomainIndexToi[domainIndex] = i;
						ZoneScopedN("CreateActor");
                        auto& actor = scene->CreateActor("Face_" + std::to_string(i), "TopoFace");
                        const auto& ss = mInternal->faceSolidShell[i];
                        actor.SetParent(*getOrCreateTopoGroup(ss.second, "Faces"));
                        mInternal->faceActors.push_back(&actor);
                        domainId.push_back(actor.GetID());
                        // ss.first is the 0-based solid ordinal (matches the
                        // Solid_k actors in the topology tree); -1 = free face.
                        domainColor.push_back(DomainColorForSolid(ss.first));
                        mInternal->childMeshInfos.push_back(std::make_pair<int, int>(indexOffset, domains[i].facets.size() * 3));
                        domainVertexNum.push_back({vertexOffset, domains[i].points.size() });
                        domainIndexNum.push_back({indexOffset ,domains[i].facets.size() * 3 });
                        vertexOffset += domains[i].points.size();
                        indexOffset += domains[i].facets.size() * 3;
                        domainRange.push_back(indexOffset);
                        numDomains++;
                    }
                }
                faceVertices.resize(vertexOffset);
                indices.resize(indexOffset);
                domainBoxs.resize(numDomains);

                auto lamda = [&](JobDispatchArgs arg) {
                    ZoneScopedN("merge job");
                    int domainIndex=arg.jobIndex;
                    Maths::FVector2 indexId = Maths::FVector2{ domainIndex * 1.0f,domainId[domainIndex] * 1.0f };

                    ::Rendering::Geometry::bbox subBox;
                    int iIndex= DomainIndexToi[domainIndex];
                    for (int k = 0; k < domains[iIndex].points.size(); k++) {
                        faceVertices[domainVertexNum[domainIndex].first + k] = {
                            Maths::FVector3{ static_cast<float>(domains[iIndex].points[k].x),static_cast<float>(domains[iIndex].points[k].y),static_cast<float>(domains[iIndex].points[k].z) },
                            Maths::FVector2{ static_cast<float>(domains[iIndex].uvs[k].x),static_cast<float>(domains[iIndex].uvs[k].y) },
                            Maths::FVector3{ static_cast<float>(domains[iIndex].normals[k].x),static_cast<float>(domains[iIndex].normals[k].y),static_cast<float>(domains[iIndex].normals[k].z)},
                            indexId 
                        };
                        subBox.grow(faceVertices[domainVertexNum[domainIndex].first + k].position);
                    }
                    domainBoxs[domainIndex]=subBox;
                    for (int k = 0; k < domains[iIndex].facets.size(); k++) {
                        indices[domainIndexNum[domainIndex].first + 3 * k] = domains[iIndex].facets[k].I1 + domainVertexNum[domainIndex].first;
                        indices[domainIndexNum[domainIndex].first + 3 * k + 1] = domains[iIndex].facets[k].I2 + domainVertexNum[domainIndex].first;
                        indices[domainIndexNum[domainIndex].first + 3 * k + 2] = domains[iIndex].facets[k].I3 + domainVertexNum[domainIndex].first;;
                    }
                };
                {
                    ZoneScopedN("Wait Batch");
                    MOON::System::JobSystem::Context mergeCtx;
                    MOON::System::JobSystem::Dispatch(mergeCtx, numDomains, 10, lamda);
                    MOON::System::JobSystem::Wait(mergeCtx);
                }


                ::Rendering::Resources::Mesh* faceMesh = nullptr;
                {
                    ZoneScopedN("faceMesh");

                    faceMesh = new ::Rendering::Resources::Mesh(
                        faceVertices,
                        indices,
                        0,
                        ::Rendering::Settings::EPrimitiveMode::TRIANGLES);
                    //add this for transparent
                    faceMesh->AddSubRangeBuffer();
                    faceMesh->AddMaterial(1,1);
                }
                auto model = faceChild->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel();
                model->GetMaterialNames().emplace_back("Face");
                model->ClearMeshes();
                model->AddMesh(faceMesh);    
                faceMesh->ComputeBoundingSphereAndBox();
                model->computeBoxAndShpere();
                //auto computeBox =[=](JobDispatchArgs arg) {
                //    faceMesh->ComputeBoundingSphereAndBox();
                //    model->computeBoxAndShpere();
                //};
                //static MOON::System::JobSystem::Context computeBoxCtx;
                //MOON::System::JobSystem::Execute(computeBoxCtx,computeBox);

                //these two tex have leaks!!!!
                //domain colors Tex
                ::Rendering::Settings::TextureDesc desc;
                desc.isTextureBuffer = true;
                desc.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F;
                desc.buffetLen = domainColor.size() * sizeof(Maths::FVector4);
                desc.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
                    .data = domainColor.data()
                };
                ::Rendering::HAL::GLTexture* domainColorTex = new ::Rendering::HAL::GLTexture(::Rendering::Settings::ETextureType::TEXTURE_BUFFER);
                domainColorTex->Allocate(desc);
                // 创建并注册默认材质
                auto& materialRenderer = *faceChild->GetComponent <Core::ECS::Components::CMaterialRenderer>();
                Core::Resources::Material* tempMat = materialRenderer.GetMaterialAtIndex(0);
                tempMat->SetProperty("domainColorTex", domainColorTex);
                auto& batchMesh = *faceChild->GetComponent<Core::ECS::Components::CBatchMeshTriangle>();
                batchMesh.SetColors(domainColor);
                {
                    ZoneScopedN("domainBoxes BuildBvh"); 
                    batchMesh.BuildBvh(domainBoxs, domainRange);
                }
                setChildsMeshTransParent({},false);
            }
            if (mInternal->updateEdge)
            {
                ZoneScopedN("updateEdge");
                static MOON::System::JobSystem::Context ctx;
                mInternal->updateEdge = false;
                std::vector<Maths::FVector3>linePoints;
                std::vector<Data::ComplexGeoData::Line>LineRanges;
                // Maps each valid edge index to its global edge index; it is
                // used to look up the edge's ancestor shell when grouping.
                std::vector<int>lineIndexToi;
                {
                    ZoneScopedN("getLines");
                    //mInternal->mTopoShape.getLines(linePoints, LineRanges, mInternal->mTopoShape.getAccuracy());
                    TopoDS_Shape shape=mInternal->mTopoShape.getShape();
                    if (!shape.IsNull()) {
                        // build up map edge->face
                        TopTools_IndexedDataMapOfShapeListOfShape edge2Face;
                        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edge2Face);
                        TopTools_IndexedMapOfShape edgeMap;
                        TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
                        std::vector<std::vector<gp_Pnt>> pointArray(edgeMap.Extent(), std::vector<gp_Pnt>());
                        auto lamda = [&](JobDispatchArgs arg) {
                            int i = arg.jobIndex+1;
                            const TopoDS_Edge& aEdge = TopoDS::Edge(edgeMap(i));
                            std::vector<gp_Pnt> points;

                            if (!Part::Tools::getPolygon3D(aEdge, points)) {
                                // the edge has not its own triangulation, but then a face the edge is attached to
                                // must provide this triangulation

                                // Look for one face in our map (it doesn't care which one we take)
                                int index = edge2Face.FindIndex(aEdge);
                                if (index < 1) {
                                    return;
                                }
                                const auto& faces = edge2Face.FindFromIndex(index);
                                if (faces.IsEmpty()) {
                                    return;
                                }
                                const TopoDS_Face& aFace = TopoDS::Face(faces.First());
                                if (!Part::Tools::getPolygonOnTriangulation(aEdge, aFace, points)) {
                                    return;
                                }
                            }
                            pointArray[arg.jobIndex] = std::move(points);
                        };
                        MOON::System::JobSystem::Context ctx;
                        MOON::System::JobSystem::Dispatch(ctx, edgeMap.Extent(), 10, lamda);
                        MOON::System::JobSystem::Wait(ctx);

                        int validLineNums = 0;
                        int vertexOffset = 0;
                        int numVertex = 0;
                        for (int i = 0; i < pointArray.size(); i++) {
                            if (pointArray[i].size() > 0) {
                                lineIndexToi.push_back(i);
                                validLineNums++;
                                LineRanges.emplace_back();
                                LineRanges.back().I1 = vertexOffset;
                                LineRanges.back().I2 = vertexOffset+ pointArray[i].size() - 1;
                                numVertex += pointArray[i].size();
                                vertexOffset = LineRanges.back().I2 + 1;
                            }
                        }
                        linePoints.resize(numVertex);
                        auto mergeVertex = [&](JobDispatchArgs arg) {
                            int i = lineIndexToi[arg.jobIndex];
                            for (int k = LineRanges[arg.jobIndex].I1; k <= LineRanges[arg.jobIndex].I2; k++) {
                                gp_Pnt& p=pointArray[i][k - LineRanges[arg.jobIndex].I1];
                                linePoints[k] = {static_cast<float>(p.X()),static_cast<float>(p.Y()) ,static_cast<float>(p.Z()) };
                            }
                        };
                        MOON::System::JobSystem::Dispatch(ctx, validLineNums, 10, mergeVertex);
                        MOON::System::JobSystem::Wait(ctx);
                    }
                }

                //build lines
                std::vector<::Rendering::Geometry::VertexBVH> p_vertices;
                std::vector<uint32_t>lineIndex;
                std::vector<uint32_t>lineSegmentOffsets;
                auto edgeChild = owner.GetChild("AllEdges");
                p_vertices.reserve(linePoints.size());
                lineSegmentOffsets.reserve(LineRanges.size());
                mInternal->edgeLeafActors.resize(LineRanges.size());
                mInternal->edgeIndexRanges.clear();
                mInternal->edgeIndexRanges.reserve(LineRanges.size());
                for (int i = 0; i < LineRanges.size(); i++) {
                    ZoneScopedN("line");
                    auto& l = LineRanges[i];
                    // The valid edge i corresponds to the global edge
                    // lineIndexToi[i]. An edge shared by several shells gets one
                    // leaf actor per shell so the TreeView shows the real
                    // topology; the first actor provides the picking ID that is
                    // stored in the line vertices.
                    std::vector<int> shells;
                    if (i < static_cast<int>(lineIndexToi.size()) &&
                        lineIndexToi[i] < static_cast<int>(mInternal->edgeShells.size())) {
                        shells = mInternal->edgeShells[lineIndexToi[i]];
                    }
                    if (shells.empty()) {
                        shells.push_back(-1);
                    }
                    const int edgeIndexStart = static_cast<int>(lineIndex.size());
                    float subLineId = 0.0f;
                    for (size_t s = 0; s < shells.size(); s++) {
                        auto& actor = scene->CreateActor("Edge_" + std::to_string(i), "TopoEdge");
                        actor.SetParent(*getOrCreateTopoGroup(shells[s], "Edges"));
                        mInternal->edgeActors.push_back(&actor);
                        mInternal->edgeLeafActors[i].push_back(&actor);
                        if (s == 0) {
                            subLineId = actor.GetID() * 1.0f;
                        }
                    }
                    for (int k = l.I1; k <= l.I2 - 1; k++) {
                        ::Rendering::Geometry::VertexBVH v;
                        v.position = linePoints[k];
                        v.domainId.x = i * 1.0f;
                        v.domainId.y = subLineId;
                        p_vertices.emplace_back(v);

                        lineIndex.push_back(k);
                        lineIndex.push_back(k + 1);
                    }
                    ::Rendering::Geometry::VertexBVH v;
                    v.position = linePoints[l.I2];
                    v.domainId.x = i * 1.0f;
                    v.domainId.y = subLineId;
                    p_vertices.emplace_back(v);

                    lineSegmentOffsets.emplace_back(lineIndex.size());
                    mInternal->edgeIndexRanges.emplace_back(
                        edgeIndexStart, static_cast<int>(lineIndex.size()) - edgeIndexStart);
                }
               
                auto lineMesh = new ::Rendering::Resources::Mesh(
                    p_vertices,
                    lineIndex,
                    0,
                    ::Rendering::Settings::EPrimitiveMode::LINES);
                mInternal->lineMesh = lineMesh;
                //lineMesh->ComputeBoundingSphereAndBox();
                auto lineModel = edgeChild->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel();
                lineModel->GetMaterialNames().emplace_back("Line");
                lineModel->ClearMeshes();
                lineModel->AddMesh(lineMesh);

                auto computeBox = [=](JobDispatchArgs arg) {
                    lineMesh->ComputeBoundingSphereAndBox();
                    lineModel->computeBoxAndShpere();
                    };
                MOON::System::JobSystem::Execute(ctx, computeBox);
                auto& lineBacthMesh =*edgeChild->GetComponent<Core::ECS::Components::CBatchMeshLine>();
                lineBacthMesh.BuildBvh(lineSegmentOffsets);
                updateEdgeMesh();
            }
            // The topology actors (Solid/Shell/Face_*/Edge_*) were rebuilt;
            // ask the TreeView to refresh so the new hierarchy is visible.
            GetViewerWidget.refreshTreeView();
        }
        // The vertices are a batch of their own, on the "AllVertices" render child:
        // none of the topology tree is rebuilt for them, which is why they are not
        // part of the block above - that one drops and rebuilds the leaves.
        if (mInternal->updateVertex)
        {
            ZoneScopedN("updateVertex");
            mInternal->updateVertex = false;
            // One point per vertex of the shape, in the shape's own coordinates.
            // These are the topology vertices - the corners, and the points where a
            // curve closes or a cone comes to a tip - taken from the shape rather
            // than from the tessellation of the faces, so a dot sits exactly where a
            // reference would name the vertex.
            std::vector<::Rendering::Geometry::VertexBVH> p_vertices;
            std::vector<uint32_t> pointIndices;
            mInternal->vertexPositions.clear();
            mInternal->vertexLeafActors.clear();
            mInternal->vertexIndexRanges.clear();
            auto& scene = *GetService(::Editor::Panels::SceneView).GetScene();
            const TopoDS_Shape shape = mInternal->mTopoShape.getShape();
            if (!shape.IsNull()) {
                TopTools_IndexedMapOfShape vertexMap;
                TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
                p_vertices.reserve(vertexMap.Extent());
                pointIndices.reserve(vertexMap.Extent());
                mInternal->vertexPositions.reserve(vertexMap.Extent());
                mInternal->vertexLeafActors.resize(vertexMap.Extent());
                mInternal->vertexIndexRanges.reserve(vertexMap.Extent());
                for (int i = 1; i <= vertexMap.Extent(); ++i) {
                    const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertexMap(i)));
                    const Maths::FVector3 position = {
                        static_cast<float>(p.X()),
                        static_cast<float>(p.Y()),
                        static_cast<float>(p.Z()) };
                    // A vertex sits under the shells of the edges that meet there. A
                    // vertex shared by two solids gets one leaf per shell, and the
                    // first one carries the id a pick reports - the way an edge does
                    // it (see the edge block above).
                    std::vector<int> shells;
                    if (i - 1 < static_cast<int>(mInternal->vertexShells.size())) {
                        shells = mInternal->vertexShells[i - 1];
                    }
                    if (shells.empty()) {
                        shells.push_back(-1);
                    }
                    float subVertexId = 0.0f;
                    for (size_t s = 0; s < shells.size(); ++s) {
                        auto& actor = scene.CreateActor(
                            "Vertex_" + std::to_string(i - 1), "TopoVertex");
                        actor.SetParent(*getOrCreateTopoGroup(shells[s], "Vertices"));
                        mInternal->vertexActors.push_back(&actor);
                        mInternal->vertexLeafActors[i - 1].push_back(&actor);
                        if (s == 0) {
                            subVertexId = actor.GetID() * 1.0f;
                        }
                    }
                    ::Rendering::Geometry::VertexBVH v;
                    v.position = position;
                    // domainId.x is the vertex's own index, domainId.y the leaf actor
                    // a pick of this dot resolves to (that is the field the picking
                    // shader writes out, the same way the lines use it). texCoords.x
                    // carries the index for anything that reads the mesh instead.
                    v.texCoords = { static_cast<float>(i - 1), 0.0f };
                    v.domainId = { static_cast<float>(i - 1), subVertexId };
                    p_vertices.emplace_back(v);
                    const int pointIndex = static_cast<int>(pointIndices.size());
                    pointIndices.push_back(static_cast<uint32_t>(pointIndex));
                    mInternal->vertexPositions.push_back(position);
                    mInternal->vertexIndexRanges.emplace_back(pointIndex, 1);
                }
            }
            auto* vertexChild = owner.GetChild("AllVertices");
            if (vertexChild != nullptr) {
                auto* vertexMesh = new ::Rendering::Resources::Mesh(
                    p_vertices,
                    pointIndices,
                    0,
                    ::Rendering::Settings::EPrimitiveMode::POINTS);
                mInternal->vertexMesh = vertexMesh;
                auto* vertexModel = vertexChild
                    ->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel();
                vertexModel->GetMaterialNames().emplace_back("Point");
                vertexModel->ClearMeshes();
                vertexModel->AddMesh(vertexMesh);
                // The points are few, so the box is taken here like the faces do it
                // rather than on a job thread like the (often long) line mesh.
                vertexMesh->ComputeBoundingSphereAndBox();
                vertexModel->computeBoxAndShpere();
                updateVertexMesh();
            }
        }
        if (mInternal->updateChildMesh) {
            mInternal->updateChildMesh = false;
            updateChildMesh();
        }
	}

    void CTopoShape::updateChildBuffer()
    {
        mInternal->updateChildMesh = true;
    }

    std::vector<std::pair<int, int>> CTopoShape::GetChildMeshInfo()
    {
        return mInternal->childMeshInfos;
        
    }

    void CTopoShape::setChildsMeshTransParent(const std::vector<int>& childs, bool updateBuffer )
    {
        if (updateBuffer) {
            updateChildBuffer();
        }

        std::vector<int>listTransparentIndex;
        std::vector<int>listOpaqueIndex;
        listTransparentIndex.resize(childs.size());
        listOpaqueIndex.resize(mInternal->childMeshInfos.size() - childs.size());
        std::vector<int>table(mInternal->childMeshInfos.size(),0);
        int indexTransparent = 0;
        int indexOpaque = 0;
        for (int i = 0; i < childs.size(); i++) {
            table[childs[i]] = 1;
        }
        for (int i = 0;i < table.size();i++) {
            if (table[i] == 1) {
                listTransparentIndex[indexTransparent++] = i;
            }
            else
            {
                listOpaqueIndex[indexOpaque++] = i;
            }
        }
		mInternal->curTransparentChildMeshIndex = listTransparentIndex;
		mInternal->curOpaqueChildMeshIndex = listOpaqueIndex;
    }

    void CTopoShape::rebuildTopologyTree()
    {
        auto& scene = *GetService(::Editor::Panels::SceneView).GetScene();

        // Destroy the previous topology leaves and group actors. The batched
        // meshes on the "Face"/"Edge" render children are not touched here.
        // Leaves are destroyed before their shell/solid parents so each actor
        // detaches itself from the parent's children list before the parent is
        // deleted (Actor::~Actor recursively deletes children otherwise).
        std::vector<Core::ECS::Actor*> toDestroy;
        toDestroy.insert(toDestroy.end(), mInternal->faceActors.begin(), mInternal->faceActors.end());
        toDestroy.insert(toDestroy.end(), mInternal->edgeActors.begin(), mInternal->edgeActors.end());
        toDestroy.insert(toDestroy.end(), mInternal->vertexActors.begin(), mInternal->vertexActors.end());
        std::vector<Core::ECS::Actor*> shells;
        std::vector<Core::ECS::Actor*> solids;
        std::vector<Core::ECS::Actor*> fallbacks;
        for (auto* group : mInternal->topoGroupList) {
            if (group->GetTag() == "Shell") {
                shells.push_back(group);
            }
            else if (group->GetTag() == "Solid") {
                solids.push_back(group);
            }
            else {
                fallbacks.push_back(group);
            }
        }
        toDestroy.insert(toDestroy.end(), shells.begin(), shells.end());
        toDestroy.insert(toDestroy.end(), solids.begin(), solids.end());
        toDestroy.insert(toDestroy.end(), fallbacks.begin(), fallbacks.end());
        scene.DelayDestroyActor(toDestroy);
        mInternal->faceActors.clear();
        mInternal->edgeActors.clear();
        mInternal->vertexActors.clear();
        mInternal->topoGroupList.clear();
        mInternal->shellGroups.clear();
        mInternal->solidGroups.clear();
        mInternal->fallbackGroups.clear();
        mInternal->lineMesh = nullptr;
        mInternal->vertexMesh = nullptr;
        mInternal->edgeIndexRanges.clear();
        mInternal->edgeLeafActors.clear();
        mInternal->vertexIndexRanges.clear();
        mInternal->vertexLeafActors.clear();
        mInternal->faceSolidShell.clear();
        mInternal->shellSolid.clear();
        mInternal->edgeShells.clear();
        mInternal->vertexShells.clear();

        const TopoDS_Shape shape = mInternal->mTopoShape.getShape();
        if (shape.IsNull()) {
            return;
        }

        // Index all solids and shells in explorer order.
        TopTools_IndexedMapOfShape solidMap;
        TopTools_IndexedMapOfShape shellMap;
        for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
            solidMap.Add(ex.Current());
        }
        for (TopExp_Explorer ex(shape, TopAbs_SHELL); ex.More(); ex.Next()) {
            shellMap.Add(ex.Current());
        }

        // For every shell, remember which solid owns it (or -1 if free).
        TopTools_IndexedDataMapOfShapeListOfShape shell2Solid;
        TopExp::MapShapesAndAncestors(shape, TopAbs_SHELL, TopAbs_SOLID, shell2Solid);
        mInternal->shellSolid.assign(shellMap.Extent(), -1);
        for (int i = 1; i <= shellMap.Extent(); ++i) {
            const TopoDS_Shape& shell = shellMap(i);
            if (shell2Solid.Contains(shell)) {
                const auto& solids = shell2Solid.FindFromKey(shell);
                if (!solids.IsEmpty()) {
                    mInternal->shellSolid[i - 1] = solidMap.FindIndex(solids.First()) - 1;
                }
            }
        }

        // Map every face (global explorer order, same as the domains array)
        // to its ancestor solid/shell indices.
        TopTools_IndexedDataMapOfShapeListOfShape face2Solid;
        TopTools_IndexedDataMapOfShapeListOfShape face2Shell;
        TopExp::MapShapesAndAncestors(shape, TopAbs_FACE, TopAbs_SOLID, face2Solid);
        TopExp::MapShapesAndAncestors(shape, TopAbs_FACE, TopAbs_SHELL, face2Shell);
        for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Shape& face = ex.Current();
            int solidIdx = -1;
            int shellIdx = -1;
            if (face2Solid.Contains(face)) {
                const auto& solids = face2Solid.FindFromKey(face);
                if (!solids.IsEmpty()) {
                    solidIdx = solidMap.FindIndex(solids.First()) - 1;
                }
            }
            if (face2Shell.Contains(face)) {
                const auto& shells = face2Shell.FindFromKey(face);
                if (!shells.IsEmpty()) {
                    shellIdx = shellMap.FindIndex(shells.First()) - 1;
                }
            }
            mInternal->faceSolidShell.emplace_back(solidIdx, shellIdx);
        }

        // Map every edge to ALL its ancestor shells. Use TopExp::MapShapes so
        // the order and uniqueness match the edge discretization below; edges
        // shared by several shells (the same TopoDS_Edge used by both solids)
        // then get one leaf actor per owning shell.
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
        TopTools_IndexedDataMapOfShapeListOfShape edge2Shell;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_SHELL, edge2Shell);
        for (int i = 1; i <= edgeMap.Extent(); ++i) {
            const TopoDS_Shape& edge = edgeMap(i);
            std::vector<int> shells;
            if (edge2Shell.Contains(edge)) {
                const auto& shellsOfEdge = edge2Shell.FindFromKey(edge);
                for (TopTools_ListIteratorOfListOfShape it(shellsOfEdge); it.More(); it.Next()) {
                    // MapShapesAndAncestors adds the ancestor shell once per
                    // adjacent face path, so the same shell can appear several
                    // times for one edge; deduplicate before storing.
                    const int shellIdx = shellMap.FindIndex(it.Value()) - 1;
                    if (std::find(shells.begin(), shells.end(), shellIdx) == shells.end()) {
                        shells.push_back(shellIdx);
                    }
                }
            }
            mInternal->edgeShells.push_back(std::move(shells));
        }

        // The same for the vertices, in the order the vertex mesh is built in (both
        // use TopExp::MapShapes, so index i is the same vertex): a vertex belongs to
        // the shells of the edges that meet there, which is what puts its dot under
        // the same Solid/Shell group as the edges and faces around it.
        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
        TopTools_IndexedDataMapOfShapeListOfShape vertex2Shell;
        TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_SHELL, vertex2Shell);
        for (int i = 1; i <= vertexMap.Extent(); ++i) {
            const TopoDS_Shape& vertex = vertexMap(i);
            std::vector<int> shells;
            if (vertex2Shell.Contains(vertex)) {
                const auto& shellsOfVertex = vertex2Shell.FindFromKey(vertex);
                for (TopTools_ListIteratorOfListOfShape it(shellsOfVertex); it.More(); it.Next()) {
                    // One ancestor shell is listed per path through the shape, so
                    // deduplicate like the edges do.
                    const int shellIdx = shellMap.FindIndex(it.Value()) - 1;
                    if (std::find(shells.begin(), shells.end(), shellIdx) == shells.end()) {
                        shells.push_back(shellIdx);
                    }
                }
            }
            mInternal->vertexShells.push_back(std::move(shells));
        }
    }

    Core::ECS::Actor* CTopoShape::getOrCreateTopoGroup(int shellIndex, const std::string& fallbackName)
    {
        auto& scene = *GetService(::Editor::Panels::SceneView).GetScene();

        // Only the registries of the current rebuild are consulted here. The
        // old groups are still alive (delay-destroyed at the end of the frame);
        // looking them up by name via GetChild would re-attach the new topology
        // to actors that are about to be deleted, orphaning the new leaves.
        if (shellIndex < 0) {
            // Faces/edges without a shell ancestor: group them under a single
            // fallback group (e.g. "Faces" / "Edges").
            auto* group = mInternal->fallbackGroups[fallbackName];
            if (!group) {
                auto& actor = scene.CreateActor(fallbackName, "TopoGroup");
                actor.SetParent(owner);
                mInternal->topoGroupList.push_back(&actor);
                group = &actor;
                mInternal->fallbackGroups[fallbackName] = group;
            }
            return group;
        }

        auto it = mInternal->shellGroups.find(shellIndex);
        Core::ECS::Actor* shellActor = nullptr;
        if (it != mInternal->shellGroups.end()) {
            shellActor = it->second;
        }
        else {
            Core::ECS::Actor* parent = &owner;
            const int solidIdx = (shellIndex >= 0 && shellIndex < static_cast<int>(mInternal->shellSolid.size()))
                ? mInternal->shellSolid[shellIndex] : -1;
            if (solidIdx >= 0) {
                const std::string solidName = "Solid_" + std::to_string(solidIdx);
                auto* solidActor = mInternal->solidGroups[solidIdx];
                if (!solidActor) {
                    auto& actor = scene.CreateActor(solidName, "Solid");
                    actor.SetParent(owner);
                    mInternal->topoGroupList.push_back(&actor);
                    solidActor = &actor;
                    mInternal->solidGroups[solidIdx] = solidActor;
                }
                parent = solidActor;
            }

            const std::string shellName = "Shell_" + std::to_string(shellIndex);
            auto& actor = scene.CreateActor(shellName, "Shell");
            actor.SetParent(*parent);
            mInternal->topoGroupList.push_back(&actor);
            shellActor = &actor;
            mInternal->shellGroups[shellIndex] = shellActor;
        }

        // The shell actor is always freshly created during this rebuild, so
        // its sub-groups are fresh as well. Faces and edges are separated into
        // their own empty parent actors ("Faces" / "Edges") per shell.
        auto* group = shellActor->GetChild(fallbackName);
        if (!group) {
            auto& actor = scene.CreateActor(fallbackName, "TopoGroup");
            actor.SetParent(*shellActor);
            mInternal->topoGroupList.push_back(&actor);
            group = &actor;
        }
        return group;
    }

	Part::TopoShape& CTopoShape::GetTopoShape()
	{
		return mInternal->mTopoShape;
	}

    Part::TopoShape CTopoShape::GetTopoFace(int childFaceId)
    {
        return mInternal->mTopoShape.getSubTopoShape(TopAbs_FACE,childFaceId+1);
    }

    Part::TopoShape CTopoShape::GetTopoEdge(int childFaceId)
    {
        return mInternal->mTopoShape.getSubTopoShape(TopAbs_EDGE, childFaceId + 1);
    }

    Part::TopoShape CTopoShape::GetTopoVertex(int childVertexId)
    {
        // Same enumeration the vertex batch is built with (TopExp::MapShapes), so
        // the index of a "Vertex_<i>" leaf is the index used here.
        return mInternal->mTopoShape.getSubTopoShape(TopAbs_VERTEX, childVertexId + 1);
    }

    void CTopoShape::hoverChild(int childId)
    {
        
        if (mInternal->highOption.mode == HighLightOption::Mode::Color) {
            auto& bacthMesh = *owner.GetChild("AllFaces")->GetComponent<Core::ECS::Components::CBatchMeshTriangle>();
            bacthMesh.SetHoverColor(mInternal->domainIndexToFaceChildIndex[childId], mInternal->highOption.hoverColor);
        }
        else if(mInternal->highOption.mode == HighLightOption::Mode::Transparent)
        {
            setChildsMeshTransParent({ mInternal->domainIndexToFaceChildIndex[childId] });
        }
    }

    void CTopoShape::selectChildFaces(const std::vector<int>& childIds)
    {
        if (mInternal->highOption.mode == HighLightOption::Mode::Color) {
            auto& batchMesh = *owner.GetChild("AllFaces")->GetComponent<Core::ECS::Components::CBatchMeshTriangle>();
            std::vector<int>candidates;
            candidates.reserve(childIds.size());
            for (int i = 0;i < childIds.size();i++) {
                candidates.emplace_back(mInternal->domainIndexToFaceChildIndex[childIds[i]]);
            }
            batchMesh.SetCandidatesIndex(candidates);
            batchMesh.SetColor(mInternal->highOption.selectColor);
        }
        else if (mInternal->highOption.mode == HighLightOption::Mode::Transparent)
        {
            std::vector<int>candidates;
            candidates.reserve(childIds.size());
            for (int i = 0;i < childIds.size();i++) {
                candidates.emplace_back(mInternal->domainIndexToFaceChildIndex[childIds[i]]);
            }
            setChildsMeshTransParent(candidates);
        }
    }

    void CTopoShape::hoverChildLine(int childId)
    {
        mInternal->hoverLine = true;
        auto& colorBar = *owner.GetChild("AllEdges")->GetComponent<Core::ECS::Components::CBatchMeshLine>();
        auto vertexArray = colorBar.getLineSeg(childId);
        mInternal->hoverLineSeg.clear();
        mInternal->hoverLineSeg.reserve(vertexArray.size());
        for (auto v : vertexArray) {
            mInternal->hoverLineSeg.push_back(Eigen::Vector3f(v.x, v.y, v.z));;
        }
    }

    void CTopoShape::selectChildLines(const std::vector<int>& childIds)
    {
        mInternal->selectLineSeg.clear();
        auto& colorBar = *owner.GetChild("AllEdges")->GetComponent<Core::ECS::Components::CBatchMeshLine>();
        for (int i = 0;i < childIds.size();i++) {
            auto vertexArray = colorBar.getLineSeg(childIds[i]);
            mInternal->selectLineSeg.emplace_back();
            auto& arr = mInternal->selectLineSeg.back();
            arr.clear();
            arr.reserve(vertexArray.size());
            for (auto v : vertexArray) {
                arr.push_back(Eigen::Vector3f(v.x, v.y, v.z));;
            }
        }
    }

    void CTopoShape::clearHover()
    {
        if (mInternal->highOption.mode == HighLightOption::Mode::Color) {
            auto& bacthMesh = *owner.GetChild("AllFaces")->GetComponent<Core::ECS::Components::CBatchMeshTriangle>();
            bacthMesh.ClearHoverColor();
        }
        else if (mInternal->highOption.mode == HighLightOption::Mode::Transparent)
        {
            setChildsMeshTransParent({  });
        }
    }

    void CTopoShape::clearHoverLine()
    {
        mInternal->hoverLine = false;
    }

    void CTopoShape::clearSelectLines()
    {
        mInternal->selectLineSeg.clear();
    }

    void CTopoShape::hoverChildVertex(int childId)
    {
        mInternal->hoverVertex = true;
        mInternal->hoverVertexId = childId;
    }

    void CTopoShape::selectChildVertex(const std::vector<int>& childIds)
    {
        mInternal->selectedVertexIds = childIds;
    }

    void CTopoShape::clearHoverVertex()
    {
        mInternal->hoverVertex = false;
        mInternal->hoverVertexId = -1;
    }

    void CTopoShape::clearSelectVertex()
    {
        mInternal->selectedVertexIds.clear();
    }

    void CTopoShape::discretizationFaceShape()
    {
        mInternal->updateFace = true;
    }

    void CTopoShape::discretizationEdgeShape()
    {
		mInternal->updateEdge = true;
    }

    void CTopoShape::discretizationVertexShape()
    {
        mInternal->updateVertex = true;
    }

	void CTopoShape::discretizationShape()
	{
        discretizationFaceShape();
        discretizationEdgeShape();
        discretizationVertexShape();
	}

	void CTopoShape::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}

	void CTopoShape::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}
    void CTopoShape::updateChildMesh()
    {
		mInternal->curOpaqueChildMesh.clear();
		mInternal->curTransparentChildMesh.clear();
        for (int i = 0;i < mInternal->curOpaqueChildMeshIndex.size();i++) {
			int id = mInternal->curOpaqueChildMeshIndex[i];
            if (id < static_cast<int>(mInternal->faceActors.size()) && mInternal->faceActors[id]->IsActive()) {
				mInternal->curOpaqueChildMesh.push_back(mInternal->childMeshInfos[id]);
            }   
        }
        for (int i = 0; i < mInternal->curTransparentChildMeshIndex.size(); i++) {
			int id = mInternal->curTransparentChildMeshIndex[i];
            if (id < static_cast<int>(mInternal->faceActors.size()) && mInternal->faceActors[id]->IsActive()) {
				mInternal->curTransparentChildMesh.push_back(mInternal->childMeshInfos[id]);
            }
        }
        auto model = owner.GetChild("AllFaces")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel();
        auto mesh = model->GetMesh(0);
        mesh->UploadIndices(mInternal->curOpaqueChildMesh, 0);
        mesh->UploadIndices(mInternal->curTransparentChildMesh, 1);
        updateEdgeMesh();
        updateVertexMesh();
    }

    void CTopoShape::updateEdgeMesh()
    {
        if (!mInternal->lineMesh) {
            return;
        }
        const size_t edgeCount = mInternal->edgeIndexRanges.size();
        if (edgeCount != mInternal->edgeLeafActors.size()) {
            return;
        }

        // An edge is visible when at least one of its leaf actors is active;
        // IsActive already walks up the ancestors, so unchecking an "Edges"
        // group, a Shell or a Solid hides every edge below it.
        const auto& allIndices = mInternal->lineMesh->GetIndices();
        std::vector<uint32_t> visibleIndices;
        for (size_t i = 0; i < edgeCount; ++i) {
            bool visible = false;
            for (auto* actor : mInternal->edgeLeafActors[i]) {
                if (actor->IsActive()) {
                    visible = true;
                    break;
                }
            }
            if (!visible) {
                continue;
            }
            const auto& range = mInternal->edgeIndexRanges[i];
            for (int k = 0; k < range.second; ++k) {
                visibleIndices.push_back(allIndices[range.first + k]);
            }
        }
        mInternal->lineMesh->UploadIndices(visibleIndices, 0);
    }

    void CTopoShape::updateVertexMesh()
    {
        if (!mInternal->vertexMesh) {
            return;
        }
        const size_t vertexCount = mInternal->vertexIndexRanges.size();
        if (vertexCount != mInternal->vertexLeafActors.size()) {
            return;
        }

        // A vertex is drawn while at least one of its leaf actors is active;
        // IsActive walks up the ancestors, so unchecking a "Vertices" group, a
        // Shell or a Solid hides the dots below it - the way it hides its edges.
        const auto& allIndices = mInternal->vertexMesh->GetIndices();
        std::vector<uint32_t> visibleIndices;
        visibleIndices.reserve(allIndices.size());
        for (size_t i = 0; i < vertexCount; ++i) {
            bool visible = false;
            for (auto* actor : mInternal->vertexLeafActors[i]) {
                if (actor->IsActive()) {
                    visible = true;
                    break;
                }
            }
            if (!visible) {
                continue;
            }
            const auto& range = mInternal->vertexIndexRanges[i];
            for (int k = 0; k < range.second; ++k) {
                visibleIndices.push_back(allIndices[range.first + k]);
            }
        }
        mInternal->vertexMesh->UploadIndices(visibleIndices, 0);
    }
}
