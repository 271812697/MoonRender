#include "MaterialEditorPanel.h"
#include "MaterialCodeEditor.h"
#include "MaterialDesigner.h"
#include "MaterialPreviewWidget.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Global/ServiceLocator.h"
#include "Core/Resources/Material.h"
#include "Rendering/Resources/ShaderArchive.h"
#include "Rendering/Resources/ShaderPackage.h"
#include "renderer/Context.h"
#include "Rendering/Context/Driver.h"
#include "Rendering/Resources/Loaders/ShaderLoader.h"
#include "Tools/Utils/PathParser.h"

#include "Matc.h"

namespace
{
	// The output colours and the status dot: the same family as the editor's palette, so a
	// failure reads the same colour wherever it is reported.
	const QColor kLogInfo(0x9d, 0x99, 0x95);
	const QColor kLogSuccess(0x8f, 0xc3, 0x65);
	const QColor kLogWarning(0xe5, 0xc0, 0x7b);
	const QColor kLogError(0xe0, 0x6c, 0x75);
	const QColor kStatusIdle(0x6f, 0x75, 0x7e);

	/** ":Shaders\\PostProcess\\Blur.shader" for "PostProcess/Blur.shader", which is what the archive
	 * and the shader manager are keyed by. */
	std::string ToShaderKey(const std::string& p_relative)
	{
		std::string key = ":Shaders\\";
		for (const char c : p_relative)
		{
			key += (c == '/' ? '\\' : c);
		}
		return key;
	}

	/** #include resolution for a release: the .shader sources and .glsl headers are packed in
	 * the executable, so MoonMatc asks the archive instead of the (absent) Shaders folder. */
	bool ResolveShaderInclude(const std::string& p_name, std::string& p_text, std::string& p_origin)
	{
		return Rendering::Resources::ReadShaderSource(p_name, p_text, &p_origin);
	}

	/** A section strip in the flat style of Inviwo's processor headers: a shade darker than the
	 * content, one hairline under it, an upper-case label, and an optional trailing widget (the
	 * cursor readout, a Clear button). */
	QWidget* MakeSectionHeader(const QString& p_title, QWidget* p_parent, QWidget* p_trailing = nullptr)
	{
		auto* header = new QFrame(p_parent);
		header->setObjectName("MaterialSectionHeader");
		header->setFixedHeight(22);

		auto* layout = new QHBoxLayout(header);
		layout->setContentsMargins(8, 0, 4, 0);
		layout->setSpacing(8);

		auto* title = new QLabel(p_title, header);
		title->setObjectName("MaterialSectionTitle");
		layout->addWidget(title);
		layout->addStretch(1);
		if (p_trailing != nullptr)
		{
			layout->addWidget(p_trailing);
		}
		return header;
	}
}

class MOON::MaterialEditorPanel::MaterialEditorPanelInternal
{
public:
	MaterialEditorPanelInternal(MaterialEditorPanel* p_owner) : m_owner(p_owner)
	{
		m_shaderRoot = std::filesystem::path(Tools::Utils::PathParser::GetExeDirectory())
			/ "Moon" / "Data" / "Engine" / "Shaders";

		BuildUi();
		RefreshFileList();

		if (m_filePicker->count() > 0)
		{
			m_filePicker->setCurrentIndex(0);
		}

		// Nothing GL happens here: the first preview is compiled when the panel is shown (see
		// OnFirstShow), and the widget it is handed to builds the shader on its own paint, once
		// the render module's context exists.
	}

