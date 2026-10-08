#include "Interactive/Widgets/DrawSketchHandlerSymmetry.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"

namespace MOON {

	
    DrawSketchHandlerSymmetry::DrawSketchHandlerSymmetry(const std::string& name) :SupperClass(name),
        listOfGeoIds(listOfGeoIds)
        , refGeoId(-1)
        , deleteOriginal(false)
        , createSymConstraints(true)
    {

	}

    DrawSketchHandlerSymmetry::~DrawSketchHandlerSymmetry()
	{
		//delete m_internal;
	}

	void DrawSketchHandlerSymmetry::onUpdate()
	{
        DrawSketchHandler::onUpdate();
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (Obj) {
            listOfGeoIds = Obj->getSelectIds();
        }
	}

	void DrawSketchHandlerSymmetry::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        Q_UNUSED(onSketchPos);
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        switch (state()) {
        case SelectMode::SeekFirst: {
            int CrvId = getPreselectCurve();
            if ((CrvId >= 0 ) && Obj->getGeometry(CrvId)->is<Part::GeomLineSegment>())
                 {  // Curves
                refGeoId = CrvId;
            }
            else {
                refGeoId =-1;
            }
            CreateAndDrawShapeGeometry();
        } break;
        default:
            break;
        }
	}


    void DrawSketchHandlerSymmetry::createShape(bool onlyeditoutline)
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();

        ShapeGeometry.clear();


        if (refGeoId == -1) {
            return;
        }

        if (onlyeditoutline) {
            std::map<int, int> dummy1;
            std::map<int, bool> dummy2;
            std::vector<Part::Geometry*> symGeos
                = Obj->getSymmetric(listOfGeoIds, dummy1, dummy2, refGeoId);

            for (auto* geo : symGeos) {
                //ShapeGeometry.emplace_back(geo);
                std::unique_ptr<Part::Geometry> curve(geo->copy());
                ShapeGeometry.push_back(std::move(curve));
            }
        }
    }

    void DrawSketchHandlerSymmetry::deleteOriginalGeos()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (Obj) {
			Obj->deleteGeometries(listOfGeoIds);
        }
    }

    bool DrawSketchHandlerSymmetry::canGoToNextMode()
    {
        if (state() == SelectMode::SeekFirst && refGeoId == -1) {
            // Prevent validation if no reference selected.
            return false;
        }
        return true;
    }

    void DrawSketchHandlerSymmetry::executeCommands()
	{    
        //createShape(false);
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        Obj->addSymmetric(listOfGeoIds, refGeoId);
        //SupperClass::executeCommands();
        if (deleteOriginal) {
            deleteOriginalGeos();
        }
    }

}

