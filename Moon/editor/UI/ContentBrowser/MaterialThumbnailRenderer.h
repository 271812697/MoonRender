#pragma once

#include <QImage>
#include <QString>

#include <memory>

namespace Editor::Panels
{
	class PreviewView;
}

namespace MOON
{
	/** Renders an .mat asset into the sphere thumbnail the content browser shows.
	 *
	 * The material editor's preview widget can only capture what it is showing, so it was not
	 * reusable for a grid of every material. This runs a PreviewView of its own on the render
	 * module's context: the same scene, light rig and sky pass the material panel uses, one
	 * frame per call, read back with AView::ReadFrame. One view is kept for the whole grid, so
	 * the second and later materials only pay for their own shader compile and frame.
	 *
	 * Every call makes the module context current for its duration; callers run from the UI
	 * thread (a timer), where no context is current. */
	class MaterialThumbnailRenderer
	{
	public:
		MaterialThumbnailRenderer();
		~MaterialThumbnailRenderer();

		MaterialThumbnailRenderer(const MaterialThumbnailRenderer&) = delete;
		MaterialThumbnailRenderer& operator=(const MaterialThumbnailRenderer&) = delete;

		/** Renders p_materialPath at p_size x p_size. Returns a null image when the render
		 * module's context is not up yet (the panel can exist before the viewport created it)
		 * or the material could not be loaded. */
		QImage Render(const QString& p_materialPath, int p_size);

	private:
		std::unique_ptr<Editor::Panels::PreviewView> m_view;
	};
}