	void BuildUi()
	{
		// Side by side needs the width: below this the halves stop being useful, so the window
		// asks for it instead of squeezing and eliding everything.
		m_owner->setMinimumWidth(600);

		auto* content = new QWidget(m_owner);
		content->setObjectName("MaterialEditorContent");
		content->setAttribute(Qt::WA_StyledBackground, true);
		auto* layout = new QVBoxLayout(content);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		// --- the two pages: the shader source, and a material designed from it -------------
		// The preview is shared by both, so the switch only chooses what sits next to it.
		auto* modes = new QWidget(content);
		modes->setObjectName("MaterialModeBar");
		modes->setAttribute(Qt::WA_StyledBackground, true);
		auto* modesLayout = new QHBoxLayout(modes);
		modesLayout->setContentsMargins(6, 3, 6, 3);
		modesLayout->setSpacing(2);

		m_modeShader = new QToolButton(modes);
		m_modeShader->setObjectName("MaterialModeButton");
		m_modeShader->setText(QStringLiteral("Shader"));
		m_modeShader->setCheckable(true);
		m_modeShader->setChecked(true);
		m_modeShader->setCursor(Qt::PointingHandCursor);
		m_modeShader->setToolTip(QStringLiteral("Edit a shader's source and hot reload it into the editor"));

		m_modeMaterial = new QToolButton(modes);
		m_modeMaterial->setObjectName("MaterialModeButton");
		m_modeMaterial->setText(QStringLiteral("Material"));
		m_modeMaterial->setCheckable(true);
		m_modeMaterial->setCursor(Qt::PointingHandCursor);
		m_modeMaterial->setToolTip(QStringLiteral("Design a material instance and assign it to the selection"));

		auto* modeGroup = new QButtonGroup(modes);
		modeGroup->setExclusive(true);
		modeGroup->addButton(m_modeShader);
		modeGroup->addButton(m_modeMaterial);

		modesLayout->addWidget(m_modeShader);
		modesLayout->addWidget(m_modeMaterial);
		modesLayout->addStretch(1);
		layout->addWidget(modes);

		// --- the shader page: the .shader source and the GLSL it generates --------------------
		// Laid out like a code viewer (the same shape as Qt-Filament's ShaderPanel): an info
		// strip with the shader and its line count, then the editable source on the left and
		// one read-only tab per generated stage on the right.
		auto* infoBar = new QWidget(content);
		infoBar->setObjectName("ShaderInfoBar");
		infoBar->setAttribute(Qt::WA_StyledBackground, true);
		auto* infoLayout = new QHBoxLayout(infoBar);
		infoLayout->setContentsMargins(8, 3, 8, 3);
		infoLayout->setSpacing(8);

		m_filePicker = new QComboBox(infoBar);
		m_filePicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_filePicker->setMinimumContentsLength(14);
		m_filePicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
		m_filePicker->setMaxVisibleItems(18);
		m_filePicker->setToolTip(QStringLiteral("The shader being edited (the archive when the folder is not shipped)"));

		m_shaderMeta = new QLabel(QStringLiteral("OpenGL 4.5 core / desktop - read-only"), infoBar);
		m_shaderMeta->setObjectName("ShaderMeta");

		m_glslStats = new QLabel(infoBar);
		m_glslStats->setObjectName("ShaderStats");

		m_copyGlsl = new QToolButton(infoBar);
		m_copyGlsl->setObjectName("MaterialButton");
		m_copyGlsl->setText(QStringLiteral("Copy GLSL"));
		m_copyGlsl->setCursor(Qt::PointingHandCursor);
		m_copyGlsl->setToolTip(QStringLiteral("Copy the active GLSL stage"));

		infoLayout->addWidget(m_filePicker, 1);
		infoLayout->addWidget(m_shaderMeta);
		infoLayout->addWidget(m_glslStats);
		infoLayout->addWidget(m_copyGlsl);

		auto* codeSplitter = new QSplitter(Qt::Horizontal);
		codeSplitter->setHandleWidth(4);
		codeSplitter->setChildrenCollapsible(false);

		auto* materialPane = new QWidget(codeSplitter);
		auto* materialLayout = new QVBoxLayout(materialPane);
		materialLayout->setContentsMargins(0, 0, 0, 0);
		materialLayout->setSpacing(0);

		auto* materialHeader = new QWidget(materialPane);
		materialHeader->setObjectName("ShaderSubHeader");
		materialHeader->setAttribute(Qt::WA_StyledBackground, true);
		auto* materialHeaderLayout = new QHBoxLayout(materialHeader);
		materialHeaderLayout->setContentsMargins(8, 3, 8, 3);
		materialHeaderLayout->setSpacing(8);

		auto* materialTitle = new QLabel(QStringLiteral("Material (.shader)"), materialHeader);
		materialTitle->setObjectName("ShaderSubTitle");
		m_materialStats = new QLabel(materialHeader);
		m_materialStats->setObjectName("ShaderStats");
		m_position = new QLabel(QStringLiteral("Ln 1, Col 1"), materialHeader);
		m_position->setObjectName("MaterialSectionHint");

		m_reload = new QToolButton(materialHeader);
		m_reload->setObjectName("MaterialButton");
		m_reload->setText(QStringLiteral("Revert"));
		m_reload->setToolTip(QStringLiteral("Discard the edits and read the file (or the archive) again"));
		m_reload->setCursor(Qt::PointingHandCursor);

		m_apply = new QToolButton(materialHeader);
		m_apply->setObjectName("MaterialPrimaryButton");
		m_apply->setText(QStringLiteral("Compile"));
		m_apply->setToolTip(QStringLiteral("Compile the source and reload the shader wherever it is used (Ctrl+Enter)"));
		m_apply->setCursor(Qt::PointingHandCursor);

		m_copyMaterial = new QToolButton(materialHeader);
		m_copyMaterial->setObjectName("MaterialButton");
		m_copyMaterial->setText(QStringLiteral("Copy"));
		m_copyMaterial->setCursor(Qt::PointingHandCursor);
		m_copyMaterial->setToolTip(QStringLiteral("Copy the .shader source"));

		materialHeaderLayout->addWidget(materialTitle);
		materialHeaderLayout->addWidget(m_materialStats);
		materialHeaderLayout->addWidget(m_position);
		materialHeaderLayout->addStretch(1);
		materialHeaderLayout->addWidget(m_reload);
		materialHeaderLayout->addWidget(m_apply);
		materialHeaderLayout->addWidget(m_copyMaterial);

		m_compileStatus = new QLabel(QStringLiteral("Ready - edit the .shader and press Compile"), materialPane);
		m_compileStatus->setObjectName("ShaderStats");
		m_compileStatus->setContentsMargins(8, 0, 8, 2);

		m_source = new ::MOON::MaterialCodeEditor(materialPane);
		materialLayout->addWidget(materialHeader);
		materialLayout->addWidget(m_compileStatus);
		materialLayout->addWidget(m_source, 1);

		auto* glslPane = new QWidget(codeSplitter);
		auto* glslLayout = new QVBoxLayout(glslPane);
		glslLayout->setContentsMargins(0, 0, 0, 0);
		glslLayout->setSpacing(0);

		m_glslTabs = new QTabWidget(glslPane);
		m_glslTabs->setDocumentMode(true);
		m_vertexEditor = new ::MOON::MaterialCodeEditor(m_glslTabs);
		m_fragmentEditor = new ::MOON::MaterialCodeEditor(m_glslTabs);
		m_geometryEditor = new ::MOON::MaterialCodeEditor(m_glslTabs);
		for (::MOON::MaterialCodeEditor* editor : { m_vertexEditor, m_fragmentEditor, m_geometryEditor })
		{
			editor->setReadOnly(true);
		}
		m_glslTabs->addTab(m_vertexEditor, QStringLiteral("Vertex"));
		m_glslTabs->addTab(m_fragmentEditor, QStringLiteral("Fragment"));
		m_glslTabs->addTab(m_geometryEditor, QStringLiteral("Geometry"));
		glslLayout->addWidget(m_glslTabs);

		codeSplitter->addWidget(materialPane);
		codeSplitter->addWidget(glslPane);
		codeSplitter->setStretchFactor(0, 2);
		codeSplitter->setStretchFactor(1, 3);
		codeSplitter->setSizes({ 300, 430 });

		// --- preview on the left, source and output on the right --------------------------
		// Side by side on purpose: the panel exists to compare what the material looks like
		// with the source that produces it, so both have to be visible at once.
		auto* columns = new QSplitter(Qt::Horizontal, content);
		columns->setHandleWidth(4);
		columns->setChildrenCollapsible(false);

		// The preview is a GL widget with a render module view behind it (see
		// MaterialPreviewWidget); the frame around it separates the rendering from the widgets.
		auto* preview = new QWidget(columns);
		auto* previewLayout = new QVBoxLayout(preview);
		previewLayout->setContentsMargins(0, 0, 0, 0);
		previewLayout->setSpacing(0);
		auto* previewHint = new QLabel(QStringLiteral("Drag to orbit, wheel to zoom"), preview);
		previewHint->setObjectName("MaterialSectionHint");

		// What the material is shown on. The sphere is the default; the cube and the plane are
		// for materials whose UV layout, tiling or planar orientation is what is being judged.
		auto* previewHeader = new QWidget(preview);
		auto* previewHeaderLayout = new QHBoxLayout(previewHeader);
		previewHeaderLayout->setContentsMargins(0, 0, 0, 0);
		previewHeaderLayout->setSpacing(6);
		previewHeaderLayout->addWidget(previewHint, 1);

		auto* meshPicker = new QComboBox(previewHeader);
		meshPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
		meshPicker->setMinimumContentsLength(6);
		meshPicker->setToolTip(QStringLiteral("What the material is shown on"));
		meshPicker->addItem(QStringLiteral("Sphere"),
			static_cast<int>(::Editor::Panels::EPreviewMesh::SPHERE));
		meshPicker->addItem(QStringLiteral("Ball"),
			static_cast<int>(::Editor::Panels::EPreviewMesh::BALL));
		meshPicker->addItem(QStringLiteral("Cube"),
			static_cast<int>(::Editor::Panels::EPreviewMesh::CUBE));
		meshPicker->addItem(QStringLiteral("Plane"),
			static_cast<int>(::Editor::Panels::EPreviewMesh::PLANE));
		meshPicker->addItem(QStringLiteral("CAD part"),
			static_cast<int>(::Editor::Panels::EPreviewMesh::CAD));
		previewHeaderLayout->addWidget(meshPicker, 0);

		auto* resetView = new QToolButton(previewHeader);
		resetView->setObjectName("MaterialLinkButton");
		resetView->setText(QStringLiteral("Reset View"));
		resetView->setCursor(Qt::PointingHandCursor);
		resetView->setToolTip(QStringLiteral("Return the preview camera to its default pose"));
		previewHeaderLayout->addWidget(resetView, 0);
		QObject::connect(resetView, &QToolButton::clicked, m_owner, [this] {
			if (m_preview != nullptr)
			{
				m_preview->ResetView();
			}
		});

		QObject::connect(meshPicker, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
			[this, meshPicker](int) {
				if (m_preview == nullptr)
				{
					return;
				}
				m_preview->SetPreviewMesh(static_cast<::Editor::Panels::EPreviewMesh>(
					meshPicker->currentData().toInt()));
				m_preview->RequestThumbnail();
			});

		previewLayout->addWidget(MakeSectionHeader(QStringLiteral("PREVIEW"), preview, previewHeader));

		auto* previewFrame = new QFrame(preview);
		previewFrame->setObjectName("MaterialPreviewFrame");
		auto* previewFrameLayout = new QVBoxLayout(previewFrame);
		previewFrameLayout->setContentsMargins(1, 1, 1, 1);
		previewFrameLayout->setSpacing(0);
		m_preview = new ::MOON::MaterialPreviewWidget(previewFrame);
		previewFrameLayout->addWidget(m_preview);
		previewLayout->addWidget(previewFrame, 1);
		// The preview belongs to the Material page only; the Shader page is all code.
		m_previewColumn = preview;

		// The output log under the code, like the console under an IDE.
		auto* output = new QWidget();
		auto* outputLayout = new QVBoxLayout(output);
		outputLayout->setContentsMargins(0, 0, 0, 0);
		outputLayout->setSpacing(0);
		m_clear = new QToolButton(output);
		m_clear->setObjectName("MaterialLinkButton");
		m_clear->setText(QStringLiteral("Clear"));
		m_clear->setCursor(Qt::PointingHandCursor);
		m_clear->setToolTip(QStringLiteral("Clear the output"));
		outputLayout->addWidget(MakeSectionHeader(QStringLiteral("OUTPUT"), output, m_clear));

		m_log = new QPlainTextEdit(output);
		m_log->setObjectName("MaterialLogView");
		m_log->setReadOnly(true);
		m_log->setFrameShape(QFrame::NoFrame);
		m_log->setMaximumBlockCount(1000);
		m_log->setFont(::MOON::MaterialCodeEditor::CodeFont());
		m_log->document()->setDocumentMargin(6.0);
		// The stylesheet paints the widget; the palette is what Qt's text machinery paints the
		// viewport and the selection with, so both are set to the same colour.
		QPalette logPalette = m_log->palette();
		logPalette.setColor(QPalette::Base, QColor(0x2b, 0x2c, 0x27));
		logPalette.setColor(QPalette::Text, QColor(0x9d, 0x99, 0x95));
		logPalette.setColor(QPalette::Highlight, QColor(0x49, 0x42, 0x57));
		m_log->setPalette(logPalette);
		outputLayout->addWidget(m_log, 1);

		auto* shaderBody = new QSplitter(Qt::Vertical);
		shaderBody->setHandleWidth(4);
		shaderBody->setChildrenCollapsible(false);
		shaderBody->addWidget(codeSplitter);
		shaderBody->addWidget(output);
		shaderBody->setStretchFactor(0, 1);
		shaderBody->setStretchFactor(1, 0);
		shaderBody->setSizes({ 520, 120 });

		// The shader page: the info strip over the code, with the log under both.
		auto* shaderPage = new QWidget(columns);
		auto* shaderPageLayout = new QVBoxLayout(shaderPage);
		shaderPageLayout->setContentsMargins(0, 0, 0, 0);
		shaderPageLayout->setSpacing(0);
		shaderPageLayout->addWidget(infoBar);
		shaderPageLayout->addWidget(shaderBody, 1);

		m_rightStack = new QStackedWidget(columns);
		m_rightStack->addWidget(shaderPage);

		m_designer = new ::MOON::MaterialDesigner(m_rightStack);
		m_rightStack->addWidget(m_designer);

		columns->addWidget(preview);
		columns->addWidget(m_rightStack);
		columns->setStretchFactor(0, 1);
		columns->setStretchFactor(1, 1);
		columns->setSizes({ 240, 400 });

		layout->addWidget(columns, 1);
		content->setLayout(layout);

		// The panel is a window now, not a dock: its content is the only child.
		auto* root = new QVBoxLayout(m_owner);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(0);
		root->addWidget(content);

		ApplyStyle(content);

		QObject::connect(m_filePicker, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
			[this](int p_index) { OnFileSelected(p_index); });
		QObject::connect(m_apply, &QToolButton::clicked, m_owner, [this] { Apply(); });
		QObject::connect(m_reload, &QToolButton::clicked, m_owner,
			[this] { LoadSelectedSource(); });

		// The usual "run" of an editor: keep the hands on the keyboard while iterating.
		auto* applyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_owner);
		applyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(applyShortcut, &QShortcut::activated, m_owner, [this] { Apply(); });
		QObject::connect(m_clear, &QToolButton::clicked, m_owner, [this] { m_log->clear(); });
		QObject::connect(m_source, &QPlainTextEdit::cursorPositionChanged, m_owner, [this] {
			const QTextCursor cursor = m_source->textCursor();
			m_position->setText(QStringLiteral("Ln %1, Col %2")
				.arg(cursor.blockNumber() + 1)
				.arg(cursor.positionInBlock() + 1));
		});
		// The preview widget compiles the GLSL itself (it has the module's context current when
		// it paints); a rejection comes back here with the driver's message.
		QObject::connect(m_preview, &::MOON::MaterialPreviewWidget::shaderFailed, m_owner,
			[this](const QString& p_error) {
				Log("[Shader] " + p_error.toStdString(), kLogError);
				SetStatus(kLogError, QStringLiteral("Shader rejected"));
			});

