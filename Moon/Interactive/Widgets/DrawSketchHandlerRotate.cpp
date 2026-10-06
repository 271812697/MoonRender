#include "Interactive/Widgets/DrawSketchHandlerRotate.h"
#include "Sketcher/SketcherObjWidget.h"
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

	
	DrawSketchHandlerRotate::DrawSketchHandlerRotate(const std::string& name) :SupperClass(name),
         deleteOriginal(false)
        , cloneConstraints(false)
        , length(0.0)
        , startAngle(0.0)
        , endAngle(0.0)
        , totalAngle(0.0)
        , individualAngle(0.0)
        , numberOfCopies(0)
    {

	}

	DrawSketchHandlerRotate::~DrawSketchHandlerRotate()
	{
		//delete m_internal;
	}

	void DrawSketchHandlerRotate::onUpdate()
	{
        DrawSketchHandler::onUpdate();
        SketcherObjWidget* widget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget();
        if (widget) {
            listOfGeoIds = widget->getSelectIds();
        }
	}

	void DrawSketchHandlerRotate::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        switch (state()) {
        case SelectMode::SeekFirst: {
            centerPoint = onSketchPos;
        } break;
        case SelectMode::SeekSecond: {
            length = (onSketchPos - centerPoint).Length();
            startAngle = (onSketchPos - centerPoint).Angle();

            startPoint = onSketchPos;

            CreateAndDrawShapeGeometry();
        } break;
        case SelectMode::SeekThird: {
            endAngle = (onSketchPos - centerPoint).Angle();
            endPoint = centerPoint + length * Base::Vector2d(cos(endAngle), sin(endAngle));

            double angle1 = endAngle - startAngle;
            double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * 3.141592653589793;
            totalAngle = abs(angle1 - totalAngle) < abs(angle2 - totalAngle) ? angle1 : angle2;

            CreateAndDrawShapeGeometry();
        } break;
        default:
            break;
        }
	}


    void DrawSketchHandlerRotate::createShape(bool onlyeditoutline)
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();

        ShapeGeometry.clear();

        if (state() == SelectMode::SeekSecond) {
            if (length > Precision::Confusion()) {
                addLineToShapeGeometry(
                    Base::Vector3d(centerPoint.x,centerPoint.y,0.0),
                    Base::Vector3d(startPoint.x,startPoint.y,0.0),
                    false
                );
            }
            return;
        }

        int numberOfCopiesToMake = numberOfCopies;
        if (numberOfCopies == 0) {
            numberOfCopiesToMake = 1;
            deleteOriginal = true;
        }
        else {
            deleteOriginal = false;
        }

        individualAngle = totalAngle / numberOfCopiesToMake;

        for (int i = 1; i <= numberOfCopiesToMake; i++) {
            for (auto& geoId : listOfGeoIds) {
                const Part::Geometry* pGeo = Obj->getGeometry(geoId);
                auto geoUniquePtr = std::unique_ptr<Part::Geometry>(pGeo->copy());
                Part::Geometry* geo = geoUniquePtr.get();

                //if (!onlyeditoutline)
                {
                    geo->reverseIfReversed();  // make sure we don't have reversed conics
                }

                double angle = individualAngle * i;

                Base::Matrix4D matrix(Base::Vector3d(centerPoint.x, centerPoint.y, 0.0), Base::Vector3d(0, 0, 1), angle);
                geo->transform(matrix);

                ShapeGeometry.emplace_back(std::move(geoUniquePtr));
            }
        }

        if (onlyeditoutline) {
            // Add the lines to show angle
            addLineToShapeGeometry(Base::Vector3d(centerPoint.x, centerPoint.y, 0.0), Base::Vector3d(startPoint.x, startPoint.y, 0.0), true);

            addLineToShapeGeometry(Base::Vector3d(centerPoint.x, centerPoint.y, 0.0), Base::Vector3d(endPoint.x, endPoint.y, 0.0), true);
        }
        else {


        }
    }

    void DrawSketchHandlerRotate::deleteOriginalGeos()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (Obj) {
			Obj->deleteGeometries(listOfGeoIds);
        }
    }

    bool DrawSketchHandlerRotate::canGoToNextMode()
    {
        if (state() == SelectMode::SeekThird && fabs(totalAngle) < Precision::Confusion()) {
            // Prevent validation rotation of 0deg.
            return false;
        }
        return true;
    }

    void DrawSketchHandlerRotate::executeCommands()
	{    
        createShape(false);
		SupperClass::executeCommands();
        if (deleteOriginal) {
			deleteOriginalGeos();
		}   
    }
    void DrawSketchHandlerRotate::onKeyPress(const std::string& key)
    {
        if (key == "A") {
            numberOfCopies++;
        }
        else if (key == "S") {
            numberOfCopies--;
            if (numberOfCopies < 0) {
                numberOfCopies = 0;
            }
        }
    }
}
