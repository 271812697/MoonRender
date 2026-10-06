#include "Interactive/Im3DRenderer.h"
#include "Interactive/Widgets/DrawSketchHandlerTrimming.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjWidget.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "core/log.h"


namespace MOON {
	class DrawSketchHandlerTrimming::Internal {
	public:
		Internal(DrawSketchHandlerTrimming* s) :self(s) {
		}
		~Internal() {
		}
	private:
		friend DrawSketchHandlerTrimming;
		DrawSketchHandlerTrimming* self = nullptr;
		Base::Vector2d a;
		Base::Vector2d b;
		double u1;
		double u2;
		int trimCurveId = -1;
	};
	DrawSketchHandlerTrimming::DrawSketchHandlerTrimming(const std::string& name) : DrawSketchHandler(name)
	{
		m_internal = new Internal(this);
	}
	DrawSketchHandlerTrimming::~DrawSketchHandlerTrimming()
	{
		delete m_internal;
	}
	void DrawSketchHandlerTrimming::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		if (m_internal->trimCurveId!=-1) {
			renderer->pushColor({255,0,255,0});
			renderer->drawPoint(plane.valueEigen(m_internal->a), 15);
			renderer->drawPoint(plane.valueEigen(m_internal->b), 15);
			renderer->popColor();		
		}
	}
	void DrawSketchHandlerTrimming::onSetActive(bool flag)
	{
		DrawSketchHandler::onSetActive(flag);
	}

	void DrawSketchHandlerTrimming::onMouseMove()
	{
		DrawSketchHandler::onMouseMove();
		updateTrimData();
	}

	void DrawSketchHandlerTrimming::onLeftMousePressed()
	{
		updateTrimData();
		auto sketchObj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (m_internal->trimCurveId!=-1) {
			if (sketchObj->trim(m_internal->trimCurveId, m_internal->u1, m_internal->u2,
				Base::Vector3d(m_internal->a.x, m_internal->a.y, 0),
				Base::Vector3d(m_internal->b.x, m_internal->b.y, 0))) {
				CORE_DEBUG("DrawSketchHandlerTrimming::onLeftMousePressed");
			}
		}
	}
	void DrawSketchHandlerTrimming::updateTrimData()
	{
		auto sketchObj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (sketchObj) {
			Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
			Base::Matrix4D pla(
				mat.data[0], mat.data[1], mat.data[2], mat.data[3],
				mat.data[4], mat.data[5], mat.data[6], mat.data[7],
				mat.data[8], mat.data[9], mat.data[10], mat.data[11],
				mat.data[12], mat.data[13], mat.data[14], mat.data[15]
			);
			SketcherObjWidget* widget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget();
			int GeoId = widget != nullptr ? widget->getPickGeoIndex(onSketchPos, pla) : -1;
		
			if (GeoId != -1) {
				CORE_DEBUG("DrawSketchHandlerTrimming::onMouseMove PickGeo")
					int GeoId1, GeoId2;
				Base::Vector3d intersect1, intersect2;

				if (sketchObj->seekTrimPoints(GeoId,
					Base::Vector3d(onSketchPos.x, onSketchPos.y, 0),
					GeoId1,
					intersect1,
					GeoId2,
					intersect2, m_internal->u1, m_internal->u2)) {
					m_internal->a = { intersect1.x, intersect1.y };
					m_internal->b = { intersect2.x, intersect2.y };
					m_internal->trimCurveId = GeoId;
				}
				else {
					m_internal->trimCurveId = -1;
				}
			}
			else
			{
				m_internal->trimCurveId = -1;
			}
		}
	}
}
