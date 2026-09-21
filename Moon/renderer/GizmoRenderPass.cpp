#include <ranges>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include "GizmoRenderPass.h"
#include "Core/Global/ServiceLocator.h"
#include "core/SceneSystem/BvhService.h"
#include "renderer/DebugSceneRenderer.h"
#include "renderer/SceneView.h"
#include "Interactive/Im3DRenderer.h"
#include "Settings/DebugSetting.h"
#include "Interactive/Widgets/RotateCenter.h"
#include "Interactive/Widgets/Measurement.h"
#include "Interactive/Widgets/ClipPlane.h"
#include "Interactive/Widgets/SketchPlane.h"
#include "Interactive/Widgets/SplitScreen.h"
#include "Interactive/Widgets/DrawSketchHandlerPoint.h"
#include "Interactive/Widgets/DrawSketchHandlerLine.h"
#include "Interactive/Widgets/DrawSketchHandlerLineSet.h"
#include "Interactive/Widgets/DrawSketchHandlerCircle.h"
#include "Interactive/Widgets/DrawSketchHandlerRotate.h"
#include "Interactive/Widgets/DrawSketchHandlerArc.h"
#include "Interactive/Widgets/DrawSketchHandlerArcSlot.h"
#include "Interactive/Widgets/DrawSketchHandlerEllipse.h"
#include "Interactive/Widgets/DrawSketchHandlerBSpline.h"
#include "Interactive/Widgets/DrawSketchHandlerRectangle.h"
#include "Interactive/Widgets/DrawSketchHandlerPolygon.h"
#include "Interactive/Widgets/DrawSketchHandlerSlot.h"
#include "Interactive/Widgets/DrawSketchHandlerTrimming.h"
#include "Interactive/Widgets/DrawSketchHandlerSymmetry.h"
#include "Interactive/Widgets/DrawSketchHandlerFillet.h"
#include "Interactive/Widgets/DrawSketchHandlerOffset.h"
#include "Interactive/Widgets/PrimitiveBox.h"
#include "Interactive/Widgets/PrimitiveSphere.h"
#include "Interactive/Widgets/PrimitiveCylinder.h"
#include "Interactive/Widgets/PrimitiveCone.h"
#include "Interactive/Widgets/ViewCubeWidget.h"
#include "Qtimgui/imgui/imgui.h"

