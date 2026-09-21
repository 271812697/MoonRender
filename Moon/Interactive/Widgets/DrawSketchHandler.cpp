#pragma once
#include "DrawSketchHandler.h"
#include "Interactive/Im3DRenderer.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Qtimgui/imgui/imgui.h"
#include "renderer/SceneView.h"
#include "Sketcher/SketcheTool2D.h"
#include "feature/SketcherFeature.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "core/Global/ServiceLocator.h"
namespace MOON
{

    void DrawSketchHandler::quit()
    {
        this->setActive(false);
        auto& sketchToolBar=GetService(MOON::SketchToolbar);
        sketchToolBar.setUncheckedAction(getName());
    }
    void DrawSketchHandler::clearEdit()
    {
        lines.clear();
    }
    void DrawSketchHandler::makePlane(const SketcherPlane2D& v)
    {
        plane = v;
    }
    void DrawSketchHandler::onUpdate()
    {
       auto feature=SketcherObjManager::instance().GetCurrentActiveSketcherFeature();
       if (feature) {
           auto sketchobj= feature->getSketcherObj();
           if (sketchobj) {
               makePlane(sketchobj->getPlane());
           }
       }

       if (isSnapedSketchPos) {
           renderer->drawPoint(plane.valueEigen(onSketchPos), 14,Eigen::Vector4<uint8_t>(255, 0, 255, 0));
           //renderer->drawPoint2D({ onSketchPos.x,onSketchPos.y }, Eigen::Vector4<uint8_t>(255,0,255,0), 16,gizmoPlane);
       }
       if (drawSketchPos) {
            auto drawList=ImGui::GetForegroundDrawList();
            Maths::FVector2 screenPos=m_sceneView->worldToScreen(getWorldPosFromSketchPos(drawPos));
            screenPos.y -= 7;
		    screenPos.x += 5;
		    std::string posText = "(" + std::to_string(drawPos.x) + "," + std::to_string(drawPos.y) + ")";
            drawList->AddText(nullptr,0,ImVec2(screenPos.x,screenPos.y), IM_COL32(0, 0, 0, 255),posText.c_str());
       }
       renderer->pushColor({255,255,0,255});
       renderer->pushSize(3);
       for (int i = 0; i < lines.size();i += 2) {
		   renderer->drawLine(plane.valueEigen(lines[i]), plane.valueEigen(lines[i + 1]));
       }
       renderer->popSize();
       renderer->popColor();
    }
    void DrawSketchHandler::onMouseMove()
    {
        //need to make sure which plane
        auto ray = m_sceneView->GetMouseRay();
        Maths::FVector3 out;
       
        ray.hitPlane(Maths::FVector3(plane.normal.x, plane.normal.y, plane.normal.z), plane.normal.Dot(plane.origin), out);
        Base::Vector3d hitPos = Base::Vector3d (out.x,out.y,out.z );
        double x = (hitPos - plane.origin).Dot(plane.xAxis);
        double y = (hitPos - plane.origin).Dot(plane.yAxis);
        onSketchPos = Base::Vector2d(int(x * 100) / 100.0, int(y * 100) / 100.0);
        auto sketchobj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		isSnapedSketchPos = false;
        if (sketchobj) {
            isSnapedSketchPos=sketchobj->snapPoint(onSketchPos);
        }
    }
    void DrawSketchHandler::drawEdit(const std::vector<Base::Vector2d>& EditCurve)
    {
        if (EditCurve.size() > 0) {
            for (int i = 0; i < EditCurve.size() - 1; i++) {
                lines.push_back(EditCurve[i]);
                lines.push_back(EditCurve[i + 1]);
            }
        }
    }
    void DrawSketchHandler::drawEdit(const std::list<std::vector<Base::Vector2d>>& list) 
    {
        for (auto it = list.begin(); it != list.end(); it++) {
            auto& segment = *it;
            for (int i = 0; i < segment.size() - 1;i++) {
                lines.push_back(segment[i]);
                lines.push_back(segment[i+1]);
            } 
        }
    }

    void DrawSketchHandler::drawEdit(const std::vector<Part::Geometry*>& geometries)  {
        std::list<std::vector<Base::Vector2d>> list;
        for (const auto& geo : geometries) {
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                list.push_back(CurveConvert::toVector2D(geo,50));
            }
        }
        drawEdit(list);
    }
    void DrawSketchHandler::drawPositionAtCursor(Base::Vector2d pos)
    {
        drawSketchPos = true;
		drawPos = pos;  
    }
    void DrawSketchHandler::clearPositionAtCursor()
    {
        drawSketchPos = false;
    }
    void DrawSketchHandler::drawFloatValue(float value)
    {
        auto drawList = ImGui::GetForegroundDrawList();
        Maths::FVector2 screenPos = m_sceneView->worldToScreen(getWorldPosFromSketchPos(onSketchPos));
        screenPos.y -= 7;
        screenPos.x += 5;
        std::string posText = std::to_string(value);
        drawList->AddText(nullptr, 0, ImVec2(screenPos.x, screenPos.y), IM_COL32(0, 0, 0, 255), posText.c_str());
    }
    int DrawSketchHandler::getPreselectCurve() const
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        return Obj->getPreselectId();
    }
    Maths::FVector3 DrawSketchHandler::getWorldPosFromSketchPos(Base::Vector2d sketchPos)
    {
		Maths::FVector3 v;
		Base::Vector3d res=plane.value(sketchPos.x, sketchPos.y);
        return Maths::FVector3(res.x,res.y,res.z);
    }
}
