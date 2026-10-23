#include "ContentBrowserPanel.h"
#include "editor/Language.h"
#include "MaterialThumbnailRenderer.h"

#include "Core/Global/ServiceLocator.h"
#include "Rendering/Resources/ShaderArchive.h"
#include "Core/ResourceManagement/MaterialManager.h"
#include "Core/ResourceManagement/ShaderManager.h"
#include "Core/Resources/Loaders/MaterialLoader.h"
#include "Core/Resources/Material.h"
#include "Rendering/Context/Driver.h"
#include "Rendering/Context/IGLContextHost.h"
#include "Tools/Utils/PathParser.h"
#include "editor/UI/MaterialPanel/MaterialEditorPanel.h"
#include "renderer/Context.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRadialGradient>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTimer>
#include <QStandardItem>
#include <QStyle>
#include <QTextLayout>
#include <QTextOption>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
	/** Loading a shader or saving a material creates GL objects; the panel runs from widget
	 * signals, where no context is current, so the module's context is bound for the call. */
	class ModuleContextScope
	{
	public:
		ModuleContextScope()
		{
			auto& context = Core::Global::ServiceLocator::Get<Editor::Core::Context>();
			m_host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
			m_ready = m_host != nullptr && m_host->MakeCurrent();
		}

		~ModuleContextScope()
		{
			if (m_ready)
			{
				m_host->DoneCurrent();
			}
		}

		explicit operator bool() const { return m_ready; }

	private:
		Rendering::Context::IGLContextHost* m_host = nullptr;
		bool m_ready = false;
	};

	const QString& EngineRoot()
	{
		static const QString root =
			QString::fromStdString(Tools::Utils::PathParser::GetExeDirectory()) + "/Moon/Data/Engine";
		return root;
	}

	const QStringList& ImageFilters()
	{
		static const QStringList filters = {
			"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.hdr", "*.exr", "*.dds"
		};
		return filters;
	}

	/** The colour a material tile is drawn with. The .mat stores the panel's sRGB value
	 * (u_Albedo, four floats); reading it here keeps the tile recognisable without compiling
	 * the shader or rendering a preview, and falls back to Moon's default grey. */
	QColor MaterialBaseColor(const QString& p_path)
	{
		const QColor fallback(184, 184, 184);

		QFile file(p_path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			return fallback;
		}
		const QString text = QString::fromUtf8(file.readAll());
		const int name = text.indexOf(QStringLiteral("<name>u_Albedo</name>"));
		if (name < 0)
		{
			return fallback;
		}
		const int begin = text.indexOf(QStringLiteral("<value>"), name);
		const int end = text.indexOf(QStringLiteral("</value>"), begin);
		if (begin < 0 || end < 0)
		{
			return fallback;
		}
		const QString block = text.mid(begin, end - begin);

		const auto component = [&block](const QString& p_tag, float p_fallback) {
			const QString open = "<" + p_tag + ">";
			const QString close = "</" + p_tag + ">";
			const int from = block.indexOf(open);
			const int to = block.indexOf(close, from);
			if (from < 0 || to < 0)
			{
				return p_fallback;
			}
			bool ok = false;
			const float value = block.mid(from + open.size(), to - from - open.size()).toFloat(&ok);
			return ok ? value : p_fallback;
		};

		return QColor::fromRgbF(
			qBound(0.0f, component(QStringLiteral("x"), 0.72f), 1.0f),
			qBound(0.0f, component(QStringLiteral("y"), 0.72f), 1.0f),
			qBound(0.0f, component(QStringLiteral("z"), 0.72f), 1.0f));
	}

	QPixmap TextureThumbnail(const QString& p_path)
	{
		QImageReader reader(p_path);
		reader.setAutoTransform(true);
		const QSize size = reader.size();
		if (size.isValid())
		{
			reader.setScaledSize(size.scaled(256, 256, Qt::KeepAspectRatio));
		}
		const QImage image = reader.read();
		return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
	}

	void DrawWrappedName(QPainter* p_painter, const QRect& p_rect, const QString& p_text, int p_maxLines)
	{
		QTextLayout layout(p_text, p_painter->font());
		QTextOption option;
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		layout.setTextOption(option);

		qreal y = 0.0;
		layout.beginLayout();
		for (int i = 0; i < p_maxLines; ++i)
		{
			QTextLine line = layout.createLine();
			if (!line.isValid())
			{
				break;
			}
			line.setLineWidth(p_rect.width());
			line.setPosition(QPointF(0.0, y));
			y += line.height();
		}
		layout.endLayout();

		p_painter->save();
		p_painter->translate(p_rect.topLeft());
		layout.draw(p_painter, QPointF(0.0, 0.0));
		p_painter->restore();
	}

	/** A shaded sphere tinted with the material's base colour, used until real preview
	 * thumbnails are rendered from the material preview view. */
	void DrawMaterialSphere(QPainter* p_painter, const QRect& p_area, QColor p_base)
	{
		if (!p_base.isValid())
		{
			p_base = QColor(184, 184, 184);
		}

		const qreal radius = qMin(p_area.width(), p_area.height()) * 0.40;
		const QPointF center = QRectF(p_area).center();
		const QRectF circle(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);

		QRadialGradient body(center + QPointF(-radius * 0.32, -radius * 0.34), radius * 1.35);
		body.setColorAt(0.0, p_base.lighter(210));
		body.setColorAt(0.45, p_base);
		body.setColorAt(0.82, p_base.darker(190));
		body.setColorAt(1.0, p_base.darker(330));

		p_painter->setPen(Qt::NoPen);
		p_painter->setBrush(body);
		p_painter->drawEllipse(circle);

		QRadialGradient highlight(center + QPointF(-radius * 0.36, -radius * 0.38), radius * 0.52);
		highlight.setColorAt(0.0, QColor(255, 255, 255, 215));
		highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
		p_painter->setBrush(highlight);
		p_painter->drawEllipse(QRectF(center.x() - radius * 0.78, center.y() - radius * 0.82,
			radius * 1.12, radius * 1.12));
	}

	/** Shader tiles have no bitmap: a small code glyph in the same violet as their rule. */
	void DrawShaderGlyph(QPainter* p_painter, const QRect& p_area)
	{
		p_painter->save();
		const QRect glyph = p_area.adjusted(26, 24, -26, -24);
		p_painter->setPen(QPen(QColor(0x9a, 0x7b, 0xd8), 1.4));
		p_painter->setBrush(QColor(0x2a, 0x24, 0x36));
		p_painter->drawRoundedRect(glyph, 3.0, 3.0);
		QFont font = p_painter->font();
		font.setPointSize(14);
		font.setBold(true);
		p_painter->setFont(font);
		p_painter->setPen(QColor(0xc8, 0xb8, 0xff));
		p_painter->drawText(glyph, Qt::AlignCenter, QStringLiteral("{}"));
		p_painter->restore();
	}

	void DrawCheckedBackground(QPainter* p_painter, const QRect& p_rect)
	{
		p_painter->save();
		QPainterPath clip;
		clip.addRoundedRect(p_rect, 3.0, 3.0);
		p_painter->setClipPath(clip);

		const int cell = 10;
		for (int y = p_rect.top(); y < p_rect.bottom(); y += cell)
		{
			for (int x = p_rect.left(); x < p_rect.right(); x += cell)
			{
				const bool even = ((x - p_rect.left()) / cell + (y - p_rect.top()) / cell) % 2 == 0;
				p_painter->fillRect(QRect(x, y, cell, cell),
					even ? QColor(0x1b, 0x1b, 0x1d) : QColor(0x26, 0x26, 0x2a));
			}
		}
		p_painter->restore();
	}
}

