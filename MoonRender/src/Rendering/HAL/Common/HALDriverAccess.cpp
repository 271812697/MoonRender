#include <Rendering/HAL/Common/HALDriverAccess.h>

#include <Core/Context.h>
#include <Core/RenderModuleHost.h>
#include <Rendering/Context/Driver.h>

Rendering::Context::Driver* Rendering::HAL::CurrentDriver()
{
	::Core::RenderModuleHost* const module = ::Core::RenderModuleHost::Instance();
	if (module == nullptr || module->GetContext() == nullptr)
	{
		return nullptr;
	}
	return module->GetContext()->driver.get();
}
