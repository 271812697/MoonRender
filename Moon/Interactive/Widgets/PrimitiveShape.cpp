#include "Interactive/Widgets/PrimitiveShape.h"
#include "Interactive/Im3DRenderer.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "Interactive/Interactive/CallbackCommand.h"
#include "renderer/SceneView.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include "core/component/CTopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "TopoShape.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <Precision.hxx>

namespace MOON {
	
	PrimitiveShape::PrimitiveShape(const std::string& name) :EventWidget(name)
	{
		this->KeyEventCallbackCommand = CallbackCommand::New();
		this->KeyEventCallbackCommand->SetClientData(this);
		this->KeyEventCallbackCommand->SetCallback(PrimitiveShape::ProcessKeyEvents);
		setActive(false);
	}
	PrimitiveShape::~PrimitiveShape()
	{
	}
	void PrimitiveShape::createTopoShape()
	{
	}

	void PrimitiveShape::onKeyPress(const std::string& key)
	{
		if (key == "RETURN")
		{
			createTopoShape();
		}
	}
}