#pragma once
#include <QtWidgets/QWidget>
#include <QString>

#include <string>

namespace MOON
{
	/** The material editor: pick a shader from the engine's shader folder, edit its source, and
	 * hot reload it into the running editor.
	 *
	 * Why it exists: it is the end to end check of the shader pipeline - the source is compiled
	 * in process by MoonMatcLib (the same parser the build uses), the package is handed to
	 * ShaderLoader::CreateFromPackage, and the resulting Shader is swapped into the materials
	 * that were using the old one. Nothing is rebuilt and nothing is restarted; a syntax error
	 * shows up in the panel with the .shader line the driver complained about, because the
	 * generated stages carry #line directives back into the file.
	 *
	 * The preview is a view of its own (PreviewView): the MaterialPreviewWidget inside this
	 * panel renders it on the render module's context and pastes the frame, so the preview
	 * goes through the same scene renderer, lights and post-process as the viewport. */
	class MaterialEditorPanel : public QWidget
	{
		Q_OBJECT
	public:
		MaterialEditorPanel(QWidget* p_parent);
		~MaterialEditorPanel();

		/** Shows the panel on the Material page and selects the given .mat (key
		 * ":Materials/Name.mat"). Called by the content browser when a material is opened. */
		void OpenMaterial(const std::string& p_key);

		/** Shows the Shader page with the given .shader (key ":Shaders\\Sub\\Name.shader").
		 * Called by the content browser when a shader is opened. */
		void OpenShader(const std::string& p_key);

	signals:
		/** The edited material changed (a parameter or Save): the content browser's rendered
		 * tile for it is stale. p_key is the .mat resource key. */
		void materialThumbnailDirty(const QString& p_key);

	protected:
		/** The first preview happens here, not in the constructor: the dock may start in a hidden
		 * tab, and the preview widget only builds its shader once it has been painted. */
		void showEvent(QShowEvent* p_event) override;

	private:
		class MaterialEditorPanelInternal;
		MaterialEditorPanelInternal* mInternal = nullptr;
	};
}