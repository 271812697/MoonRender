#include <tinyxml2.h>
#include <Core/ECS/Actor.h>
#include "core/component/CGeometryLine.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include "Geometry.h"
#include "Core/ResourceManagement/ModelManager.h"
#include "renderer/SceneView.h"
#include "renderer/Context.h"
#include "Core/Global/ServiceLocator.h"
#include "base/Tools2D.h"
#include "Sketcher/SketcheTool2D.h"
#include "core/callbackManager.h"
#include "core/log.h"
namespace Core::ECS::Components
{

	class CGeometryLine::CGeometryLineInternal {
	public:
		CGeometryLineInternal(CGeometryLine* self) :mSelf(self){

		}
		~CGeometryLineInternal() {
		}
	private:
		friend class CGeometryLine;
		CGeometryLine* mSelf = nullptr;
        bool update = false;
        bool buildLine = false;
        int plane = 2;
        Part::Geometry* mGeometry;
	};
	CGeometryLine::CGeometryLine(ECS::Actor& p_owner) : AComponent(p_owner),mInternal(new CGeometryLineInternal(this))
	{
	}

	CGeometryLine::~CGeometryLine()
	{
		delete mInternal;
	}

	std::string CGeometryLine::GetName()
	{
		return "CGeometryLine";
	}

	void CGeometryLine::OnUpdate(float p_deltaTime)
	{ 
        if (mInternal->update) {
			mInternal->update = false;
            buildLines(mInternal->plane);
        }
	}

	Part::Geometry* CGeometryLine::GetGeometry()
	{
        
		return mInternal->mGeometry;
	}

    void CGeometryLine::setGeometry(Part::Geometry* geometry)
	{
		mInternal->mGeometry = geometry;
    }

	void CGeometryLine::discretizationShape(int plane)
	{
		CORE_INFO("discretization GeometryLine for plane {0}", plane);
        mInternal->update = true;
        mInternal->plane = plane;
	}

	void CGeometryLine::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}

	void CGeometryLine::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}
    void CGeometryLine::buildLines(int plane)
	{
        CORE_INFO("build GeometryLine");
        //build lines
        std::vector<::Rendering::Geometry::VertexBVH> p_vertices;
        std::vector<uint32_t>lineIndex;
        std::vector<Base::Vector2d>linePoints = MOON::CurveConvert::toVector2D(mInternal->mGeometry, 50);
        p_vertices.reserve(linePoints.size());
        for (int i = 0; i < linePoints.size(); i++) {
            ::Rendering::Geometry::VertexBVH v;
            if (plane == 2) {
                v.position.x = static_cast<float>(linePoints[i].x);
                v.position.y = static_cast<float>(linePoints[i].y);
                v.position.z = 0;
            }
            else if (plane == 0) {
                v.position.x = 0;
                v.position.y = static_cast<float>(linePoints[i].x);
                v.position.z = static_cast<float>(linePoints[i].y);
            }
            else {
                v.position.x = static_cast<float>(linePoints[i].x);
                v.position.y = 0;
                v.position.z = static_cast<float>(linePoints[i].y);
            }

            v.texCoords.x = i * 1.0f;
            v.texCoords.y = i * 1.0f;
            p_vertices.emplace_back(v);
            if (i < linePoints.size() - 1) {
                lineIndex.push_back(i);
                lineIndex.push_back(i + 1);
            }
        }
        auto lineMesh = new ::Rendering::Resources::Mesh(
            p_vertices,
            lineIndex,
            0,
            ::Rendering::Settings::EPrimitiveMode::LINES);
        auto lineModel = new ::Rendering::Resources::Model(owner.GetName() + std::string("_lineModel"));
        ::Core::Global::ServiceLocator::Get<Core::ResourceManagement::ModelManager>().RegisterResource(owner.GetName() + std::string("_lineModel"), lineModel);
        lineModel->GetMaterialNames().emplace_back("Default");
        lineModel->AddMesh(lineMesh);

        owner.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(lineModel);
        auto& lineRener = *owner.GetComponent<Core::ECS::Components::CMaterialRenderer>();

        auto lineMat = new Core::Resources::Material();
        Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(owner.GetName() + std::string("_lineMat"), lineMat);
        lineMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Line.ovfx"]);
        lineMat->SetBackfaceCulling(false);
        lineMat->SetCastShadows(false);
        lineMat->SetReceiveShadows(false);
        lineMat->SetLineWidth(2.0);
        lineRener.SetMaterialAtIndex(0, *lineMat);
        lineRener.UpdateMaterialList();
	}
}
