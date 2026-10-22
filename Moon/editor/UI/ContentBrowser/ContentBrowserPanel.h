#pragma once

#include <QDockWidget>
#include <QHash>
#include <QPixmap>
#include <QSize>
#include <QStandardItemModel>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QString>

#include <memory>

class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace MOON
{
	class MaterialThumbnailRenderer;

	/** The two asset kinds the content browser knows. */
	enum class ContentAssetType
	{
		Material,
		Texture,
		Shader
	};

	namespace ContentRoles
	{
		enum Role
		{
			PathRole = Qt::UserRole + 1,
			TypeRole,
			SubtitleRole,
			BaseColorRole
		};
	}

	/** QStandardItemModel that hands the selected assets to a drop target as file URIs, so a
	 * material or a texture can be dragged into the viewport or onto a material slot. */
	class ContentBrowserModel : public QStandardItemModel
	{
	public:
		using QStandardItemModel::QStandardItemModel;

		QStringList mimeTypes() const override;
		QMimeData* mimeData(const QModelIndexList& p_indexes) const override;
		Qt::DropActions supportedDragActions() const override;
	};

	/** One tile of the content browser: a thumbnail over a checkerboard, the asset name on two
	 * lines, its kind and the coloured rule under the tile (cyan for materials, red for
	 * textures), like the Unreal content browser this is modelled on.
	 *
	 * Materials have no rendered preview yet: the tile draws a shaded sphere tinted with the
	 * base colour read from the .mat, which is enough to tell the assets apart. */
	class ContentTileDelegate : public QStyledItemDelegate
	{
	public:
		explicit ContentTileDelegate(QObject* p_parent = nullptr);

		QSize sizeHint(const QStyleOptionViewItem& p_option, const QModelIndex& p_index) const override;
		void paint(QPainter* p_painter, const QStyleOptionViewItem& p_option,
			const QModelIndex& p_index) const override;
	};

	/** The content browser: a folder tree for Moon/Data/Engine/Materials and .../Textures on
	 * the left and a thumbnail grid for the selected folder on the right. Materials are
	 * .mat files, textures are the image files the engine can load. */
	class ContentBrowserPanel : public QDockWidget
	{
		Q_OBJECT

	public:
		explicit ContentBrowserPanel(QWidget* p_parent = nullptr);
		~ContentBrowserPanel() override;

		/** Re-scans the selected folder (and the tree roots when p_tree too). */
		void Refresh(bool p_tree = false);

	protected:
		/** The material thumbnails need the render module's context, which only exists once the
		 * viewport has been painted; a panel built before that gets its queue going here. */
		void showEvent(QShowEvent* p_event) override;

	private:
		void BuildUi();
		void PopulateTree();
		void AddFolderItem(QTreeWidgetItem* p_parent, const QString& p_label, const QString& p_path,
			ContentAssetType p_type);
		void ShowFolder(const QString& p_folder, ContentAssetType p_type);
		void UpdateCount();
		void UpdateBreadcrumb();

		void ImportTextures();
		void NewMaterial();
		void NewShader();
		void OnActivated(const QModelIndex& p_index);
		void OnContextMenu(const QPoint& p_pos);
		void CopySelected();
		void RevealSelected();
		void RenameSelected();
		void DeleteSelected();
		void OnTreeChanged();

		/** Queues a rendered sphere thumbnail for every material in the current folder that has
		 * no cached one yet. The renders run one per timer tick so the grid stays responsive. */
		void EnqueueMaterialThumbnails();
		void RenderNextThumbnail();
		void ApplyThumbnail(const QString& p_path, const QPixmap& p_thumbnail);
		QString ThumbnailKey(const QString& p_path) const;

		/** The material editor changed a material: drop its cached tile and re-render it
		 * (debounced, because a slider drag emits once per move). */
		void OnMaterialThumbnailDirty(const QString& p_key);

		QString SelectedPath() const;
		QString SelectedFolder() const;

		QTreeWidget* m_tree = nullptr;
		QListView* m_view = nullptr;
		QLineEdit* m_search = nullptr;
		QLabel* m_breadcrumb = nullptr;
		QLabel* m_count = nullptr;
		ContentBrowserModel* m_model = nullptr;
		QSortFilterProxyModel* m_proxy = nullptr;

		QString m_currentFolder;
		ContentAssetType m_currentType = ContentAssetType::Material;
		/** Texture thumbnails, keyed by "path|lastModified". Loading every image of a folder on
		 * each visit would make switching folders flicker; the cache is small (the folder's own
		 * assets) and is dropped when the panel refreshes. */
		QHash<QString, QPixmap> m_thumbnails;

		/** Rendered material spheres, keyed by path and modification time. */
		std::unique_ptr<MaterialThumbnailRenderer> m_thumbnailRenderer;
		QTimer* m_thumbnailTimer = nullptr;
		QTimer* m_thumbnailDirtyTimer = nullptr;
		/** File name of the material whose tile is stale (":Materials/X.mat" -> "X.mat"). */
		QString m_dirtyThumbnailName;
		QStringList m_thumbnailQueue;
		QHash<QString, QPixmap> m_materialThumbnails;
		/** How often a material whose render came back empty was retried. The context may not
		 * exist yet when the panel is built (the viewport creates it on its first paint). */
		QHash<QString, int> m_thumbnailAttempts;
	};
}
