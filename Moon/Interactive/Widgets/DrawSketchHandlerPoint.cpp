#include "Interactive/Widgets/DrawSketchHandlerPoint.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"

namespace MOON {
	class DrawSketchHandlerPoint::Internal {
	public:
		Internal(DrawSketchHandlerPoint*s) :self(s){
            editPoint = Base::Vector2d{ 0.0,0.0 };
		}
		~Internal() {
		
		}
	private:
		friend DrawSketchHandlerPoint;
		DrawSketchHandlerPoint* self = nullptr;
        Base::Vector2d editPoint;
	};
	
	DrawSketchHandlerPoint::DrawSketchHandlerPoint(const std::string& name) :SupperClass(name),m_internal(new Internal(this))
	{

	}

	DrawSketchHandlerPoint::~DrawSketchHandlerPoint()
	{
		delete m_internal;
	}

	void DrawSketchHandlerPoint::onUpdate()
	{
        DrawSketchHandler::onUpdate();

		renderer->drawPoint(plane.valueEigen(m_internal->editPoint), 12);
	}

	void DrawSketchHandlerPoint::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			m_internal->editPoint = onSketchPos;
			drawPositionAtCursor(onSketchPos);
			CreateAndDrawShapeGeometry();
		} break;
		default:
			break;
		}
	}


    void DrawSketchHandlerPoint::createShape(bool onlyeditoutline)
    {
        ShapeGeometry.clear();
		addPointToShapeGeometry(Base::Vector3d(m_internal->editPoint.x, m_internal->editPoint.y,0.0),false);
    }

}