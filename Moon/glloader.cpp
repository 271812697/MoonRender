#include "glloader.h"
#include <glad/glad.h>

#include <QOpenGLContext>

namespace
{
	/** The context the loader resolves addresses from. glad takes a plain function pointer
	 * and calls it back for every entry point, so the context it reads from has to be
	 * reachable from a free function. */
	QOpenGLContext* s_contextForLoader = nullptr;

	void* ResolveProcAddress(const char* p_name)
	{
		return s_contextForLoader != nullptr
			? reinterpret_cast<void*>(s_contextForLoader->getProcAddress(p_name))
			: nullptr;
	}
}

bool GlLoader::CustomLoadGL(void* load)
{
	static bool initFlag = false;
	if (!initFlag) {
		initFlag = true;
		if (!gladLoadGLLoader((GLADloadproc)load)) {
			return false;
		}
	}
	return true;
}

bool InstallGlLoader(QOpenGLContext* p_context)
{
	if (p_context == nullptr)
	{
		return false;
	}
	s_contextForLoader = p_context;
	return GlLoader::CustomLoadGL(reinterpret_cast<void*>(&ResolveProcAddress));
}
