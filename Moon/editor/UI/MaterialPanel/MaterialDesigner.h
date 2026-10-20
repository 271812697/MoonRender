#pragma once
#include <QIcon>
#include <QImage>
#include <QString>
#include <QWidget>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Rendering/Data/FeatureSet.h>
#include <Rendering/Data/Material.h>

class QLabel;
class QListWidget;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace Core::Resources { class Material; }
namespace MOON

{
	class PropertyComponent;

	/** The material designer: an .ovmat asset edited through the parameters its shader declares.
	 *
	 * Why it exists: a material is an instance of a shader - the shader's package knows the
	 * parameters (name, type, texture slot, default), so the form is generated instead of
	 * hand-written, and it stays correct when the shader gains a parameter (the same reflection
	 * Material reads, see Shader::GetParameters).
	 *
	 * The asset lives where the engine keeps its materials (Moon/Data/Engine/Materials): the list
	 * is scanned from disk, an entry is loaded through the MaterialManager (so every actor that
	 * references it shares the instance), edits are pushed into the loaded material as they
	 * happen - the preview shows them live - and Save writes the .ovmat back.
	 *
	 * Assign gives the material to the selected actor (or to the AllFaces anchor of a topology
	 * shape), which is the point where a designed material becomes part of the scene. */
	class MaterialDesigner : public QWidget
	{
		Q_OBJECT
	public:
		explicit MaterialDesigner(QWidget* p_parent = nullptr);
		~MaterialDesigner() override;

		Core::Resources::Material* GetMaterial() const;

		/** Re-scans the material and texture folders and rebuilds the form for the current
		 * material. Called when the panel switches to this page, so a shader that was hot
		 * reloaded in the Shader page shows its new parameters here. */
		void Refresh();

		/** The preview's last frame, downscaled. The browser shows it next to the material's name,
		 * so the list reads as a set of look previews rather than as file names. */
		void SetThumbnail(const QImage& p_image);

	signals:
		void materialShown(Core::Resources::Material* p_material);
		void materialEdited();
		void statusMessage(const QString& p_message, bool p_error);

	public:
		/** One texture asset the texture slots can point at. The key is the resource path the
		 * serializer stores (" :Textures/plane.png"). */
		struct TextureAsset
		{
			std::string key;
			QString label;
			QIcon icon;
		};

	private:
		/** One undo step: everything the designer can change on an instance - the property values,
		 * the feature set (which selects the variant) and the renderer state overrides. The shader
		 * itself is the definition and is not part of it. */
		struct Snapshot
		{
			Rendering::Data::Material::PropertyMap properties;
			Rendering::Data::FeatureSet features;
			bool blendable = false;
			Rendering::Settings::EBlendMode blendMode = Rendering::Settings::EBlendMode::BLEND_OPAQUE;
			bool transparent = false;
			bool backfaceCulling = true;
			bool frontfaceCulling = false;
			bool depthTest = true;
			bool depthWriting = true;
			bool castShadows = true;
			bool receiveShadows = true;
		};

		void BuildUi();
		void ScanTextures();

		void RefreshMaterialList(const std::string& p_select = {});
		void OnMaterialSelected(int p_index);
		void RebuildForm();

		void NewMaterial();
		void DuplicateMaterial();
		void RevertMaterial();
		void DeleteMaterial();
		void SaveMaterial();
		void AssignToSelection();

		Snapshot Capture() const;
		void ApplySnapshot(const Snapshot& p_snapshot);
		void ResetHistory();
		void Undo();
		void Redo();

		void MarkDirty();
		void SetStatus(const QString& p_text, bool p_error = false);

		/** Recompiles the current material''s shader with exactly its feature set (filament''s
		 * model: features are fixed at compile time) and hot swaps the result into the instance.
		 * The material''s properties and state are captured and restored across the swap. */
		void RecompileMaterialFeatures();

		/** Feature names per shader path, captured from the first package the designer saw. A
		 * fixed-feature recompile shrinks the package''s variant union, and the form must keep
		 * offering every declared feature, not just the enabled ones. */
		std::map<std::string, std::vector<std::string>> m_declaredFeatures;

		QListWidget* m_browser = nullptr;
		QToolButton* m_new = nullptr;
		QToolButton* m_duplicate = nullptr;
		QToolButton* m_revert = nullptr;
		QToolButton* m_save = nullptr;
		QToolButton* m_assign = nullptr;
		QScrollArea* m_scroll = nullptr;
		QWidget* m_form = nullptr;
		QVBoxLayout* m_formLayout = nullptr;
		QLabel* m_status = nullptr;

		std::string m_materialsRoot;
		std::string m_shadersRoot;
		std::string m_texturesRoot;
		std::string m_selectedKey;
		std::vector<std::string> m_materialKeys;
		std::vector<TextureAsset> m_textures;
		/** The last preview of each material the designer has shown, keyed by resource path.
		 * Filled as materials are visited and refreshed whenever the current one is edited. */
		std::map<std::string, QIcon> m_thumbnails;
		Core::Resources::Material* m_material = nullptr;
		std::unique_ptr<PropertyComponent> m_component;
		bool m_dirty = false;

		/** The undo/redo history of the current material, and the state the current edit burst
		 * started from (a slider drag is one undo step, not one per pixel). */
		std::vector<Snapshot> m_undo;
		std::vector<Snapshot> m_redo;
		Snapshot m_baseline;
		bool m_hasBaseline = false;
		qint64 m_lastEditMs = 0;
	};
}