namespace MOON
{
	QStringList ContentBrowserModel::mimeTypes() const
	{
		return { QStringLiteral("text/uri-list") };
	}

	QMimeData* ContentBrowserModel::mimeData(const QModelIndexList& p_indexes) const
	{
		auto* mime = new QMimeData();
		QList<QUrl> urls;
		for (const QModelIndex& index : p_indexes)
		{
			const QString path = index.data(ContentRoles::PathRole).toString();
			if (!path.isEmpty())
			{
				urls.push_back(QUrl::fromLocalFile(path));
			}
		}
		mime->setUrls(urls);
		return mime;
	}

	Qt::DropActions ContentBrowserModel::supportedDragActions() const
	{
		return Qt::CopyAction;
	}

	ContentTileDelegate::ContentTileDelegate(QObject* p_parent)
		: QStyledItemDelegate(p_parent)
	{
	}

	QSize ContentTileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
	{
		return QSize(178, 216);
	}

	void ContentTileDelegate::paint(QPainter* p_painter, const QStyleOptionViewItem& p_option,
		const QModelIndex& p_index) const
	{
		p_painter->save();
		p_painter->setRenderHint(QPainter::Antialiasing, true);

		const QRect tile = p_option.rect.adjusted(2, 2, -2, -2);
		const bool selected = (p_option.state & QStyle::State_Selected) != 0;
		const bool hovered = (p_option.state & QStyle::State_MouseOver) != 0;

		const QColor background = selected ? QColor(0x2c, 0x3a, 0x44)
			: hovered ? QColor(0x2a, 0x2a, 0x2e) : QColor(0x20, 0x20, 0x23);
		p_painter->setPen(QPen(selected ? QColor(0x3f, 0x7f, 0x9f) : QColor(0x2f, 0x2f, 0x34)));
		p_painter->setBrush(background);
		p_painter->drawRoundedRect(tile, 4.0, 4.0);

		const int pad = 10;
		const QRect thumbnail(tile.left() + pad, tile.top() + pad, tile.width() - 2 * pad, 132);
		DrawCheckedBackground(p_painter, thumbnail);

		const int type = p_index.data(ContentRoles::TypeRole).toInt();
		const QPixmap pixmap = p_index.data(Qt::DecorationRole).value<QPixmap>();
		if (!pixmap.isNull())
		{
			p_painter->save();
			QPainterPath clip;
			clip.addRoundedRect(thumbnail, 3.0, 3.0);
			p_painter->setClipPath(clip);
			const QPixmap scaled = pixmap.scaled(thumbnail.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
			p_painter->drawPixmap(thumbnail.center().x() - scaled.width() / 2,
				thumbnail.center().y() - scaled.height() / 2, scaled);
			p_painter->restore();
		}
		else if (type == static_cast<int>(ContentAssetType::Material))
		{
			DrawMaterialSphere(p_painter, thumbnail,
				p_index.data(ContentRoles::BaseColorRole).value<QColor>());
		}
		else if (type == static_cast<int>(ContentAssetType::Shader))
		{
			DrawShaderGlyph(p_painter, thumbnail);
		}
		else
		{
			// No readable preview (an HDR/EXR the Qt image plugins cannot decode): a simple
			// picture glyph is clearer than an empty checkerboard.
			p_painter->setPen(QPen(QColor(0x55, 0x55, 0x5c), 1.4));
			const QRect glyph = thumbnail.adjusted(26, 24, -26, -24);
			p_painter->setBrush(Qt::NoBrush);
			p_painter->drawRoundedRect(glyph, 2.0, 2.0);
			p_painter->drawLine(glyph.left() + 4, glyph.bottom() - 6, glyph.center().x() - 2, glyph.center().y());
			p_painter->drawLine(glyph.center().x() - 2, glyph.center().y(), glyph.right() - 4, glyph.bottom() - 14);
		}

		QFont nameFont = p_painter->font();
		nameFont.setPointSize(9);
		p_painter->setFont(nameFont);
		p_painter->setPen(selected ? QColor(0xf0, 0xf0, 0xf2) : QColor(0xd2, 0xd2, 0xd6));
		DrawWrappedName(p_painter, QRect(tile.left() + pad, thumbnail.bottom() + 8,
			tile.width() - 2 * pad, 34), p_index.data(Qt::DisplayRole).toString(), 2);

		QFont subtitleFont = p_painter->font();
		subtitleFont.setPointSize(8);
		p_painter->setFont(subtitleFont);
		p_painter->setPen(QColor(0x88, 0x88, 0x8e));
		p_painter->drawText(QRect(tile.left() + pad, tile.bottom() - 26,
			tile.width() - 2 * pad, 16), Qt::AlignLeft | Qt::AlignVCenter,
			p_index.data(ContentRoles::SubtitleRole).toString());

		// The kind's colour rule, like the content browser this layout follows: cyan for a
		// material, red for a texture.
		const QColor rule = type == static_cast<int>(ContentAssetType::Material)
			? QColor(0x2f, 0xb6, 0xd8)
			: type == static_cast<int>(ContentAssetType::Shader)
				? QColor(0x9a, 0x7b, 0xd8) : QColor(0xd0, 0x5b, 0x5b);
		p_painter->fillRect(QRect(tile.left() + pad, tile.bottom() - 4,
			tile.width() - 2 * pad, 2), rule);

		p_painter->restore();
	}

	ContentBrowserPanel::ContentBrowserPanel(QWidget* p_parent)
		: QDockWidget(p_parent)
	{
		RegService(ContentBrowserPanel, *this);

		setObjectName(QStringLiteral("ContentBrowserPanel"));
		MOON::Language::SetWindowTitle(this, QStringLiteral("内容浏览器"), QStringLiteral("Content Browser"));
		setAllowedAreas(Qt::AllDockWidgetAreas);

		BuildUi();
		PopulateTree();

		// A material edited in the material window must not leave a stale tile behind.
		auto& materialEditor = GetService(MaterialEditorPanel);
		connect(&materialEditor, &MaterialEditorPanel::materialThumbnailDirty, this,
			[this](const QString& p_key) { OnMaterialThumbnailDirty(p_key); });
	}

	ContentBrowserPanel::~ContentBrowserPanel() = default;

	void ContentBrowserPanel::BuildUi()
	{
		auto* content = new QWidget(this);
		auto* layout = new QVBoxLayout(content);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		auto* bar = new QWidget(content);
		auto* barLayout = new QHBoxLayout(bar);
		barLayout->setContentsMargins(8, 6, 8, 6);
		barLayout->setSpacing(6);

		auto* add = new QToolButton(bar);
		add->setText(tr("+ Add"));
		add->setCursor(Qt::PointingHandCursor);
		add->setPopupMode(QToolButton::InstantPopup);
		auto* addMenu = new QMenu(add);
		QAction* newMaterial = addMenu->addAction(tr("New Material"));
		connect(newMaterial, &QAction::triggered, this, [this] { NewMaterial(); });
		QAction* newShader = addMenu->addAction(tr("New Shader"));
		connect(newShader, &QAction::triggered, this, [this] { NewShader(); });
		QAction* importTextures = addMenu->addAction(tr("Import Textures..."));
		connect(importTextures, &QAction::triggered, this, [this] { ImportTextures(); });
		add->setMenu(addMenu);

		auto* import = new QToolButton(bar);
		import->setText(tr("Import"));
		import->setCursor(Qt::PointingHandCursor);
		connect(import, &QToolButton::clicked, this, [this] { ImportTextures(); });

		auto* refresh = new QToolButton(bar);
		refresh->setText(tr("Refresh"));
		refresh->setCursor(Qt::PointingHandCursor);
		connect(refresh, &QToolButton::clicked, this, [this] { Refresh(false); });

		m_breadcrumb = new QLabel(bar);
		m_breadcrumb->setObjectName(QStringLiteral("ContentBrowserBreadcrumb"));
		m_breadcrumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

		m_search = new QLineEdit(bar);
		m_search->setObjectName(QStringLiteral("ContentBrowserSearch"));
		m_search->setPlaceholderText(tr("Search"));
		m_search->setClearButtonEnabled(true);
		m_search->setMaximumWidth(260);

		barLayout->addWidget(add);
		barLayout->addWidget(import);
		barLayout->addWidget(refresh);
		barLayout->addWidget(m_breadcrumb, 1);
		barLayout->addWidget(m_search);
		layout->addWidget(bar);

		auto* splitter = new QSplitter(Qt::Horizontal, content);
		splitter->setHandleWidth(4);
		splitter->setChildrenCollapsible(false);

		m_tree = new QTreeWidget(splitter);
		m_tree->setObjectName(QStringLiteral("ContentBrowserTree"));
		m_tree->setHeaderHidden(true);
		m_tree->setColumnCount(1);
		m_tree->setIndentation(20);
		m_tree->setIconSize(QSize(24, 24));
		// Same tree furniture as the Hierarchy panel: the chevron branch arrows, the row height
		// and the hover/selected colours, so the two trees read as one widget family.
		m_tree->setStyleSheet(QStringLiteral(R"(
QTreeView::item { height: 30px; padding-left: 4px; }
QTreeView::item:hover { background-color: #2a2a2e; }
QTreeView::item:selected { background-color: #2f6f9f; color: white; }
QTreeView::branch { background: transparent; border-image: none; }
/* The default style paints branch connector lines through border-image for the
   sibling/leaf states; clearing them keeps only the chevron. */
QTreeView::branch:has-siblings:!adjoins-item,
QTreeView::branch:has-siblings:adjoins-item,
QTreeView::branch:!has-children:!has-siblings:adjoins-item {
    border-image: none;
    image: none;
}
QTreeView::branch:has-children:!has-siblings:closed,
QTreeView::branch:closed:has-children:has-siblings {
    image: url(:/widgets/icons/arrow_right.svg);
}
QTreeView::branch:open:has-children:!has-siblings,
QTreeView::branch:open:has-children:has-siblings {
    image: url(:/widgets/icons/arrow_down.svg);
}
)"));
		m_tree->setMinimumWidth(170);
		m_tree->setMaximumWidth(360);

		m_view = new QListView(splitter);
		m_view->setObjectName(QStringLiteral("ContentBrowserView"));
		m_view->setViewMode(QListView::IconMode);
		m_view->setResizeMode(QListView::Adjust);
		m_view->setMovement(QListView::Static);
		m_view->setWrapping(true);
		m_view->setUniformItemSizes(true);
		m_view->setSpacing(6);
		m_view->setMouseTracking(true);
		m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
		m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_view->setDragEnabled(true);
		m_view->setDragDropMode(QAbstractItemView::DragOnly);
		m_view->setContextMenuPolicy(Qt::CustomContextMenu);
		m_view->setGridSize(QSize(186, 224));
		m_view->setItemDelegate(new ContentTileDelegate(m_view));

		splitter->addWidget(m_tree);
		splitter->addWidget(m_view);
		splitter->setStretchFactor(0, 0);
		splitter->setStretchFactor(1, 1);
		splitter->setSizes({ 210, 900 });
		layout->addWidget(splitter, 1);

		m_count = new QLabel(content);
		m_count->setObjectName(QStringLiteral("ContentBrowserCount"));
		m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		m_count->setContentsMargins(8, 2, 8, 4);
		layout->addWidget(m_count);

		setWidget(content);

		m_thumbnailTimer = new QTimer(this);
		m_thumbnailTimer->setSingleShot(true);
		m_thumbnailTimer->setInterval(0);
		connect(m_thumbnailTimer, &QTimer::timeout, this, [this] { RenderNextThumbnail(); });

		// The material editor reports every edit; re-rendering a tile per slider tick would be
		// wasteful, so a short idle window collapses a burst into one render.
		m_thumbnailDirtyTimer = new QTimer(this);
		m_thumbnailDirtyTimer->setSingleShot(true);
		m_thumbnailDirtyTimer->setInterval(600);
		connect(m_thumbnailDirtyTimer, &QTimer::timeout, this, [this] {
			if (m_dirtyThumbnailName.isEmpty())
			{
				return;
			}
			for (int row = 0; row < m_model->rowCount(); ++row)
			{
				QStandardItem* item = m_model->item(row, 0);
				if (item == nullptr)
				{
					continue;
				}
				const QString path = item->data(ContentRoles::PathRole).toString();
				if (QFileInfo(path).fileName() != m_dirtyThumbnailName)
				{
					continue;
				}
				m_materialThumbnails.remove(ThumbnailKey(path));
				m_thumbnailAttempts.remove(path);
				if (!m_thumbnailQueue.contains(path))
				{
					m_thumbnailQueue.push_front(path);
				}
			}
			if (!m_thumbnailQueue.isEmpty())
			{
				m_thumbnailTimer->start(0);
			}
		});

		m_model = new ContentBrowserModel(this);
		m_proxy = new QSortFilterProxyModel(this);
		m_proxy->setSourceModel(m_model);
		m_proxy->setFilterKeyColumn(0);
		m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
		m_proxy->setDynamicSortFilter(true);
		m_view->setModel(m_proxy);

		connect(m_search, &QLineEdit::textChanged, this, [this] {
			m_proxy->setFilterFixedString(m_search->text());
			UpdateCount();
		});
		connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& p_index) {
			m_view->setCurrentIndex(p_index);
			OnActivated(p_index);
		});
		connect(m_view, &QListView::customContextMenuRequested, this, [this](const QPoint& p_pos) {
			m_view->setCurrentIndex(m_view->indexAt(p_pos));
			OnContextMenu(p_pos);
		});
		connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { OnTreeChanged(); });
	}

	void ContentBrowserPanel::PopulateTree()
	{
		QSignalBlocker blocker(m_tree);
		m_tree->clear();

		AddFolderItem(nullptr, tr("Materials"), EngineRoot() + "/Materials", ContentAssetType::Material);
		AddFolderItem(nullptr, tr("Textures"), EngineRoot() + "/Textures", ContentAssetType::Texture);

		const QString shaderRoot = EngineRoot() + "/Shaders";
		if (QDir(shaderRoot).exists())
		{
			AddFolderItem(nullptr, tr("Shaders"), shaderRoot, ContentAssetType::Shader);
		}
		else
		{
			// A release build packs the shader sources into the executable; there is no folder
			// to walk, so the root is a virtual one backed by the archive.
			auto* item = new QTreeWidgetItem(m_tree);
			item->setText(0, tr("Shaders"));
			item->setIcon(0, QIcon(QStringLiteral(":/widgets/icons/folder.png")));
			item->setData(0, Qt::UserRole, QStringLiteral(":Shaders"));
			item->setData(0, ContentRoles::TypeRole, static_cast<int>(ContentAssetType::Shader));
			item->setToolTip(0, tr("Compiled shaders (sources are packed at build time)"));
		}
		m_tree->expandAll();

		if (m_tree->topLevelItemCount() > 0)
		{
			m_tree->setCurrentItem(m_tree->topLevelItem(0));
		}

		// The blocker above also swallows the currentItemChanged that selecting the first
		// folder emits, so load it directly - otherwise the grid starts empty.
		if (m_tree->currentItem() != nullptr)
		{
			OnTreeChanged();
		}
	}

	void ContentBrowserPanel::AddFolderItem(QTreeWidgetItem* p_parent, const QString& p_label,
		const QString& p_path, ContentAssetType p_type)
	{
		QTreeWidgetItem* item = p_parent != nullptr
			? new QTreeWidgetItem(p_parent) : new QTreeWidgetItem(m_tree);
		item->setText(0, p_label);
		item->setIcon(0, QIcon(QStringLiteral(":/widgets/icons/folder.png")));
		item->setData(0, Qt::UserRole, p_path);
		item->setData(0, ContentRoles::TypeRole, static_cast<int>(p_type));
		item->setToolTip(0, p_path);

		const QDir dir(p_path);
		for (const QFileInfo& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		{
			AddFolderItem(item, sub.fileName(), sub.filePath(), p_type);
		}
	}

	void ContentBrowserPanel::OnTreeChanged()
	{
		const QTreeWidgetItem* item = m_tree->currentItem();
		if (item == nullptr)
		{
			return;
		}
		ShowFolder(item->data(0, Qt::UserRole).toString(),
			static_cast<ContentAssetType>(item->data(0, ContentRoles::TypeRole).toInt()));
	}

	void ContentBrowserPanel::ShowFolder(const QString& p_folder, ContentAssetType p_type)
	{
		m_currentFolder = p_folder;
		m_currentType = p_type;
		m_model->clear();

		// A release has no shader folder: list what the build packed into the executable.
		if (p_type == ContentAssetType::Shader && (p_folder == QStringLiteral(":Shaders")
			|| !QDir(p_folder).exists()))
		{
			const Rendering::Resources::ShaderArchive& archive =
				Rendering::Resources::GetShaderArchive();
			std::vector<std::string> names;
			for (uint32_t i = 0; i < archive.count; ++i)
			{
				std::string name = archive.files[i].name;
				std::replace(name.begin(), name.end(), '\\', '/');
				// Source/ entries mirror the .shader text for the editor; list the packages.
				if (name.rfind("Source/", 0) == 0)
				{
					continue;
				}
				if (name.ends_with(".shader"))
				{
					names.push_back(std::move(name));
				}
			}
			std::ranges::sort(names);
			names.erase(std::unique(names.begin(), names.end()), names.end());

			for (const std::string& name : names)
			{
				const QString key = QStringLiteral(":Shaders/") + QString::fromStdString(name);
				auto* item = new QStandardItem(QString::fromStdString(name));
				item->setEditable(false);
				item->setData(key, ContentRoles::PathRole);
				item->setData(static_cast<int>(ContentAssetType::Shader), ContentRoles::TypeRole);
				item->setData(tr("Shader"), ContentRoles::SubtitleRole);
				item->setToolTip(key);
				m_model->appendRow(item);
			}

			UpdateBreadcrumb();
			UpdateCount();
			return;
		}

		const QDir dir(p_folder);
		QStringList files;
		if (p_type == ContentAssetType::Material)
		{
			files = dir.entryList({ QStringLiteral("*.mat") }, QDir::Files, QDir::Name);
		}
		else if (p_type == ContentAssetType::Shader)
		{
			files = dir.entryList({ QStringLiteral("*.shader") }, QDir::Files, QDir::Name);
		}
		else
		{
			files = dir.entryList(ImageFilters(), QDir::Files, QDir::Name);
		}

		for (const QString& file : files)
		{
			const QFileInfo info(dir.filePath(file));

			auto* item = new QStandardItem(info.completeBaseName());
			item->setEditable(false);
			item->setData(info.filePath(), ContentRoles::PathRole);
			item->setData(static_cast<int>(p_type), ContentRoles::TypeRole);
			const QString subtitle = p_type == ContentAssetType::Material ? tr("Material")
				: p_type == ContentAssetType::Shader ? tr("Shader") : tr("Texture");
			item->setData(subtitle, ContentRoles::SubtitleRole);
			item->setToolTip(info.filePath());

			if (p_type == ContentAssetType::Material)
			{
				item->setData(MaterialBaseColor(info.filePath()), ContentRoles::BaseColorRole);

				// The rendered sphere, when this material has already been shown once. Until
				// then the tile falls back to the base-colour sphere the delegate draws.
				const auto cached = m_materialThumbnails.constFind(ThumbnailKey(info.filePath()));
				if (cached != m_materialThumbnails.constEnd())
				{
					item->setData(*cached, Qt::DecorationRole);
				}
			}
			else if (p_type == ContentAssetType::Texture)
			{
				// The decoded preview rides in the decoration role, so the delegate only paints.
				item->setData(TextureThumbnail(info.filePath()), Qt::DecorationRole);
			}
			// Shaders have no bitmap preview: the delegate draws a code glyph for them.

			m_model->appendRow(item);
		}

		UpdateBreadcrumb();
		UpdateCount();

		m_thumbnailQueue.clear();
		m_thumbnailAttempts.clear();
		if (m_thumbnailTimer != nullptr)
		{
			m_thumbnailTimer->stop();
		}
		if (isVisible() && p_type == ContentAssetType::Material)
		{
			EnqueueMaterialThumbnails();
		}
	}

	void ContentBrowserPanel::showEvent(QShowEvent* p_event)
	{
		QDockWidget::showEvent(p_event);
		if (m_currentType == ContentAssetType::Material)
		{
			// The render context may not have existed when the folder was first loaded.
			EnqueueMaterialThumbnails();
		}
	}

	QString ContentBrowserPanel::ThumbnailKey(const QString& p_path) const
	{
		const QFileInfo info(p_path);
		return p_path + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
	}

	void ContentBrowserPanel::EnqueueMaterialThumbnails()
	{
		if (m_thumbnailTimer == nullptr)
		{
			return;
		}

		m_thumbnailQueue.clear();
		for (int row = 0; row < m_model->rowCount(); ++row)
		{
			QStandardItem* item = m_model->item(row, 0);
			if (item == nullptr)
			{
				continue;
			}

			const QString path = item->data(ContentRoles::PathRole).toString();
			const auto cached = m_materialThumbnails.constFind(ThumbnailKey(path));
			if (cached != m_materialThumbnails.constEnd())
			{
				item->setData(*cached, Qt::DecorationRole);
				continue;
			}
			if (!m_thumbnailQueue.contains(path))
			{
				m_thumbnailQueue.push_back(path);
			}
		}

		if (!m_thumbnailQueue.isEmpty())
		{
			m_thumbnailTimer->start(0);
		}
	}

	void ContentBrowserPanel::RenderNextThumbnail()
	{
		if (m_thumbnailQueue.isEmpty())
		{
			return;
		}

		const QString path = m_thumbnailQueue.takeFirst();
		if (m_thumbnailRenderer == nullptr)
		{
			m_thumbnailRenderer = std::make_unique<MaterialThumbnailRenderer>();
		}

		const QImage image = m_thumbnailRenderer->Render(path, 256);
		if (!image.isNull())
		{
			const QPixmap thumbnail = QPixmap::fromImage(image);
			m_materialThumbnails.insert(ThumbnailKey(path), thumbnail);
			m_thumbnailAttempts.remove(path);
			ApplyThumbnail(path, thumbnail);
		}
		else
		{
			// The module context is created with the viewport's first paint; a panel that was
			// built first gets an empty frame. Retry a few times instead of leaving the tile on
			// the base-colour placeholder for good.
			const int attempts = m_thumbnailAttempts.value(path) + 1;
			m_thumbnailAttempts.insert(path, attempts);
			if (attempts < 6 && !m_thumbnailQueue.contains(path))
			{
				m_thumbnailQueue.push_back(path);
			}
		}

		if (!m_thumbnailQueue.isEmpty())
		{
			m_thumbnailTimer->start(image.isNull() ? 400 : 0);
		}
	}

	void ContentBrowserPanel::OnMaterialThumbnailDirty(const QString& p_key)
	{
		m_dirtyThumbnailName = QFileInfo(p_key).fileName();
		if (m_thumbnailDirtyTimer != nullptr)
		{
			m_thumbnailDirtyTimer->start();
		}
	}

	void ContentBrowserPanel::ApplyThumbnail(const QString& p_path, const QPixmap& p_thumbnail)
	{
		for (int row = 0; row < m_model->rowCount(); ++row)
		{
			QStandardItem* item = m_model->item(row, 0);
			if (item != nullptr && item->data(ContentRoles::PathRole).toString() == p_path)
			{
				item->setData(p_thumbnail, Qt::DecorationRole);
			}
		}
	}

	void ContentBrowserPanel::UpdateBreadcrumb()
	{
		if (m_currentFolder == QStringLiteral(":Shaders"))
		{
			m_breadcrumb->setText(tr("Shaders (compiled)"));
			m_breadcrumb->setToolTip(tr("Sources packed into the executable at build time"));
			return;
		}
		QString relative = QDir(EngineRoot()).relativeFilePath(m_currentFolder);
		// Keep the breadcrumb ASCII: the build's code page (936) cannot represent the
		// typographic separator and the compiler would substitute a replacement character.
		relative.replace('/', QStringLiteral("  >  "));
		m_breadcrumb->setText(relative);
		m_breadcrumb->setToolTip(m_currentFolder);
	}

	void ContentBrowserPanel::UpdateCount()
	{
		m_count->setText(tr("%1 items").arg(m_proxy->rowCount()));
	}

	void ContentBrowserPanel::Refresh(bool p_tree)
	{
		if (p_tree)
		{
			PopulateTree();
			return;
		}
		ShowFolder(m_currentFolder, m_currentType);
	}

	QString ContentBrowserPanel::SelectedPath() const
	{
		const QModelIndex index = m_view->currentIndex();
		return index.isValid() ? index.data(ContentRoles::PathRole).toString() : QString();
	}

	void ContentBrowserPanel::OnActivated(const QModelIndex& p_index)
	{
		if (!p_index.isValid())
		{
			return;
		}

		const QString path = p_index.data(ContentRoles::PathRole).toString();
		const int type = p_index.data(ContentRoles::TypeRole).toInt();
		if (type == static_cast<int>(ContentAssetType::Material))
		{
			// The material designer keys its list by the serializer's resource path.
			GetService(MaterialEditorPanel).OpenMaterial(
				(":Materials/" + QFileInfo(path).fileName()).toStdString());
		}
		else if (type == static_cast<int>(ContentAssetType::Shader))
		{
			// A filesystem entry becomes a resource key; an archive entry already is one.
			QString key = path;
			if (!key.startsWith(QStringLiteral(":Shaders")))
			{
				const QString relative = QDir(EngineRoot() + "/Shaders").relativeFilePath(path);
				key = QStringLiteral(":Shaders/") + relative;
			}
			GetService(MaterialEditorPanel).OpenShader(key.toStdString());
		}
		else
		{
			QDesktopServices::openUrl(QUrl::fromLocalFile(path));
		}
	}

	void ContentBrowserPanel::OnContextMenu(const QPoint& p_pos)
	{
		QMenu menu(this);
		const QModelIndex index = m_view->indexAt(p_pos);
		if (index.isValid())
		{
			QAction* openAction = menu.addAction(tr("Open"));
			connect(openAction, &QAction::triggered, this, [this, index] { OnActivated(index); });

			QAction* copyPath = menu.addAction(tr("Copy Path"));
			connect(copyPath, &QAction::triggered, this, [this] { CopySelected(); });

			QAction* reveal = menu.addAction(tr("Reveal in Explorer"));
			connect(reveal, &QAction::triggered, this, [this] { RevealSelected(); });

			menu.addSeparator();

			QAction* rename = menu.addAction(tr("Rename..."));
			connect(rename, &QAction::triggered, this, [this] { RenameSelected(); });

			QAction* remove = menu.addAction(tr("Delete..."));
			connect(remove, &QAction::triggered, this, [this] { DeleteSelected(); });

			menu.addSeparator();
		}

		if (m_currentType == ContentAssetType::Texture)
		{
			QAction* importAction = menu.addAction(tr("Import Textures..."));
			connect(importAction, &QAction::triggered, this, [this] { ImportTextures(); });
		}
		else if (m_currentType == ContentAssetType::Shader)
		{
			QAction* newShader = menu.addAction(tr("New Shader"));
			connect(newShader, &QAction::triggered, this, [this] { NewShader(); });
		}
		else
		{
			QAction* newMaterial = menu.addAction(tr("New Material"));
			connect(newMaterial, &QAction::triggered, this, [this] { NewMaterial(); });
		}
		QAction* refresh = menu.addAction(tr("Refresh"));
		connect(refresh, &QAction::triggered, this, [this] { Refresh(false); });
		menu.exec(m_view->viewport()->mapToGlobal(p_pos));
	}

	void ContentBrowserPanel::CopySelected()
	{
		const QString path = SelectedPath();
		if (!path.isEmpty())
		{
			QApplication::clipboard()->setText(path);
		}
	}

	void ContentBrowserPanel::RevealSelected()
	{
		const QString path = SelectedPath();
		if (path.isEmpty())
		{
			return;
		}
		if (path.startsWith(QStringLiteral(":Shaders")))
		{
			QMessageBox::information(this, tr("Reveal"),
				tr("This shader is packed into the build; there is no source file to reveal."));
			return;
		}
		QProcess::startDetached(QStringLiteral("explorer.exe"),
			{ QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
	}

	void ContentBrowserPanel::RenameSelected()
	{
		const QString path = SelectedPath();
		if (path.isEmpty())
		{
			return;
		}
		if (path.startsWith(QStringLiteral(":Shaders")))
		{
			QMessageBox::information(this, tr("Rename"),
				tr("This shader is packed into the build; there is no source file to rename."));
			return;
		}

		const QFileInfo info(path);
		bool accepted = false;
		const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name:"),
			QLineEdit::Normal, info.fileName(), &accepted);
		if (!accepted || name.isEmpty() || name == info.fileName())
		{
			return;
		}

		QString target = info.dir().filePath(name);
		if (QFileInfo(target).suffix().isEmpty())
		{
			target = info.dir().filePath(name + "." + info.suffix());
		}
		if (QFile::exists(target))
		{
			QMessageBox::warning(this, tr("Rename"), tr("A file with that name already exists."));
			return;
		}
		if (!QFile::rename(path, target))
		{
			QMessageBox::warning(this, tr("Rename"), tr("The file could not be renamed."));
			return;
		}
		ShowFolder(m_currentFolder, m_currentType);
	}

	void ContentBrowserPanel::DeleteSelected()
	{
		const QString path = SelectedPath();
		if (path.isEmpty())
		{
			return;
		}
		if (path.startsWith(QStringLiteral(":Shaders")))
		{
			QMessageBox::information(this, tr("Delete"),
				tr("This shader is packed into the build; there is no source file to delete."));
			return;
		}
		if (QMessageBox::question(this, tr("Delete"),
			tr("Delete \"%1\"?").arg(QFileInfo(path).fileName()))
			!= QMessageBox::Yes)
		{
			return;
		}
		if (!QFile::remove(path))
		{
			QMessageBox::warning(this, tr("Delete"), tr("The file could not be deleted."));
			return;
		}
		ShowFolder(m_currentFolder, m_currentType);
	}

	void ContentBrowserPanel::ImportTextures()
	{
		if (m_currentType != ContentAssetType::Texture)
		{
			QMessageBox::information(this, tr("Import"),
				tr("Select a folder under Textures before importing an image."));
			return;
		}

		const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Textures"),
			QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.hdr *.exr *.dds)"));
		if (files.isEmpty())
		{
			return;
		}

		const QDir folder(m_currentFolder);
		int copied = 0;
		for (const QString& file : files)
		{
			const QString target = folder.filePath(QFileInfo(file).fileName());
			if (QFile::exists(target))
			{
				continue;
			}
			if (QFile::copy(file, target))
			{
				++copied;
			}
		}
		if (copied > 0)
		{
			ShowFolder(m_currentFolder, m_currentType);
		}
	}

	void ContentBrowserPanel::NewMaterial()
	{
		if (m_currentType != ContentAssetType::Material)
		{
			QMessageBox::information(this, tr("New Material"),
				tr("Select a folder under Materials first."));
			return;
		}

		// A .mat is an instance of a .shader (the Unity-like material/shader split), so
		// creating one starts by picking the shader it references.
		const std::filesystem::path shaderRoot =
			std::filesystem::path(EngineRoot().toStdString()) / "Shaders";
		std::vector<std::string> shaderKeys;
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".shader")
			{
				continue;
			}
			std::string relative = std::filesystem::relative(entry.path(), shaderRoot).generic_string();
			std::replace(relative.begin(), relative.end(), '/', '\\');
			shaderKeys.push_back(":Shaders\\" + relative);
		}
		std::sort(shaderKeys.begin(), shaderKeys.end());
		if (shaderKeys.empty())
		{
			QMessageBox::warning(this, tr("New Material"), tr("No .shader files were found."));
			return;
		}

		QDialog dialog(this);
		dialog.setWindowTitle(tr("New Material"));
		auto* form = new QFormLayout(&dialog);

		auto* nameEdit = new QLineEdit(&dialog);
		nameEdit->setPlaceholderText(QStringLiteral("MyMaterial"));
		form->addRow(tr("Name"), nameEdit);

		auto* shaderPicker = new QComboBox(&dialog);
		for (const std::string& shaderKey : shaderKeys)
		{
			shaderPicker->addItem(QString::fromStdString(shaderKey));
		}
		form->addRow(tr("Shader"), shaderPicker);

		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		form->addRow(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

		if (dialog.exec() != QDialog::Accepted)
		{
			return;
		}

		QString name = nameEdit->text().trimmed();
		if (name.isEmpty())
		{
			QMessageBox::warning(this, tr("New Material"), tr("A material needs a name."));
			return;
		}
		if (name.contains('/') || name.contains('\\') || name.contains(QStringLiteral("..")))
		{
			QMessageBox::warning(this, tr("New Material"), tr("The name cannot contain a path."));
			return;
		}
		if (!name.endsWith(QStringLiteral(".mat")))
		{
			name += QStringLiteral(".mat");
		}

		const QString target = QDir(m_currentFolder).filePath(name);
		if (QFile::exists(target))
		{
			QMessageBox::warning(this, tr("New Material"), tr("That material already exists."));
			return;
		}

		const std::string key = ":Materials/" + name.toStdString();
		std::string failure;
		Core::Resources::Material* created = nullptr;
		{
			ModuleContextScope context;
			if (!context)
			{
				failure = "The render context is not ready yet.";
			}
			else
			{
				auto* shader = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ShaderManager>()
					.GetResource(shaderPicker->currentText().toStdString(), true);
				if (shader == nullptr)
				{
					failure = "The shader could not be loaded.";
				}
				else
				{
					// Scaffold the instance, write it, then let the manager load it back: that
					// is what gives the material its (const) resource path.
					Core::Resources::Material scaffold;
					scaffold.SetShader(shader);
					Core::Resources::Loaders::MaterialLoader::Save(scaffold, target.toStdString());
					created = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
						.GetResource(key, true);
					if (created == nullptr)
					{
						failure = "The new material could not be loaded back.";
					}
				}
			}
		}

		if (created == nullptr)
		{
			QMessageBox::warning(this, tr("New Material"), QString::fromStdString(failure));
			return;
		}

		ShowFolder(m_currentFolder, m_currentType);
		for (int row = 0; row < m_model->rowCount(); ++row)
		{
			const QModelIndex source = m_model->index(row, 0);
			if (source.data(ContentRoles::PathRole).toString() == target)
			{
				m_view->setCurrentIndex(m_proxy->mapFromSource(source));
				break;
			}
		}
	}

	void ContentBrowserPanel::NewShader()
	{
		if (m_currentType != ContentAssetType::Shader)
		{
			QMessageBox::information(this, tr("New Shader"),
				tr("Select a folder under Shaders first."));
			return;
		}
		if (!QDir(EngineRoot() + "/Shaders").exists())
		{
			QMessageBox::information(this, tr("New Shader"),
				tr("This build packs its shader sources into the executable; "
					"create shaders in a build that ships the Shaders folder."));
			return;
		}

		// A new shader starts as a copy of an existing one: the templates are exactly the
		// .shader files the Shader page lists.
		const std::filesystem::path shaderRoot =
			std::filesystem::path(EngineRoot().toStdString()) / "Shaders";
		std::vector<std::string> templates;
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot, error))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".shader")
			{
				templates.push_back(
					std::filesystem::relative(entry.path(), shaderRoot).generic_string());
			}
		}
		std::sort(templates.begin(), templates.end());
		if (templates.empty())
		{
			QMessageBox::warning(this, tr("New Shader"),
				tr("No .shader files to use as a template."));
			return;
		}

		QDialog dialog(this);
		dialog.setWindowTitle(tr("New Shader"));
		auto* form = new QFormLayout(&dialog);

		auto* nameEdit = new QLineEdit(&dialog);
		nameEdit->setPlaceholderText(QStringLiteral("MyShader"));
		form->addRow(tr("Name"), nameEdit);

		auto* templatePicker = new QComboBox(&dialog);
		for (const std::string& relative : templates)
		{
			templatePicker->addItem(QString::fromStdString(relative));
		}
		form->addRow(tr("Template"), templatePicker);

		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		form->addRow(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

		if (dialog.exec() != QDialog::Accepted)
		{
			return;
		}

		QString name = nameEdit->text().trimmed();
		if (name.isEmpty())
		{
			QMessageBox::warning(this, tr("New Shader"), tr("A shader needs a name."));
			return;
		}
		if (name.contains('/') || name.contains('\\') || name.contains(QStringLiteral("..")))
		{
			QMessageBox::warning(this, tr("New Shader"), tr("The name cannot contain a path."));
			return;
		}
		if (!name.endsWith(QStringLiteral(".shader")))
		{
			name += QStringLiteral(".shader");
		}

		const QString target = QDir(m_currentFolder).filePath(name);
		if (QFile::exists(target))
		{
			QMessageBox::warning(this, tr("New Shader"), tr("That shader already exists."));
			return;
		}

		const QString templatePath = QString::fromStdString(
			(shaderRoot / templatePicker->currentText().toStdString()).string());
		if (!QFile::copy(templatePath, target))
		{
			QMessageBox::warning(this, tr("New Shader"), tr("The shader could not be created."));
			return;
		}

		ShowFolder(m_currentFolder, m_currentType);
		for (int row = 0; row < m_model->rowCount(); ++row)
		{
			const QModelIndex source = m_model->index(row, 0);
			if (source.data(ContentRoles::PathRole).toString() == target)
			{
				m_view->setCurrentIndex(m_proxy->mapFromSource(source));
				break;
			}
		}
	}
}
