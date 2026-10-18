#include "Interactive/Widgets/DrawSketchHandlerLine.h"
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
#include <ThirdParty/imgui/implot/implotCustom.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MOON {
	namespace
	{
		// Draw one double arrow between two screen points, showing the given
		// sketch-space length. Used for the horizontal/vertical components.
		void DrawOneArrow(const ImVec2& from, const ImVec2& to, float value, ImU32 col)
		{
			const ImVec2 delta(to.x - from.x, to.y - from.y);
			const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
			if (len < 4.0f) {
				return;
			}
			// Screen Y grows downwards: a direction (dx,dy) maps to transform
			// angle -atan2(dy,dx).
			const float angleDeg = -std::atan2(delta.y, delta.x) * 180.0f / 3.14159265358979f;
			const float thickness = 2.5f;

			char text[64];
			snprintf(text, sizeof(text), "%.2f", value);
			ImPlotCustom::drawDoubleArrow(
				ImPlotCustom::Transform(from.x, from.y, angleDeg),
				col,
				len,
				thickness,
				text
			);
		}

		// Both component arrows share the mouse position as one endpoint:
		// - horizontal arrow: from sketch (start.x, end.y) to the mouse
		// - vertical arrow:   from sketch (end.x, start.y) to the mouse
		void DrawGluedComponentArrows(
			const ImVec2& mouse,
			const ImVec2& horizFrom,
			const ImVec2& vertFrom,
			float deltaX,
			float deltaY
		)
		{
			const ImU32 col = IM_COL32(255, 255, 0, 255);
			DrawOneArrow(horizFrom, mouse, deltaX, col);
			DrawOneArrow(vertFrom, mouse, deltaY, col);
		}
	}
	class DrawSketchHandlerLine::Internal {
	public:
		Internal(DrawSketchHandlerLine*s) :self(s){
            editPoint = Base::Vector2d{ 0.0,0.0 };
		}
		~Internal() {
		
		}
	private:
		friend DrawSketchHandlerLine;
		DrawSketchHandlerLine* self = nullptr;
        Base::Vector2d editPoint;
	};
	
	DrawSketchHandlerLine::DrawSketchHandlerLine(const std::string& name,LineConstructionMethod constrMethod) :SupperClass(name, constrMethod),m_internal(new Internal(this)),  length(0.0)
		, lengthSign(0)
		, widthSign(0)
		, capturedDirection(0.0, 0.0)
	{

	}

	DrawSketchHandlerLine::~DrawSketchHandlerLine()
	{
		delete m_internal;
	}

	void DrawSketchHandlerLine::onUpdate()
	{
        DrawSketchHandler::onUpdate();

		renderer->drawPoint(plane.valueEigen(m_internal->editPoint), 12);

		if (state() == SelectMode::SeekSecond) {
			auto unwrapDegrees = [](float current, float previous) {
				float delta = current - previous;
				while (delta > 180.0f) delta -= 360.0f;
				while (delta < -180.0f) delta += 360.0f;
				return previous + delta;
			};

			const Maths::FVector2 screenMouse = m_sceneView->worldToScreen(getWorldPosFromSketchPos(endPoint));
			const Maths::FVector2 screenStart = m_sceneView->worldToScreen(getWorldPosFromSketchPos(startPoint));
			const Maths::FVector2 screenHorizFrom = m_sceneView->worldToScreen(
				getWorldPosFromSketchPos(Base::Vector2d(startPoint.x, endPoint.y))
			);
			const Maths::FVector2 screenVertFrom = m_sceneView->worldToScreen(
				getWorldPosFromSketchPos(Base::Vector2d(endPoint.x, startPoint.y))
			);
			DrawGluedComponentArrows(
				ImVec2(screenMouse.x, screenMouse.y),
				ImVec2(screenHorizFrom.x, screenHorizFrom.y),
				ImVec2(screenVertFrom.x, screenVertFrom.y),
				static_cast<float>(std::fabs(startPoint.x - endPoint.x)),
				static_cast<float>(std::fabs(startPoint.y - endPoint.y))
			);

			// Angle annotation: an arc centered on the line start with radius
			// half the current screen length, sweeping from the horizontal
			// axis to the segment direction. The label is the angle the
			// segment makes with the sketch X axis.
			const Base::Vector2d sketchDir = endPoint - startPoint;
			const float screenDX = screenMouse.x - screenStart.x;
			const float screenDY = screenMouse.y - screenStart.y;
			const float screenDist = std::sqrt(screenDX * screenDX + screenDY * screenDY);
			if (screenDist > 8.0f) {
				const float rawSketchDeg = std::atan2(
					static_cast<float>(sketchDir.y),
					static_cast<float>(sketchDir.x)
				) * 180.0f / 3.14159265358979f;
				const float rawScreenDeg = std::atan2(screenDY, screenDX)
					* 180.0f / 3.14159265358979f;

				// Keep the angle continuous across the +/-180 boundary: add
				// the smallest change from the previous value instead of using
				// atan2's wrapped [-180,180] result directly.
				m_sketchAngleAccumDeg = unwrapDegrees(rawSketchDeg, m_sketchAngleAccumDeg);
				m_screenAngleAccumDeg = unwrapDegrees(rawScreenDeg, m_screenAngleAccumDeg);

				char angleText[64];
				snprintf(angleText, sizeof(angleText), "%.1f", m_sketchAngleAccumDeg);
				ImPlotCustom::drawDoubleArcArrow(
					ImPlotCustom::Transform(screenStart.x, screenStart.y, 0.0f),
					IM_COL32(255, 255, 0, 255),
					screenDist * 0.5f,
					m_screenAngleAccumDeg,
					2.5f,
					angleText
				);
			}
		}
	}

	void DrawSketchHandlerLine::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			drawPositionAtCursor(onSketchPos);

			startPoint = onSketchPos;
			endPoint = startPoint;
			m_sketchAngleAccumDeg = 0.0f;
			m_screenAngleAccumDeg = 0.0f;

			//seekAndRenderAutoConstraint(sugConstraints[0], onSketchPos, Base::Vector2d(0.f, 0.f));
		} break;
		case SelectMode::SeekSecond: {
			//drawDirectionAtCursor(onSketchPos, startPoint);

			endPoint = onSketchPos;

			try {
				CreateAndDrawShapeGeometry();
			}
			catch (const Base::ValueError&) {
			}  // equal points while hovering raise an objection that can be safely ignored

			//seekAndRenderAutoConstraint(sugConstraints[1], onSketchPos, onSketchPos - startPoint);
		} break;
		default:
			break;
		}
	}


    void DrawSketchHandlerLine::createShape(bool onlyeditoutline)
    {
		Q_UNUSED(onlyeditoutline);
		ShapeGeometry.clear();

		Base::Vector2d vecL = endPoint - startPoint;
		length = vecL.Length();
		if (length > Precision::Confusion()) {

			addLineToShapeGeometry(Base::Vector3d(startPoint.x,startPoint.y,0), Base::Vector3d(endPoint.x,endPoint.y,0), false);
		}
    }

	void DrawSketchHandlerLine::onReset()
	{
		lengthSign = 0;
		widthSign = 0;
		capturedDirection = Base::Vector2d(0.0, 0.0);
		m_sketchAngleAccumDeg = 0.0f;
		m_screenAngleAccumDeg = 0.0f;
	}

}
