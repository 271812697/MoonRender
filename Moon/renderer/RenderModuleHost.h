#pragma once
// The editor's name for the engine's render module host: the class itself lives in the engine
// (MoonRender/include/Core/RenderModuleHost.h) and knows nothing about Qt. What the editor brings
// to it is a context: Editor::Core::QtGLContextHost, which implements the engine's
// Rendering::Context::IGLContextHost with a QOpenGLContext in the editor's share group.
#include <Core/RenderModuleHost.h>

namespace Editor::Core
{
	using RenderModuleHost = ::Core::RenderModuleHost;
}