		// --- the mode switch and the designer --------------------------------------------------
		QObject::connect(m_modeShader, &QToolButton::clicked, m_owner, [this] { SetMode(false); });
		QObject::connect(m_modeMaterial, &QToolButton::clicked, m_owner, [this] { SetMode(true); });

		QObject::connect(m_designer, &::MOON::MaterialDesigner::materialShown, m_owner,
			[this](Core::Resources::Material* p_material) {
				// The window title is the material name, so the editor never leaves it ambiguous
				// which .mat the form and the preview belong to.
				if (p_material != nullptr)
				{
					m_owner->setWindowTitle(QString::fromStdString(
						std::filesystem::path(p_material->path).stem().string()));
				}
				else
				{
					m_owner->setWindowTitle(QStringLiteral("Material Editor"));
				}

				if (m_rightStack->currentIndex() == 1)
				{
					m_preview->ShowMaterial(p_material);
					if (p_material != nullptr)
					{
						// The browser wants a thumbnail of the material it just switched to.
						m_preview->RequestThumbnail();
					}
				}
			});
		QObject::connect(m_designer, &::MOON::MaterialDesigner::materialEdited, m_owner, [this] {
			// The designer changed the instance in place; the preview just has to paint it again,
			// and the content browser's tile for this material is stale.
			if (m_rightStack->currentIndex() == 1)
			{
				m_preview->RequestThumbnail();
			}
			emit m_owner->materialThumbnailDirty(QString::fromStdString(m_designer->CurrentKey()));
		});
		QObject::connect(m_designer, &::MOON::MaterialDesigner::statusMessage, m_owner,
			[this](const QString& p_text, bool p_error) {
				Log("[Material] " + p_text.toStdString(), p_error ? kLogError : kLogInfo);
			});
		QObject::connect(m_preview, &::MOON::MaterialPreviewWidget::thumbnailReady, m_owner,
			[this](const QImage& p_image) {
				if (m_designer != nullptr)
				{
					m_designer->SetThumbnail(p_image);
				}
			});

