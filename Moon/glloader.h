#pragma once
class QOpenGLContext;

class GlLoader {
public:
	static bool CustomLoadGL(void* load);
};

/** Loads the GL entry points the editor's own code calls through, from p_context (which has
 * to be current). Called once, when the viewport's context first exists: it is the one place
 * in the editor that knows a context has just been created.
 *
 * The render module loads its own pointers through its driver; this is the editor side. */
bool InstallGlLoader(QOpenGLContext* p_context);
