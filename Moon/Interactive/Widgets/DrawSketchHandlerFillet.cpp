#include "Interactive/Widgets/DrawSketchHandlerFillet.h"
#include "Sketcher/SketcherObjWidget.h"
#include "Interactive/Im3DRenderer.h"
#include "core/log.h"
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

	
    DrawSketchHandlerFillet::DrawSketchHandlerFillet(const std::string& name, ConstructionMethod constrMethod) :SupperClass(name,constrMethod),
        preserveCorner(true)
        , geoId1(-1)
        , geoId2(-1)
    {

	}

    DrawSketchHandlerFillet::~DrawSketchHandlerFillet()
	{
		//delete m_internal;
	}

	void DrawSketchHandlerFillet::onUpdate()
	{
        DrawSketchHandler::onUpdate();
        //renderer->drawPoint2D({ firstPos.x,firstPos .y},15, static_cast<Plane2D>(plane));
        //renderer->drawPoint2D({ secondPos.x,secondPos.y }, 15, static_cast<Plane2D>(plane));
	}

	void DrawSketchHandlerFillet::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        SketcherObj* obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        switch (state()) {
        case SelectMode::SeekFirst: {
            geoId1 = getPreselectCurve();
            firstPos = onSketchPos;
           

        } break;
        case SelectMode::SeekSecond: {
            geoId2 = getPreselectCurve();
            secondPos = onSketchPos;
           
        } break;
        default:
            break;
        }
	}



    bool DrawSketchHandlerFillet::canGoToNextMode()
    { 
        SketcherObj* obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        SketcherObjWidget* widget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget();
        if (state() == SelectMode::SeekFirst) {
            if (geoId1 >= 0) {
                const Part::Geometry* geo = obj->getGeometry(geoId1);
                if (geo->isDerivedFrom<Part::GeomBoundedCurve>()) {
                    widget->addSelect(geoId1);
                    return true;
                }
            }
        }

        if (state() == SelectMode::SeekSecond) {
            if (geoId2 >= 0&&geoId2!=geoId1) {
                const Part::Geometry* geo = obj->getGeometry(geoId2);
                if (geo->isDerivedFrom<Part::GeomBoundedCurve>()) {
                    widget->addSelect(geoId2);
                    return true;
                }
            }
        }

        return false;
    }

    void DrawSketchHandlerFillet::executeCommands()
	{    
        SketcherObj* obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        SketcherObjWidget* widget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget();

        bool construction = false;
        bool isChamfer = constructionMethod() == ConstructionMethod::Chamfer;

        {
            Base::Vector3d refPnt1(firstPos.x, firstPos.y, 0.f);
            Base::Vector3d refPnt2(secondPos.x, secondPos.y, 0.f);

            const Part::Geometry* geo1 = obj->getGeometry(geoId1);
            const Part::Geometry* geo2 = obj->getGeometry(geoId2);


            double radius = 0;
            if (geo1->is<Part::GeomLineSegment>() && geo2->is<Part::GeomLineSegment>()) {
                // guess fillet radius
                auto* line1 = static_cast<const Part::GeomLineSegment*>(geo1);
                auto* line2 = static_cast<const Part::GeomLineSegment*>(geo2);

                radius = Part::suggestFilletRadius(line1, line2, refPnt1, refPnt2);
                if (radius < 0) {
                    return;
                }
            }

            int filletGeoId = obj->getHighestCurveIndex() + (isChamfer ? 2 : 1);

            // create fillet between lines
            try {
                obj->fillet(geoId1, geoId2,
                    Base::Vector3d(firstPos.x,
                    firstPos.y,0),
                    Base::Vector3d(secondPos.x,
                        secondPos.y,0),radius,true,preserveCorner,isChamfer
                    );
                widget->removeSelect({geoId1,geoId2});
            }
            catch (const Base::CADKernelError& e) {
                CORE_ERROR(e.what());
            }
            catch (const Base::ValueError& e) {
                CORE_ERROR(e.what());
            }
        }
    }

    void DrawSketchHandlerFillet::onButtonPressed(Base::Vector2d onSketchPos)
    {
        this->updateDataAndDrawToPosition(onSketchPos);
        if (canGoToNextMode()) {
            moveToNextMode();
        }
    }

}
