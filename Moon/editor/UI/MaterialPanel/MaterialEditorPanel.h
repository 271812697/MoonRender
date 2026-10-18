#pragma once
#include <QtWidgets/QDockWidget>

namespace MOON
{
	/** The material editor: pick a shader from the engine's shader folder, edit its source, and
	 * hot reload it into the running editor.
	 *
	 * Why it exists: it is the end to end check of the shader pipeline - the source is compiled
	 * in process by MoonMatcLib (the same parser the build uses), the package is handed to
	 * ShaderLoader::CreateFromPackage, and the resulting Shader is swapped into the materials
	 * that were using the old one. Nothing is rebuilt and nothing is restarted; a syntax error
	 * shows up in the panel with the .ovfx line the driver complained about, because the
	 * generated stages carry #line directives back into the file.
	 *
	 * The preview is rendered by the viewport (it owns the GL context) - see
	 * ViewerWidget::RenderMaterialPreview - and comes back as an image this panel shows. */
	class MaterialEditorPanel : public QDockWidget
	{
		Q_OBJECT
	public:
		MaterialEditorPanel(QWidget* p_parent);
		~MaterialEditorPanel();

	protected:
		/** The preview needs the viewport's GL context, which only exists once the panel (part
		 * of the workspace) is shown - so the first preview happens here, not in the
		 * constructor. */
		void showEvent(QShowEvent* p_event) override;

	private:
		class MaterialEditorPanelInternal;
		MaterialEditorPanelInternal* mInternal = nullptr;
	};
}