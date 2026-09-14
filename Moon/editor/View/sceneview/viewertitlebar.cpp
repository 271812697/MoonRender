#include "viewertitlebar.h"
#include "Core/Global/ServiceLocator.h"
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include "renderer/SceneView.h"
#include "Core/ECS/Components/CMaterialRenderer.h"
#include "editor/Command/viewer/CameraFitCommand.h"
#include "renderer/PointRenderPass.h"
#include "renderer/GizmoRenderPass.h"
#include "core/callbackManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QMenu>
#include <QActionGroup>
#include <QProxyStyle>
#include <QFrame>
#include <QPointer>
#include <QEvent>
#include <QTimer>
#include <algorithm>

namespace MOON {

	namespace
	{
		/** Free space between the floating card and the edge of the viewport. */
		constexpr int kOverlayMargin = 10;
		/** Icon size of the category rows in the card. The buttons of a category
		 * live in its menu, so they follow the menu icon size of the style. */
		constexpr int kIconSize = 40;
		/** Rounding of the card and of its menus, in pixels. */
		constexpr int kCardRadius = 8;
		/** Alpha of the card and the menus over the scene: the scene has to stay
		 * readable behind them, but the buttons must stay legible. */
		constexpr int kPanelAlpha = 200;

		/** The card: one translucent rounded panel holding flat buttons. */
		QString OverlayStyleSheet()
		{
			return QStringLiteral(
				"#ViewerOverlayBar{background:rgba(32,35,42,%1);border-radius:%2px;}"
				"QToolButton{background:transparent;border:none;border-radius:%2px;padding:6px;}"
				"QToolButton:hover{background:rgba(255,255,255,45);}"
				"QToolButton:checked{background:rgba(90,150,255,130);}")
				.arg(kPanelAlpha)
				.arg(kCardRadius);
		}

		/** The menus look like the card they drop from, so the two read as one. */
		QString MenuStyleSheet()
		{
			return QStringLiteral(
				"QMenu{background:rgba(32,35,42,%1);border:none;border-radius:%2px;padding:4px;}"
				"QMenu::item{padding:6px 26px 6px 10px;border-radius:6px;}"
				"QMenu::item:selected{background:rgba(90,150,255,130);}")
				.arg(kPanelAlpha)
				.arg(kCardRadius);
		}

		/** Makes the icons of the menus as big as the icons in the card.
		 *
		 * Menu items take their icon size from the style (PM_SmallIconSize, 16px
		 * here), which looked out of place next to the 40px category icons; a proxy
		 * style is the supported way to change it. */
		class MenuIconSizeStyle : public QProxyStyle
		{
		public:
			void SetMenuIconSize(int p_size)
			{
				mMenuIconSize = p_size;
			}

			int pixelMetric(
				PixelMetric p_metric,
				const QStyleOption* p_option = nullptr,
				const QWidget* p_widget = nullptr) const override
			{
				if (p_metric == QStyle::PM_SmallIconSize && mMenuIconSize > 0)
				{
					return mMenuIconSize;
				}
				return QProxyStyle::pixelMetric(p_metric, p_option, p_widget);
			}

		private:
			int mMenuIconSize = -1;
		};

		/** The one style all the category menus share. */
		MenuIconSizeStyle* MenuStyle()
		{
			static MenuIconSizeStyle* style = nullptr;
			if (style == nullptr)
			{
				style = new MenuIconSizeStyle;
				style->SetMenuIconSize(kIconSize);
			}
			return style;
		}
	}

	class  WireCommand : public Command
	{
	public:
		WireCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(true);

