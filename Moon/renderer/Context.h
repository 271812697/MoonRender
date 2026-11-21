#pragma once
// The editor's name for the engine's Context: the class itself lives in the engine
// (MoonRender/include/Core/Context.h), which knows nothing about Qt or the editor. Everything in
// the editor that says Editor::Core::Context keeps compiling and keeps referring to the same type,
// which also means ServiceLocator::Get<Editor::Core::Context>() and Get<Core::Context>() are the
// same lookup.
#include <Core/Context.h>

namespace Editor::Core
{
	using Context = ::Core::Context;
}
