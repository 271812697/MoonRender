#include "MaterialEditorPanel.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "renderer/Context.h"
#include "Core/Rendering/MaterialPreviewRenderer.h"   // RenderMaterialPreview's return type needs the QImage only
#include "Core/SceneSystem/Scene.h"
#include "Core/ECS/Components/CMaterialRenderer.h"
#include "Rendering/Data/Material.h"
#include "Rendering/Resources/Loaders/ShaderLoader.h"
#include "Tools/Utils/PathParser.h"
#include "editor/View/sceneview/viewerwidget.h"

#include "Matc.h"

namespace
{
	/** ":Shaders\\PostProcess\\Blur.ovfx" for "PostProcess/Blur.ovfx", which is what the archive
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

		// The first preview needs the viewport's GL context. Asking for it from the constructor
		// would touch GL with no context current; the dock may also start in a hidden tab and
		// never see a show event - so wait for the context, then do it once.
		m_initTimer = new QTimer(m_owner);
		QObject::connect(m_initTimer, &QTimer::timeout, m_owner, [this] {
			if (!m_ready && GetViewerWidget.HasGLContext())
			{
				m_initTimer->stop();
				OnFirstShow();
			}
		});
		m_initTimer->start(300);
	}

	void BuildUi()
	{
		// A dock widget already has a layout of its own; its content is a widget we fill.
		// Laid out vertically on purpose: the panel lives in a dock, and a dock is usually tall
		// and narrow rather than the other way round.
		auto* content = new QWidget(m_owner);
		auto* layout = new QVBoxLayout(content);

		auto* top = new QHBoxLayout();
		m_filePicker = new QComboBox(content);
		m_apply = new QPushButton("Apply", content);
		m_reload = new QPushButton("Reload", content);
		top->addWidget(m_filePicker, 1);
		top->addWidget(m_apply);
		top->addWidget(m_reload);

		auto* splitter = new QSplitter(Qt::Vertical, content);

		m_preview = new QLabel(splitter);
		m_preview->setMinimumHeight(200);
		m_preview->setAlignment(Qt::AlignCenter);
		m_preview->setText("(no preview)");

		m_source = new QPlainTextEdit(splitter);
		m_source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		m_source->setTabStopDistance(32);

		m_log = new QPlainTextEdit(splitter);
		m_log->setReadOnly(true);
		m_log->setMaximumHeight(120);
		m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

		splitter->addWidget(m_preview);
		splitter->addWidget(m_source);
		splitter->addWidget(m_log);
		splitter->setStretchFactor(1, 3);

		layout->addLayout(top);
		layout->addWidget(splitter, 1);
		content->setLayout(layout);
		m_owner->setWidget(content);

		QObject::connect(m_filePicker, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
			[this](int p_index) { OnFileSelected(p_index); });
		QObject::connect(m_apply, &QPushButton::clicked, m_owner, [this] { Apply(); });
		QObject::connect(m_reload, &QPushButton::clicked, m_owner, [this] { LoadSelectedSource(); });
		// The viewport renders the preview inside its own frame and hands the image back here;
		// the panel never touches GL.
		QObject::connect(&GetViewerWidget, &::MOON::ViewerWidget::materialPreviewReady, m_owner,
			[this](const QImage& p_image) {
				if (p_image.isNull()) {
					m_preview->setText("(preview failed)");
					return;
				}
				m_preview->setPixmap(QPixmap::fromImage(p_image));
			});
	}
	void RefreshFileList()
	{
		m_filePicker->clear();
		m_relativePaths.clear();

		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_shaderRoot, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".ovfx")
			{
				continue;
			}
			m_relativePaths.push_back(std::filesystem::relative(entry.path(), m_shaderRoot).generic_string());
		}
		std::ranges::sort(m_relativePaths);
		for (const std::string& relative : m_relativePaths)
		{
			m_filePicker->addItem(QString::fromStdString(relative));
		}
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

		std::ifstream stream(m_shaderRoot / m_current, std::ios::binary);
		if (!stream)
		{
			Log("cannot read " + m_current);
			return;
		}
		const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		m_source->setPlainText(QString::fromStdString(text));
		Log("loaded " + m_current);

		// The preview renders with the viewport's GL context, which does not exist until the
		// workspace is shown: the first one waits for showEvent (OnFirstShow), the rest follow
		// every file change.
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

		const std::string source = m_source->toPlainText().toStdString();
		const Moon::Matc::CompileResult result = Moon::Matc::CompileSource(source, options);
		if (!result.success || result.passthrough)
		{
			Log(std::string("[MoonMatc] ") + (result.error.empty() ? "not a material (passthrough)" : result.error));
			return;
		}

		// Building the Shader compiles GLSL, so it has to happen with the viewport's context
		// current - that is what the viewport's entry point is for.
		std::string driverError;
		Rendering::Resources::Shader* shader = GetViewerWidget.CreateShaderFromPackage(
			result.package, ToShaderKey(m_current), driverError);
		if (shader == nullptr)
		{
			Log("[Shader] " + (driverError.empty() ? std::string("the driver refused the material") : driverError));
			return;
		}

		// Everything that was drawing this material switches to the new shader, so the viewport
		// shows the reload on its next frame.
		const int rebound = RebindSceneMaterials(ToShaderKey(m_current), shader);

		Log(m_current + " reloaded (" + std::to_string(result.variantCount)
			+ " variant(s), " + std::to_string(result.package.size()) + " bytes, "
			+ std::to_string(rebound) + " scene material(s) rebound)");
		Preview();
	}

	/** Points every scene material that was drawing with p_previous at p_new, so the next
	 * frame in the viewport shows the reloaded shader too. Returns how many it swapped - the
	 * panel reports that number, which is what makes "the viewport updated" checkable. */
	int RebindSceneMaterials(const std::string& p_shaderPath, Rendering::Resources::Shader* p_new)
	{
		auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
		if (p_new == nullptr || context.sceneManager.GetCurrentScene() == nullptr)
		{
			return 0;
		}

		int rebound = 0;
		int actors = 0;
		int materialsSeen = 0;
		for (::Core::ECS::Actor* actor : context.sceneManager.GetCurrentScene()->GetActors())
		{
			if (actor == nullptr)
			{
				continue;
			}
			++actors;
			if (auto* materials = actor->GetComponent<::Core::ECS::Components::CMaterialRenderer>())
			{
				for (::Core::Resources::Material* material : materials->GetMaterials())
				{
					if (material != nullptr) { ++materialsSeen; }
					if (material != nullptr && material->GetShader() != nullptr && material->GetShader()->path == p_shaderPath)
					{
						material->SetShader(p_new);
						++rebound;
					}
				}
			}
		}
		return rebound;
	}