			setAction(action);
			setIcon(QString::fromUtf8(":/widgets/icons/wire.png"));
		}
	protected:
		virtual void execute()override {
			bool value=action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			if (view.IsSelectActor()) {
				auto matList=view.GetSelectedActor()->GetComponent<Core::ECS::Components::CMaterialRenderer>();
				if (matList) {
					auto mat = matList->GetMaterialAtIndex(0);
					if (mat&&mat->SupportsFeature("WITH_EDGE")) {
						mat->EnableFeature("WITH_EDGE",value);
					}
				}
			}
		}
	};
	class  PointsCommand : public Command
	{
	public:
		PointsCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			setIcon(QString::fromUtf8(":/widgets/icons/points.png"));
			
		}
	protected:
		virtual void execute()override {
			bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			view.GetRenderer().GetPass<Editor::Rendering::PointRenderPass>("PointDraw").SetEnabled(value);
		}
	};
	class  MeasureCommand : public Command
	{
	public:
		MeasureCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			setIcon(QString::fromUtf8(":/widgets/icons/pqRuler.svg"));
			createCallBack(CallBackManager::instance(), [this]() {
				this->execute();
				});

		}
	protected:
		virtual void execute()override {
			bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").enableGizmoWidget("Measure",value);
		}
	};
	class  ClipCommand : public Command
	{
	public:
		ClipCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			setIcon(QString::fromUtf8(":/widgets/icons/pqClip.svg"));
			//createCallBack(CallBackManager::instance(), [this]() {
			//	this->execute();
			//	});


		}
	protected:
		virtual void execute()override {
			bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").enableGizmoWidget("ClipPlane", value);
			auto& feature = view.GetRenderer().GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
			feature.EnableClip(value);
		}
	};

	/** Switches the camera between the two projection modes.
	 *
	 * The two are one exclusive choice: the menu shows the mode the camera is
	 * actually in, and picking the other entry applies it straight away. */
	class CameraProjectionCommand : public Command
	{
	public:
		enum class Mode
		{
			Perspective,
			Orthographic
		};

		CameraProjectionCommand(QObject* parent, Mode mode) : Command(parent), mMode(mode)
		{
			auto action = new QAction(this);
			action->setCheckable(true);
			action->setText(mode == Mode::Perspective
				? QStringLiteral("Perspective projection")
				: QStringLiteral("Orthographic projection"));
			setAction(action);
			// There is no icon per projection mode yet: the camera icon keeps the
			// pair recognisable next to the display toggles above it.
			setIcon(QString::fromUtf8(":/widgets/icons/pqCamera.svg"));
		}

	protected:
		void execute() override
		{
			auto& view = GetService(Editor::Panels::SceneView);
			auto* camera = view.GetCamera();
			if (camera == nullptr)
			{
				return;
			}
			camera->SetProjectionMode(mMode == Mode::Perspective
				? ::Rendering::Settings::EProjectionMode::PERSPECTIVE
				: ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC);
		}

	private:
		Mode mMode;
	};

	/** Everything the overlay is made of: the commands and the category rows.
	 *
	 * The card is a menu bar turned on its side: one row per category, and clicking
	 * a row drops that category's buttons next to it. Only the categories live in
	 * the viewport, so the card keeps its size no matter how many buttons a
	 * category ends up with.
	 *
	 * To add a button, append its action to the category it belongs to in
	 * CreateCategories(). To add a category, call AddCategory() once more: the rows
	 * and the menus are built from that list, so nothing else has to change.
	 */
	class ViewerWindowTitleBar::ViewerWindowTitleBarInternal {
	public:
		ViewerWindowTitleBarInternal(ViewerWindowTitleBar* titleBar) :mSelf(titleBar) {
			CreateCommands();
			CreateCategories();
			SetupProjectionPair();
		}
		~ViewerWindowTitleBarInternal() {

		}

	private:
		/** One category: the row in the card and the menu it drops. */
		struct Category
		{
			QString name;
			QToolButton* button = nullptr;
			QMenu* menu = nullptr;
		};

		void CreateCommands()
		{
			xMinus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_NEGATIVE_X);
			xPlus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_POSITIVE_X);
			yMinus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_NEGATIVE_Y);
			yPlus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_POSITIVE_Y);
			zMinus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_NEGATIVE_Z);
			zPlus = new CameraFitCommand(mSelf, CameraFitCommand::Mode::RESET_POSITIVE_Z);
			isometricView = new CameraFitCommand(mSelf, CameraFitCommand::Mode::APPLY_ISOMETRIC_VIEW);
			zoomToSelection = new CameraFitCommand(mSelf, CameraFitCommand::Mode::ZOOM_TO_DATA);
			rotateCameraCCW = new CameraFitCommand(mSelf, CameraFitCommand::Mode::ROTATE_CAMERA_CCW);
			rotateCameraCW = new CameraFitCommand(mSelf, CameraFitCommand::Mode::ROTATE_CAMERA_CW);

			xPlus->setIcon(QString::fromUtf8(":/widgets/icons/pqXPlus.svg"));
			xMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqXMinus.svg"));
			yPlus->setIcon(QString::fromUtf8(":/widgets/icons/pqYPlus.svg"));
			yMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqYMinus.svg"));
			zPlus->setIcon(QString::fromUtf8(":/widgets/icons/pqZPlus.svg"));
			zMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqZMinus.svg"));
			isometricView->setIcon(QString::fromUtf8(":/widgets/icons/pqIsometricView.svg"));
			zoomToSelection->setIcon(QString::fromUtf8(":/widgets/icons/pqZoomToSelection.svg"));
			rotateCameraCW->setIcon(QString::fromUtf8(":/widgets/icons/pqRotateCameraCW.svg"));
			rotateCameraCCW->setIcon(QString::fromUtf8(":/widgets/icons/pqRotateCameraCCW.svg"));

			// The names the menus show next to the icons.
			xPlus->action()->setText(QStringLiteral("Look from +X"));
			xMinus->action()->setText(QStringLiteral("Look from -X"));
			yPlus->action()->setText(QStringLiteral("Look from +Y"));
			yMinus->action()->setText(QStringLiteral("Look from -Y"));
			zPlus->action()->setText(QStringLiteral("Look from +Z"));
			zMinus->action()->setText(QStringLiteral("Look from -Z"));
			isometricView->action()->setText(QStringLiteral("Isometric view"));
			zoomToSelection->action()->setText(QStringLiteral("Zoom to selection"));
			rotateCameraCW->action()->setText(QStringLiteral("Rotate camera CW"));
			rotateCameraCCW->action()->setText(QStringLiteral("Rotate camera CCW"));

			wire = new WireCommand(mSelf);
			points = new PointsCommand(mSelf);
			measure = new MeasureCommand(mSelf);
			clip = new ClipCommand(mSelf);

			wire->action()->setText(QStringLiteral("Wireframe"));
			points->action()->setText(QStringLiteral("Points"));
			measure->action()->setText(QStringLiteral("Measure"));
			clip->action()->setText(QStringLiteral("Clip plane"));

			perspectiveProjection =
				new CameraProjectionCommand(mSelf, CameraProjectionCommand::Mode::Perspective);
			orthographicProjection =
				new CameraProjectionCommand(mSelf, CameraProjectionCommand::Mode::Orthographic);
		}

		void CreateCategories()
		{
			// The layout: the categories side by side, nothing else. The buttons of a
			// category only appear while its menu is open.
			mBarLayout = new QHBoxLayout(mSelf);
			mBarLayout->setContentsMargins(4, 4, 4, 4);
			mBarLayout->setSpacing(2);

			AddCategory(
				QStringLiteral("View"),
				QString::fromUtf8(":/widgets/icons/pqIsometricView.svg"),
				{ xPlus->action(), xMinus->action(), yPlus->action(), yMinus->action(),
				  zPlus->action(), zMinus->action(), isometricView->action(),
				  // A null entry asks for a separator between the fixed views above
				  // and the camera moves below.
				  nullptr,
				  zoomToSelection->action(), rotateCameraCW->action(), rotateCameraCCW->action() });

			AddCategory(
				QStringLiteral("Camera"),
				QString::fromUtf8(":/widgets/icons/pqCamera.svg"),
				{ perspectiveProjection->action(), orthographicProjection->action() });

			AddCategory(
				QStringLiteral("Display"),
				// The eye only lives under the /entityTree prefix in the qrc.
				QString::fromUtf8(":/entityTree/icons/pqEyeball.svg"),
				{ wire->action(), points->action(), measure->action(), clip->action() });
		}

		void AddCategory(
			const QString& p_name,
			const QString& p_icon,
			const std::vector<QAction*>& p_actions)
		{
			Category category;
			category.name = p_name;

			const int index = static_cast<int>(mCategories.size());

			// The row: the icon only, the name lives in the tool tip. The icon makes
			// the card as narrow as the old button strip was.
			category.button = new QToolButton(mSelf);
			category.button->setIcon(QIcon(p_icon));
			category.button->setIconSize(QSize(kIconSize, kIconSize));
			category.button->setToolButtonStyle(Qt::ToolButtonIconOnly);
			category.button->setAutoRaise(true);
			category.button->setFocusPolicy(Qt::NoFocus);
			category.button->setToolTip(p_name);
			mBarLayout->addWidget(category.button);

			// The menu: every button of this category, icons and check marks included.
			category.menu = new QMenu(category.button);
			category.menu->setStyle(MenuStyle());
			// Same translucent card as the panel, no native frame or shadow, so the
			// menu reads as a part of the card instead of a separate window.
			category.menu->setStyleSheet(MenuStyleSheet());
			category.menu->setWindowFlags(
				category.menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
			category.menu->setAttribute(Qt::WA_TranslucentBackground);
			for (QAction* action : p_actions)
			{
				if (action == nullptr)
				{
					// A null entry in the list asks for a separator.
					category.menu->addSeparator();
					continue;
				}
				category.menu->addAction(action);
			}
			// The row stays highlighted while its menu is open, like a menu bar item.
			connect(category.menu, &QMenu::aboutToHide, mSelf, [button = category.button]()
				{
					button->setDown(false);
				});

			// mSelf is the QObject here: the internal helper is not one.
			connect(category.button, &QToolButton::clicked, mSelf, [this, index]()
				{
					OpenCategory(index);
				});

			mCategories.push_back(category);
		}

		/** Drops the buttons of one category next to its row, the way a menu bar
		 * drops a menu. */
		void OpenCategory(int p_index)
		{
			if (p_index < 0 || p_index >= static_cast<int>(mCategories.size()))
			{
				return;
			}
			Category& category = mCategories[p_index];
			category.button->setDown(true);
			// Below the card, left edge on the row that was clicked: a menu bar drops
			// its menus from one line, so they do not jump around per button. popup()
			// instead of exec() keeps this non blocking, and the row is unhighlighted
			// again by the menu's aboutToHide signal.
			const QPoint origin = mSelf->mapToGlobal(
				QPoint(category.button->x(), mSelf->height() + 2));
			category.menu->popup(origin);
		}

		/** The menu of a category by name, so a category can be reached after the
		 * table above was built. */
		QMenu* FindCategoryMenu(const QString& p_name) const
		{
			for (const Category& category : mCategories)
			{
				if (category.name == p_name)
				{
					return category.menu;
				}
			}
			return nullptr;
		}

		/** Wires the projection pair: exclusive, and refreshing its marks from the
		 * camera whenever the menu opens, because the mode changes behind our back
		 * (entering a sketch switches to orthographic, for instance). */
		void SetupProjectionPair()
		{
			auto* group = new QActionGroup(mSelf);
			group->setExclusive(true);
			group->addAction(perspectiveProjection->action());
			group->addAction(orthographicProjection->action());

			if (QMenu* menu = FindCategoryMenu(QStringLiteral("Camera")))
			{
				connect(menu, &QMenu::aboutToShow, mSelf, [this]()
					{
						SyncProjectionMarks();
					});
			}
			// No sync here: the card is built while the view (and the service locator
			// entry for it) is still being created, so asking for the camera now would
			// look up a service that does not exist yet. aboutToShow above covers the
			// first time the menu is opened.
		}

		/** Shows which projection mode the camera is in. */
		void SyncProjectionMarks()
		{
			auto& view = GetService(Editor::Panels::SceneView);
			auto* camera = view.GetCamera();
			if (camera == nullptr)
			{
				return;
			}
			const bool orthographic =
				camera->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC;
			orthographicProjection->action()->setChecked(orthographic);
			perspectiveProjection->action()->setChecked(!orthographic);
		}

	private:
		ViewerWindowTitleBar* mSelf = nullptr;
		/** Holds one row per category. */
		QHBoxLayout* mBarLayout = nullptr;
		std::vector<Category> mCategories;

		CameraFitCommand* xMinus = nullptr;
		CameraFitCommand* xPlus = nullptr;
		CameraFitCommand* yMinus = nullptr;
		CameraFitCommand* yPlus = nullptr;
		CameraFitCommand* zMinus = nullptr;
		CameraFitCommand* zPlus = nullptr;
		CameraFitCommand* isometricView = nullptr;
		CameraFitCommand* zoomToSelection = nullptr;
		CameraFitCommand* rotateCameraCCW = nullptr;
		CameraFitCommand* rotateCameraCW = nullptr;
		WireCommand* wire = nullptr;
		PointsCommand* points = nullptr;
		MeasureCommand* measure = nullptr;
		ClipCommand* clip = nullptr;
		CameraProjectionCommand* perspectiveProjection = nullptr;
		CameraProjectionCommand* orthographicProjection = nullptr;
	};

	ViewerWindowTitleBar::ViewerWindowTitleBar(QWidget* parent)
		: QWidget(parent), mInternal(new ViewerWindowTitleBarInternal(this))
	{
		// A translucent card floating in the viewport; the internal constructor has
		// already filled it with the category rows.
		setObjectName(QStringLiteral("ViewerOverlayBar"));
		setAttribute(Qt::WA_TranslucentBackground);
		setStyleSheet(OverlayStyleSheet());
		setFocusPolicy(Qt::NoFocus);

		if (parent != nullptr)
		{
			// Follow the viewport: it is the one that changes size, not the card.
			parent->installEventFilter(this);
		}

		// The buttons only reach their final size once the layout and the style have
		// run, so the first placement is left to the event loop; showEvent() and
		// resizeEvent() keep it centered from then on.
		QTimer::singleShot(0, this, [this]()
			{
				adjustSize();
				PlaceOverlay();
			});
	}

	ViewerWindowTitleBar::~ViewerWindowTitleBar()
	{
		delete mInternal;
	}

	void ViewerWindowTitleBar::PlaceOverlay()
	{
		QWidget* host = parentWidget();
		if (host == nullptr)
		{
			return;
		}
		// Centered along the top edge: clear of the FPS text in the top left corner
		// and of the navigation cube with its rotate ring in the top right one.
		const int x = std::max(kOverlayMargin, (host->width() - width()) / 2);
		move(x, kOverlayMargin);
		raise();
	}

	void ViewerWindowTitleBar::resizeEvent(QResizeEvent* p_event)
	{
		QWidget::resizeEvent(p_event);
		// Keep the center: only moving here, so this cannot recurse.
		PlaceOverlay();
	}

	void ViewerWindowTitleBar::showEvent(QShowEvent* p_event)
	{
		QWidget::showEvent(p_event);
		adjustSize();
		PlaceOverlay();
	}

	bool ViewerWindowTitleBar::eventFilter(QObject* p_watched, QEvent* p_event)
	{
		if (p_watched == parentWidget() && p_event != nullptr)
		{
			switch (p_event->type())
			{
			case QEvent::Resize:
			case QEvent::Show:
				adjustSize();
				PlaceOverlay();
				break;
			default:
				break;
			}
		}
		return QWidget::eventFilter(p_watched, p_event);
	}
}