struct GizmoRenderSettings
{
	bool drawBvh = false;
}gizmoRenderSetting;
class Editor::Rendering::GizmoRenderPass::GizmoRenderPassInternal {
	public:
		GizmoRenderPassInternal(Editor::Rendering::GizmoRenderPass* gizmoPass):
			mSelf(gizmoPass)
		{
			mWidgets["RotateCenter"] = new MOON::RotateCenter("RotateCenter");
			mWidgets["Measure"] = new MOON::Measurement("Measure");
			mWidgets["ClipPlane"] = new MOON::ClipPlane("ClipPlane");
			mWidgets["SplitScreen"] = new MOON::SplitScreen("SplitScreen");
			mWidgets["DrawSketchHandlerPoint"] = new MOON::DrawSketchHandlerPoint("DrawSketchHandlerPoint");
			mWidgets["DrawSketchHandlerLine"]=new MOON::DrawSketchHandlerLine("DrawSketchHandlerLine");
			mWidgets["DrawSketchHandlerLineSet"] = new MOON::DrawSketchHandlerLineSet("DrawSketchHandlerLineSet");
			mWidgets["DrawSketchHandlerCircle"] = new MOON::DrawSketchHandlerCircle("DrawSketchHandlerCircle");
			mWidgets["DrawSketchHandlerArc"] = new MOON::DrawSketchHandlerArc("DrawSketchHandlerArc");
			mWidgets["DrawSketchHandlerArcSlot"] = new MOON::DrawSketchHandlerArcSlot("DrawSketchHandlerArcSlot");
			mWidgets["DrawSketchHandlerEllipse"] = new MOON::DrawSketchHandlerEllipse("DrawSketchHandlerEllipse");
			mWidgets["DrawSketchHandlerBSpline"] = new MOON::DrawSketchHandlerBSpline("DrawSketchHandlerBSpline");
			mWidgets["DrawSketchHandlerRectangle"] = new MOON::DrawSketchHandlerRectangle("DrawSketchHandlerRectangle");
			mWidgets["DrawSketchHandlerPolygon"] = new MOON::DrawSketchHandlerPolygon("DrawSketchHandlerPolygon");
			mWidgets["DrawSketchHandlerSlot"] = new MOON::DrawSketchHandlerSlot("DrawSketchHandlerSlot");
			mWidgets["DrawSketchHandlerSymmetry"] = new MOON::DrawSketchHandlerSymmetry("DrawSketchHandlerSymmetry");
			mWidgets["DrawSketchHandlerFillet"] = new MOON::DrawSketchHandlerFillet("DrawSketchHandlerFillet");
			mWidgets["DrawSketchHandlerOffset"] = new MOON::DrawSketchHandlerOffset("DrawSketchHandlerOffset");
			mWidgets["PrimitiveBox"] = new MOON::PrimitiveBox("PrimitiveBox");
			mWidgets["PrimitiveSphere"] = new MOON::PrimitiveSphere("PrimitiveSphere");
			mWidgets["PrimitiveCylinder"] = new MOON::PrimitiveCylinder("PrimitiveCylinder");
			mWidgets["PrimitiveCone"] = new MOON::PrimitiveCone("PrimitiveCone");
			mWidgets["ViewCube"] = new MOON::ViewCubeWidget("ViewCube");
			
			mWidgets["DrawSketchHandlerRotate"] = new MOON::DrawSketchHandlerRotate("DrawSketchHandlerRotate");
			mWidgets["DrawSketchHandlerTrimming"] = new MOON::DrawSketchHandlerTrimming("DrawSketchHandlerTrimming");
			mWidgets["DrawSketchHandlerTrimming"]->setActive(false);
			
			mWidgets["ClipPlane"]->setActive(false);
			mWidgets["SplitScreen"]->setActive(false);
			
		}
		~GizmoRenderPassInternal()
		{
			for (auto it : mWidgets) {
				delete it.second;
			}
		}
		void enableGizmoWidget(const std::string& name, bool flag)
		{
			if (mWidgets.find(name) != mWidgets.end()) {
				mWidgets[name]->setActive(flag);
			}
		}
		bool isEnableGizmoWidget(const std::string&name) {
			if (mWidgets.find(name) != mWidgets.end()) {
				return mWidgets[name]->isActived();
			}
			return false;
		}
		MOON::EventWidget* getGizmoWidget(const std::string& name) {
			if (mWidgets.find(name) != mWidgets.end()) {
				return mWidgets[name];
			}
			return nullptr;
		}
	private:
		friend  class Editor::Rendering::GizmoRenderPass;
		Editor::Rendering::GizmoRenderPass* mSelf = nullptr;
		MOON::RotateCenter* mRotateCenterWidget = nullptr;
		MOON::Measurement* mMeasurementWidget = nullptr;
		std::unordered_map<std::string, MOON::EventWidget*>mWidgets;
};
Editor::Rendering::GizmoRenderPass::GizmoRenderPass(::Rendering::Core::CompositeRenderer& p_renderer)
	: ::Rendering::Core::ARenderPass(p_renderer),mInternal(new Editor::Rendering::GizmoRenderPass::GizmoRenderPassInternal(this))
{
	MOON::DebugSettings::instance().addCallBack("showBvh", "Default", [](MOON::NodeBase* self) {
		gizmoRenderSetting.drawBvh= self->getData<bool>();
	});

}

Editor::Rendering::GizmoRenderPass::~GizmoRenderPass()
{
	if (mInternal) {
		delete mInternal;
	}
}

void Editor::Rendering::GizmoRenderPass::enableGizmoWidget(const std::string& name, bool flag)
{
	mInternal->enableGizmoWidget(name,flag);
}

bool Editor::Rendering::GizmoRenderPass::isEnableGizmoWidget(const std::string& name)
{
	return mInternal->isEnableGizmoWidget(name);
}

MOON::EventWidget* Editor::Rendering::GizmoRenderPass::getGizmoWidget(const std::string& name)
{
	return mInternal->getGizmoWidget(name);
}

std::unordered_map<std::string, MOON::EventWidget*>& Editor::Rendering::GizmoRenderPass::getGizmoWidgets()
{
	return mInternal->mWidgets;
}



void Editor::Rendering::GizmoRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	auto& view = GetService(Editor::Panels::SceneView);;
	auto& renderer = MOON::ImRenderer::instance();
	//renderer.newFrame(&view);
	if (gizmoRenderSetting.drawBvh) {
		auto sceneBvh = view.GetScene()->GetBvh();
		std::vector<::Rendering::Geometry::Bvh::Node*>stack;
		if(sceneBvh!=nullptr)
		stack.push_back(sceneBvh->m_root);
		while (!stack.empty()) {
			auto cur = stack.back(); stack.pop_back();
			if (!cur)continue;
			if (cur->type == ::Rendering::Geometry::Bvh::kInternal) {
				if (cur->lc) {
					stack.push_back(cur->lc);
				}
				if (cur->rc) {
					stack.push_back(cur->rc);
				}
			}
			auto pmin = cur->bounds.pmin;
			auto pmax = cur->bounds.pmax;
			renderer.drawAlignedBox({ pmin.x,pmin.y ,pmin.z }, { pmax.x,pmax.y ,pmax.z });
		}
	}
	renderer.endFrame();
}