		QObject::connect(m_copyGlsl, &QToolButton::clicked, m_owner, [this] {
			if (::MOON::MaterialCodeEditor* editor = CurrentGlslEditor())
			{
				QApplication::clipboard()->setText(editor->toPlainText());
			}
		});
		QObject::connect(m_copyMaterial, &QToolButton::clicked, m_owner, [this] {
			QApplication::clipboard()->setText(m_source->toPlainText());
		});
		QObject::connect(m_glslTabs, &QTabWidget::currentChanged, m_owner,
			[this](int) { UpdateGlslStats(); });
		QObject::connect(m_source, &QPlainTextEdit::textChanged, m_owner, [this] {
			if (m_materialStats != nullptr)
			{
				m_materialStats->setText(QStringLiteral("%1 lines").arg(m_source->blockCount()));
			}
			if (m_compileStatus != nullptr)
			{
				m_compileStatus->setText(QStringLiteral("Modified - press Compile to reload"));
			}
		});

		// The Shader page is the default and it has no preview: the code takes the window.
		SetMode(false);
		UpdateGlslStats();
	}

	/** Switches what sits next to the preview. The preview widget is the same in both modes: it
	 * shows the designed instance in Material mode and its own shader material in Shader mode. */
	void SetMode(bool p_material)
	{
		if (m_rightStack == nullptr || m_designer == nullptr || m_preview == nullptr)
		{
			return;
		}

		m_rightStack->setCurrentIndex(p_material ? 1 : 0);
		if (m_previewColumn != nullptr)
		{
			m_previewColumn->setVisible(p_material);
		}

		if (p_material)
		{
			// Rebuild the form first: the shader may have been hot reloaded in the other page,
			// which changes the parameters the form is generated from.
			m_designer->Refresh();
			m_preview->ShowMaterial(m_designer->GetMaterial());
			if (m_designer->GetMaterial() != nullptr)
			{
				m_preview->RequestThumbnail();
			}
		}
		else
		{
			m_preview->ShowMaterial(nullptr);
			// Coming back to the source after the shader changed elsewhere: fill the GLSL
			// tabs if they are empty (the first show compiles them as well).
			if (m_ready && m_vertexEditor != nullptr && m_vertexEditor->document()->isEmpty())
			{
				Preview();
			}
		}
	}

	::MOON::MaterialCodeEditor* CurrentGlslEditor() const
	{
		if (m_glslTabs == nullptr)
		{
			return nullptr;
		}
		switch (m_glslTabs->currentIndex())
		{
		case 0: return m_vertexEditor;
		case 1: return m_fragmentEditor;
		default: return m_geometryEditor;
		}
	}

	void UpdateGlslStats()
	{
		if (m_glslStats == nullptr)
		{
			return;
		}
		const ::MOON::MaterialCodeEditor* editor = CurrentGlslEditor();
		m_glslStats->setText(editor != nullptr
			? QStringLiteral("%1 lines").arg(editor->blockCount()) : QString());
	}

	/** Fills the read-only stage tabs from one compiled package. A material has a variant per
	 * pass/feature set; the viewer shows the default one (no features), like Qt-Filament's
	 * ShaderPanel does with the package it extracts from the .filamat. */
	void UpdateGlslTabs(const std::string& p_package)
	{
		const auto clearTabs = [this] {
			if (m_vertexEditor != nullptr) { m_vertexEditor->clear(); }
			if (m_fragmentEditor != nullptr) { m_fragmentEditor->clear(); }
			if (m_geometryEditor != nullptr) { m_geometryEditor->clear(); }
			if (m_glslTabs != nullptr) { m_glslTabs->setTabVisible(2, false); }
			UpdateGlslStats();
		};

		if (p_package.empty())
		{
			clearTabs();
			return;
		}

		const Rendering::Resources::ShaderPackage package(
			p_package.data(), static_cast<uint32_t>(p_package.size()));
		if (!package.IsValid() || package.Variants().empty())
		{
			clearTabs();
			return;
		}

		Rendering::Resources::ShaderPackage::Variant chosen = package.Variants().front();
		for (const auto& variant : package.Variants())
		{
			// The default pass has no name in MoonMatc's model (the material's passes list is
			// empty for it); older packages spell it "Default".
			if ((variant.pass.empty() || variant.pass == "Default") && variant.features.empty())
			{
				chosen = variant;
				break;
			}
		}

		const std::string vertex = package.Source(
			chosen, Rendering::Resources::ShaderPackage::Stage::Vertex);
		const std::string fragment = package.Source(
			chosen, Rendering::Resources::ShaderPackage::Stage::Fragment);
		const std::string geometry = package.Source(
			chosen, Rendering::Resources::ShaderPackage::Stage::Geometry);

		m_vertexEditor->setPlainText(QString::fromStdString(vertex));
		m_fragmentEditor->setPlainText(QString::fromStdString(fragment));
		m_geometryEditor->setPlainText(QString::fromStdString(geometry));
		if (m_glslTabs->count() > 2)
		{
			m_glslTabs->setTabVisible(2, !geometry.empty());
			if (geometry.empty() && m_glslTabs->currentIndex() == 2)
			{
				m_glslTabs->setCurrentIndex(1);
			}
		}
		UpdateGlslStats();
	}

	/** The panel's own stylesheet: flat strips, a hairline between the areas, the gradient
	 * buttons and the flat combo Inviwo's theme uses. Scoped to this panel's content so it does
	 * not leak into the rest of the editor. */
	void ApplyStyle(QWidget* p_content)
	{
		p_content->setStyleSheet(QString::fromLatin1(R"(
			QWidget#MaterialEditorContent { background: #323234; }
			QWidget#MaterialToolbar { background: #323234; border-bottom: 1px solid #1b1b1d; }
			QWidget#MaterialModeBar { background: #2c2c2f; border-bottom: 1px solid #1b1b1d; }
			QToolButton#MaterialModeButton {
				color: #9aa0a6;
				padding: 5px 16px;
				border: none;
				border-bottom: 2px solid transparent;
				border-radius: 0;
				background: transparent;
			}
			QToolButton#MaterialModeButton:hover { color: #e0e0e0; background: #343438; }
			QToolButton#MaterialModeButton:checked { color: #e8e8e8; border-bottom: 2px solid #268bd2; }
			QWidget#MaterialDesignerBar { background: #323234; border-bottom: 1px solid #1b1b1d; }
			QWidget#MaterialDesigner, QWidget#MaterialDesignerForm { background: #2e2e32; }
			QScrollArea#MaterialDesignerScroll { background: #2e2e32; border: none; }
			QLabel#MaterialDesignerStatus {
				color: #7d8288;
				font-size: 11px;
				padding: 3px 8px;
				background: #2c2c2f;
				border-top: 1px solid #1b1b1d;
			}
			QLabel#MaterialDesignerHint { color: #8a8f95; font-size: 12px; background: transparent; }
			QLabel#MaterialTextureThumb { background: #2c2c2c; border: 1px solid #3a3a3e; border-radius: 3px; }
			QListWidget#MaterialBrowser {
				background: #2a2a2d;
				border: none;
				border-right: 1px solid #1b1b1d;
				color: #c8ccd0;
				outline: none;
				padding: 2px;
			}
			QListWidget#MaterialBrowser::item { padding: 5px 8px; border-radius: 2px; }
			QListWidget#MaterialBrowser::item:hover { background: #333338; }
			QListWidget#MaterialBrowser::item:selected { background: #144c72; color: #e8e8e8; }
			QLineEdit#NumberWidget { background: transparent; border: 1px solid transparent; color: #c8ccd0; }
			QLineEdit#NumberWidget:hover,
			QLineEdit#NumberWidget:focus { background: #47474b; border: 1px solid #268bd2; }
			QLineEdit { color: #c8ccd0; }
			QFrame#MaterialSectionHeader {
				background: #3a3a3d;
				border-top: 1px solid #1b1b1d;
				border-bottom: 1px solid #262629;
			}
			QLabel#MaterialSectionTitle { color: #c8ccd0; font-size: 11px; font-weight: 600; }
			QLabel#MaterialSectionHint { color: #7d8288; font-size: 11px; padding-right: 6px; }
			QComboBox {
				color: #c8ccd0;
				background: transparent;
				border: 1px solid transparent;
				border-radius: 2px;
				padding: 3px 6px;
				min-height: 18px;
			}
			QComboBox:hover, QComboBox:focus, QComboBox:on {
				background: #47474b;
				border: 1px solid #268bd2;
			}
			QComboBox::drop-down { border: none; width: 18px; }
			QComboBox::down-arrow { image: url(:/widgets/icons/arrow_down.svg); width: 10px; height: 10px; }
			QComboBox QAbstractItemView {
				background: #3a3a3d;
				color: #c8ccd0;
				border: 1px solid #1b1b1d;
				selection-background-color: #47474b;
			}
			QToolButton#MaterialPrimaryButton {
				color: #e8e8e8;
				padding: 4px 14px;
				border: 1px solid #1b1b1d;
				border-radius: 3px;
				background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
					stop:0 #6f6f75, stop:0.12 #144c72, stop:0.9 #144c72, stop:1 #222224);
			}
			QToolButton#MaterialPrimaryButton:hover { border-color: #268bd2; }
			QToolButton#MaterialPrimaryButton:pressed { padding-top: 5px; padding-left: 15px; }
			QToolButton#MaterialButton {
				color: #d7dae0;
				padding: 4px 12px;
				border: 1px solid #1b1b1d;
				border-radius: 3px;
				background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
					stop:0 #6f6f75, stop:0.12 #323235, stop:0.9 #323235, stop:1 #222224);
			}
			QToolButton#MaterialButton:hover { border-color: #268bd2; }
			QToolButton#MaterialButton:pressed { padding-top: 5px; padding-left: 13px; }
			QToolButton#MaterialLinkButton {
				color: #8f9398;
				background: transparent;
				border: none;
				padding: 0 4px;
			}
			QToolButton#MaterialLinkButton:hover { color: #e9e4de; }
			QFrame#MaterialPreviewFrame { background: #1b1b1d; border: 1px solid #1b1b1d; }
			QPlainTextEdit#MaterialLogView {
				background: #2b2c27;
				color: #9d9995;
				border: none;
				selection-background-color: #494257;
			}
			QPlainTextEdit#MaterialLogView:hover {
				background: #2b2c27;
				border: none;
			}
			QWidget#ShaderInfoBar { background: #2b2b2e; border-bottom: 1px solid #1b1b1d; }
			QWidget#ShaderSubHeader { background: #2b2b2e; border-bottom: 1px solid #1b1b1d; }
			QLabel#ShaderSubTitle { color: #d8dbe0; font-weight: 600; }
			QLabel#ShaderMeta, QLabel#ShaderStats { color: #7d8288; }
			QTabWidget::pane { border: none; background: #1e1f23; }
			QTabBar::tab {
				background: #2b2b2e;
				color: #aeb3b9;
				padding: 4px 14px;
				border: none;
			}
			QTabBar::tab:selected {
				background: #1e1f23;
				color: #e8e8e8;
				border-top: 1px solid #268bd2;
			}
			QTabBar::tab:hover { color: #ffffff; }
			QSplitter::handle { background: #2b2b2d; }
			QSplitter::handle:hover { background: #268bd2; }
		)"));
	}

	void RefreshFileList()
	{
		m_filePicker->clear();
		m_relativePaths.clear();

		// The release build packs every .shader into the binary (Tools/ShaderPack), so the
		// shader folder may not exist at all. The archive is the source of truth; a source
		// checkout additionally contributes files added since the last build.
		std::vector<std::string> names;
		const Rendering::Resources::ShaderArchive& archive = Rendering::Resources::GetShaderArchive();
		for (uint32_t i = 0; i < archive.count; ++i)
		{
			std::string name = archive.files[i].name;
			std::replace(name.begin(), name.end(), '\\', '/');
			// Source/ entries are the editor's copies of the .shader text; the package under
			// the plain name is the one to list.
			if (name.rfind("Source/", 0) == 0)
			{
				continue;
			}
			if (name.ends_with(".shader"))
			{
				names.push_back(std::move(name));
			}
		}

		std::error_code error;
		if (std::filesystem::exists(m_shaderRoot))
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(m_shaderRoot, error))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".shader")
				{
					names.push_back(
						std::filesystem::relative(entry.path(), m_shaderRoot).generic_string());
				}
			}
		}

		std::ranges::sort(names);
		names.erase(std::unique(names.begin(), names.end()), names.end());
		m_relativePaths = std::move(names);
		for (const std::string& relative : m_relativePaths)
		{
			m_filePicker->addItem(QString::fromStdString(relative));
		}
	}

	void OpenShader(const std::string& p_key)
	{
		if (m_filePicker == nullptr)
		{
			return;
		}

		// ":Shaders\\Sub\\Name.shader" -> "Sub/Name.shader", the picker's relative form.
		std::string relative = p_key;
		for (char& c : relative)
		{
			if (c == '\\')
			{
				c = '/';
			}
		}
		const std::string prefix = ":Shaders/";
		if (relative.rfind(prefix, 0) == 0)
		{
			relative = relative.substr(prefix.size());
		}

		// The content browser may have just created the file: rescan before giving up.
		RefreshFileList();
		int index = -1;
		for (size_t i = 0; i < m_relativePaths.size(); ++i)
		{
			if (m_relativePaths[i] == relative)
			{
				index = static_cast<int>(i);
				break;
			}
		}
		if (index < 0)
		{
			Log("shader not found: " + relative, kLogError);
			SetStatus(kLogError, QStringLiteral("Cannot find the shader"));
			return;
		}

		m_filePicker->setCurrentIndex(index);
		OnFileSelected(index);
	}

	void OnFileSelected(int p_row)
	{
		if (p_row < 0 || p_row >= static_cast<int>(m_relativePaths.size()))
		{
			return;
		}
		m_current = m_relativePaths[p_row];
		LoadSelectedSource();
	}

	void LoadSelectedSource()
	{
		if (m_current.empty())
		{
			return;
		}

		// The archive holds the source of every shader the release packed; a dev checkout can
		// also read files that were added since the last build.
		std::string text;
		std::string origin;
		if (!Rendering::Resources::ReadShaderSource((m_shaderRoot / m_current).string(), text, &origin))
		{
			Log("cannot read " + m_current, kLogError);
			SetStatus(kLogError, QStringLiteral("Cannot read the file"));
			return;
		}
		m_source->setPlainText(QString::fromStdString(text));
		if (m_materialStats != nullptr)
		{
			m_materialStats->setText(QStringLiteral("%1 lines").arg(m_source->blockCount()));
		}
		if (m_compileStatus != nullptr)
		{
			m_compileStatus->setText(QStringLiteral("Ready - edit the .shader and press Compile"));
		}
		Log("loaded " + origin, kLogInfo);
		SetStatus(kStatusIdle, QStringLiteral("Loaded %1").arg(QString::fromStdString(origin)));
		if (!m_ready)
		{
			UpdateGlslTabs({});
		}

		// The preview renders with the render module's context, which the viewport creates: the
		// first one waits for showEvent (OnFirstShow), the rest follow every file change.
		if (m_ready)
		{
			Preview();
		}
	}

	void Apply()
	{
		if (m_current.empty())
		{
			return;
		}
		Moon::Matc::CompileOptions options;
		options.displayName = std::filesystem::path(m_current).filename().string();
		options.includeDirs = { m_shaderRoot.string() };
		options.includeResolver = ResolveShaderInclude;

		const std::string source = m_source->toPlainText().toStdString();
		const Moon::Matc::CompileResult result = Moon::Matc::CompileSource(source, options);
		if (!result.success || result.passthrough)
		{
			Log(std::string("[MoonMatc] ") + (result.error.empty() ? "not a material (passthrough)" : result.error),
				kLogWarning);
			SetStatus(kLogWarning, QStringLiteral("Compile failed"));
			if (m_compileStatus != nullptr)
			{
				m_compileStatus->setText(QStringLiteral("Compile failed - see OUTPUT"));
			}
			return;
		}

		UpdateGlslTabs(result.package);
		if (m_compileStatus != nullptr)
		{
			m_compileStatus->setText(QStringLiteral("Compiled %1 variant(s)")
				.arg(result.variantCount));
		}

		// The shader belongs to the resource manager and is reloaded *in place*: every material
		// that was drawing with it - in the scene and in the preview - points at the same Shader
		// object, so replacing its programs switches all of them at once. No rebinding pass.
		auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
		const std::string shaderKey = ToShaderKey(m_current);
		std::string driverError;
		bool reloaded = false;

		// Shaders are GL objects, and a button callback runs with no context current. The
		// module's context is the one they belong on - it is where the draws that use them
		// happen - so it is made current for the duration of the compile. (The preview widget
		// does the same for its own compiles; the panel still never picks a context itself.)
		auto* host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
		if (host == nullptr || !host->MakeCurrent())
		{
			Log("[Shader] the render module has no GL context yet: nothing was reloaded", kLogError);
			SetStatus(kLogError, QStringLiteral("No render context"));
			return;
		}

		Rendering::Resources::Shader* shader = context.shaderManager.GetResource(shaderKey, false);
		if (shader == nullptr)
		{
			shader = Rendering::Resources::Loaders::ShaderLoader::CreateFromPackage(
				result.package.data(), static_cast<uint32_t>(result.package.size()), shaderKey, &driverError);
			if (shader != nullptr)
			{
				context.shaderManager.RegisterResource(shaderKey, shader);
				reloaded = true;
			}
		}
		else
		{
			reloaded = Rendering::Resources::Loaders::ShaderLoader::RecompileFromPackage(
				*shader, result.package.data(), static_cast<uint32_t>(result.package.size()), &driverError);
		}

		host->DoneCurrent();

		if (!reloaded)
		{
			Log("[Shader] " + (driverError.empty() ? std::string("the driver refused the material") : driverError),
				kLogError);
			SetStatus(kLogError, QStringLiteral("The driver refused the material"));
			return;
		}

		Log(m_current + " reloaded (" + std::to_string(result.variantCount)
			+ " variant(s), " + std::to_string(result.package.size()) + " bytes)", kLogSuccess);
		SetStatus(kLogSuccess, QStringLiteral("Reloaded %1").arg(QString::fromStdString(m_current)));
		Preview();
	}

	void Preview()
	{
		if (!m_ready)
		{
			// The preview widget needs the render module's context, which the viewport creates;
			// it waits for that (OnFirstShow).
			return;
		}
		if (m_current.empty() || m_preview == nullptr)
		{
			return;
		}

		// Compile what is in the editor - the same source Apply would compile - and hand the
		// package to the preview widget. The widget builds the Shader itself, on the module's
		// context; the panel still never touches GL.
		Moon::Matc::CompileOptions options;
		options.displayName = std::filesystem::path(m_current).filename().string();
		options.includeDirs = { m_shaderRoot.string() };
		options.includeResolver = ResolveShaderInclude;

		const Moon::Matc::CompileResult result = Moon::Matc::CompileSource(
			m_source->toPlainText().toStdString(), options);
		if (!result.success || result.passthrough)
		{
			Log(std::string("[MoonMatc] ") + (result.error.empty() ? "not a material (passthrough)" : result.error),
				kLogWarning);
			SetStatus(kLogWarning, QStringLiteral("Compile failed"));
			if (m_compileStatus != nullptr)
			{
				m_compileStatus->setText(QStringLiteral("Compile failed - see OUTPUT"));
			}
			m_preview->SetShaderPackage({}, {});
			UpdateGlslTabs({});
			return;
		}

		UpdateGlslTabs(result.package);
		if (m_compileStatus != nullptr)
		{
			m_compileStatus->setText(QStringLiteral("Compiled %1 variant(s)")
				.arg(result.variantCount));
		}
		m_preview->SetShaderPackage(result.package, ToShaderKey(m_current));
	}

	/** The first time the panel is shown: compile the source the file picker landed on and hand
	 * it to the preview widget, which builds the shader on its next paint (with the render
	 * module's context current). */
	void OnFirstShow()
	{
		if (!m_ready)
		{
			m_ready = true;
			Preview();
		}

		// The designer loads its material when it is first shown, which compiles that material's
		// shader: on the start page there is no context yet, so every show is a retry.
		if (m_rightStack != nullptr && m_rightStack->currentIndex() == 1 && m_designer != nullptr)
		{
			m_designer->Refresh();
		}
	}

	void SetStatus(const QColor& p_color, const QString& p_tooltip)
	{
		if (m_status == nullptr)
		{
			return;
		}
		m_status->setStyleSheet(
			QStringLiteral("background: %1; border-radius: 4px;").arg(p_color.name()));
		m_status->setToolTip(p_tooltip);
	}

	void Log(const std::string& p_text, const QColor& p_color = kLogInfo)
	{
		QTextCursor cursor(m_log->document());
		cursor.movePosition(QTextCursor::End);
		QTextCharFormat format;
		format.setForeground(p_color);
		cursor.setCharFormat(format);
		cursor.insertText(QString::fromStdString(p_text) + QLatin1Char('\n'));
		m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());

		// Also to stdout: that is where a shader compile failure shows up too, and it is what a
		// scripted run greps for. (Not CORE_INFO: the panel is built while the editor's panels
		// are still being constructed, and the logger's panel sink is not ready for it.)
		std::cout << "[MaterialEditor] " << p_text << std::endl;
	}

	MaterialEditorPanel* m_owner = nullptr;
	std::filesystem::path m_shaderRoot;
	std::vector<std::string> m_relativePaths;
	std::string m_current;
	bool m_ready = false;

	QFrame* m_status = nullptr;
	QToolButton* m_modeShader = nullptr;
	QToolButton* m_modeMaterial = nullptr;
	QStackedWidget* m_rightStack = nullptr;
	::MOON::MaterialDesigner* m_designer = nullptr;
	QComboBox* m_filePicker = nullptr;
	QToolButton* m_apply = nullptr;
	QToolButton* m_reload = nullptr;
	QToolButton* m_clear = nullptr;
	QLabel* m_position = nullptr;
	/** The surface the preview view is shown on: it renders on the module's context and pastes. */
	::MOON::MaterialPreviewWidget* m_preview = nullptr;
	/** The left column with the preview; hidden while the Shader page is active. */
	QWidget* m_previewColumn = nullptr;
	::MOON::MaterialCodeEditor* m_source = nullptr;
	QPlainTextEdit* m_log = nullptr;

	/** The Shader page's code viewer: one editable source, one read-only tab per GLSL stage. */
	QLabel* m_shaderMeta = nullptr;
	QLabel* m_materialStats = nullptr;
	QLabel* m_glslStats = nullptr;
	QLabel* m_compileStatus = nullptr;
	QToolButton* m_copyGlsl = nullptr;
	QToolButton* m_copyMaterial = nullptr;
	QTabWidget* m_glslTabs = nullptr;
	::MOON::MaterialCodeEditor* m_vertexEditor = nullptr;
	::MOON::MaterialCodeEditor* m_fragmentEditor = nullptr;
	::MOON::MaterialCodeEditor* m_geometryEditor = nullptr;
};

