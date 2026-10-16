#include "Interactive/Widgets/Measurement.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"


namespace MOON {
	struct Measure
	{
		Eigen::Vector3f start;
		Eigen::Vector3f end;
	};
	std::vector<Measure> g_measures;
	bool selectingStart = false;
	Measurement::Measurement(const std::string& name) :EventWidget(name)
	{
		// Define widget events
		this->CallbackMapper->SetCallbackMethod(ExecuteCommand::LeftButtonPressEvent, GizmoEvent::NoModifier, 0,
			0, 0, WidgetEvent::Select, this, Measurement::MousePressed);
		this->CallbackMapper->SetCallbackMethod(ExecuteCommand::MouseMoveEvent, GizmoEvent::NoModifier, 0,
			0, 0, WidgetEvent::Move3D, this, Measurement::mouseMove);
		setActive(true);

	}
	Measurement::~Measurement()
	{
	}
	void Measurement::onUpdate()
	{

		for (const auto& m : g_measures) {
			renderer->drawLine(m.start, m.end, 2.0,Eigen::Vector4<uint8_t>{ 255,0, 255, 0 });
			float distance = (m.start - m.end).norm();
			Eigen::Vector3f delta = m.start - m.end;
			
			Eigen::Vector3f mid = (m.start + m.end) * 0.5f;
			Eigen::Vector2f spos=renderer->worldToScreen(mid);
			std::string text = std::to_string(distance) + "\n" + "x:"+ std::to_string(abs(delta.x())) + "\n" + "y:" + std::to_string(abs(delta.y())) + "\n" + "z:" + std::to_string(abs(delta.z()));
			ImGui::GetForegroundDrawList()->AddText({spos.x(),spos.y()}, IM_COL32(255, 255, 0, 255), text.c_str());
			//renderer->drawText3D(mid, std::to_string(distance), Eigen::Vector4<uint8_t>{ 255, 255, 255, 255 }, 16);
		}
		//auto rc = m_sceneView->GetRoaterCenter();
		//Eigen::Vector3f center = { rc.x,rc.y,rc.z };
		//renderer->translation(renderer->makeId("rotaterCenter"), center);
	}
	void Measurement::onSetActive(bool flag)
	{
		g_measures.clear();
	}
	void Measurement::onMouseClicked()
	{
		// The point under the cursor comes from the picking target, which carries the
		// world position of every pixel next to the id of what is drawn there: no ray
		// through the scene to cast, and nothing to build for it.
		Maths::FVector3 out;
		if (m_sceneView->GetPickWorldPosition(out)) {
			Eigen::Vector3f pos = { out.x,out.y,out.z };
			if (!selectingStart) {
				Measure m;
				m.start = pos;
				m.end = pos;
				g_measures.push_back(m);
				selectingStart = true;
			}
			else {
				if (g_measures.size()) {
					g_measures.back().end = pos;
					selectingStart = false;
				}

			}
		}
	}
	void Measurement::onMouseMove()
	{
		if (selectingStart) {
			Maths::FVector3 out;
			if (m_sceneView->GetPickWorldPosition(out)) {
				Eigen::Vector3f pos = { out.x,out.y,out.z };
				if ( g_measures.size()) {
					g_measures.back().end = pos;
				}
			}		
		}
	}

	void Measurement::SetEnabled(int v)
	{
		EventWidget::SetEnabled(v);
	}
	void Measurement::MousePressed(AbstractWidget* w)
	{
		Measurement* self = reinterpret_cast<Measurement*>(w);
		self->onMouseClicked();
	}
	
	void Measurement::mouseMove(AbstractWidget* w)
	{
		Measurement* self = reinterpret_cast<Measurement*>(w);
		self->onMouseMove();
	}
}