	void Preview()
	{
		if (!m_ready)
		{
			// Loading a shader from the manager compiles it, and that needs the viewport's
			// context: the preview waits for it (OnFirstShow).
			return;
		}

		// A different file means a different shader: the preview - and the "previous shader"
		// Apply compares against - starts from the archive's version of *this* material, not
		// from whatever was applied to the last one.
		// The panel does not touch GL at all: it leaves a request and the viewport renders the
		// preview inside its own frame, where the context is current and the render module is in
		// a known state, then hands the image back through materialPreviewReady.
		GetViewerWidget.RequestMaterialPreview(ToShaderKey(m_current), 256);
	}

	/** The first time the panel is shown: the viewport has a GL context by then, which is what
	 * the preview needs (rendering one earlier would touch GL with no context current). */
	void OnFirstShow()
	{
		if (m_ready)
		{
			return;
		}
		m_ready = true;
		Preview();
	}

	void Log(const std::string& p_text)
	{
		m_log->appendPlainText(QString::fromStdString(p_text));
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
	/** The file the preview material's shader came from, so switching files resets it once. */

	QComboBox* m_filePicker = nullptr;
	QTimer* m_initTimer = nullptr;
	QPlainTextEdit* m_source = nullptr;
	QPlainTextEdit* m_log = nullptr;
	QLabel* m_preview = nullptr;
	QPushButton* m_apply = nullptr;
	QPushButton* m_reload = nullptr;


};

MOON::MaterialEditorPanel::MaterialEditorPanel(QWidget* p_parent)
	: QDockWidget(p_parent)
	, mInternal(new MaterialEditorPanelInternal(this))
{
}

void MOON::MaterialEditorPanel::showEvent(QShowEvent* p_event)
{
	QDockWidget::showEvent(p_event);
	if (mInternal != nullptr)
	{
		mInternal->OnFirstShow();
	}
}

MOON::MaterialEditorPanel::~MaterialEditorPanel()
{
	delete mInternal;
}