MOON::MaterialEditorPanel::MaterialEditorPanel(QWidget* p_parent)
	: QWidget(p_parent)
	, mInternal(new MaterialEditorPanelInternal(this))
{
	// A window of its own rather than a dock: materials are opened from the content browser,
	// so the editor is a place you go to, not a panel that shares the workspace's layout.
	setWindowFlags(Qt::Window);
	setWindowTitle(QStringLiteral("Material Editor"));
	resize(1120, 720);

	RegService(MaterialEditorPanel, *this);
}

void MOON::MaterialEditorPanel::OpenShader(const std::string& p_key)
{
	if (mInternal == nullptr)
	{
		return;
	}

	show();
	raise();
	activateWindow();
	mInternal->m_modeShader->setChecked(true);
	mInternal->SetMode(false);
	mInternal->OpenShader(p_key);
}

void MOON::MaterialEditorPanel::OpenMaterial(const std::string& p_key)
{
	if (mInternal == nullptr)
	{
		return;
	}

	show();
	raise();
	activateWindow();
	mInternal->m_modeMaterial->setChecked(true);
	mInternal->SetMode(true);
	if (mInternal->m_designer != nullptr)
	{
		mInternal->m_designer->OpenMaterial(p_key);
	}
}

void MOON::MaterialEditorPanel::showEvent(QShowEvent* p_event)
{
	QWidget::showEvent(p_event);
	if (mInternal != nullptr)
	{
		mInternal->OnFirstShow();
	}
}

MOON::MaterialEditorPanel::~MaterialEditorPanel()
{
	delete mInternal;
}
