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
#include <QToolBar>
#include <QPointer>
#include <QEvent>
#include <QTimer>
#include <algorithm>

namespace MOON {

	namespace
	{
		/** Free space between the floating bar and the edge of the viewport. */
		constexpr int kOverlayMargin = 10;
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
	class ViewerWindowTitleBar::ViewerWindowTitleBarInternal {
	public:
		ViewerWindowTitleBarInternal(ViewerWindowTitleBar* titleBar) :mSelf(titleBar) {



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
			mSelf->addAction(xPlus->action());
			
			xMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqXMinus.svg"));
			mSelf->addAction(xMinus->action());
			yPlus->setIcon(QString::fromUtf8(":/widgets/icons/pqYPlus.svg"));
			mSelf->addAction(yPlus->action());
			yMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqYMinus.svg"));
			mSelf->addAction(yMinus->action());
			zPlus->setIcon(QString::fromUtf8(":/widgets/icons/pqZPlus.svg"));
			mSelf->addAction(zPlus->action());
			zMinus->setIcon(QString::fromUtf8(":/widgets/icons/pqZMinus.svg"));
			mSelf->addAction(zMinus->action());
			isometricView->setIcon(QString::fromUtf8(":/widgets/icons/pqIsometricView.svg"));
			mSelf->addAction(isometricView->action());
			zoomToSelection->setIcon(QString::fromUtf8(":/widgets/icons/pqZoomToSelection.svg"));
			mSelf->addAction(zoomToSelection->action());
			rotateCameraCW->setIcon(QString::fromUtf8(":/widgets/icons/pqRotateCameraCW.svg"));
			mSelf->addAction(rotateCameraCW->action());
			rotateCameraCCW->setIcon(QString::fromUtf8(":/widgets/icons/pqRotateCameraCCW.svg"));
			mSelf->addAction(rotateCameraCCW->action());

			wire = new WireCommand(mSelf);
			mSelf->addAction(wire->action());

			points = new PointsCommand(mSelf);
			mSelf->addAction(points->action());

			measure = new MeasureCommand(mSelf);
			mSelf->addAction(measure->action());
			clip = new ClipCommand(mSelf);
			mSelf->addAction(clip->action());
		
		}
		~ViewerWindowTitleBarInternal() {

		}
	private:
		
		ViewerWindowTitleBar* mSelf = nullptr;
		
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
	};
	ViewerWindowTitleBar::ViewerWindowTitleBar(QWidget* parent) :QToolBar(parent), mInternal(new ViewerWindowTitleBarInternal(this))
	{
		// The bar floats inside the viewport it is given instead of sitting in the
		// window's toolbar area: vertical along the left edge, translucent so the
		// scene stays readable behind it, and it never docks or floats away.
		setOrientation(Qt::Vertical);
		setMovable(false);
		setFloatable(false);
		setAttribute(Qt::WA_TranslucentBackground);
		setStyleSheet(
			"QToolBar{background:rgba(32,35,42,170);"
			"border:none;border-radius:8px;padding:3px;}"
			"QToolButton{background:transparent;border:none;border-radius:5px;padding:3px;}"
			"QToolButton:hover{background:rgba(255,255,255,45);}"
			"QToolButton:checked{background:rgba(90,150,255,130);}");
		if (layout() != nullptr)
		{
			layout()->setSpacing(2);
			layout()->setContentsMargins(3, 3, 3, 3);
		}

		if (parent != nullptr)
		{
			// Follow the viewport: it is the one that changes size, not the bar.
			parent->installEventFilter(this);
		}

		// The buttons only reach their final size once the layout and the style
		// have run, so the first placement is left to the event loop; showEvent()
		// and resizeEvent() keep it centered from then on.
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
		// Left edge, centered vertically. When the bar is taller than the viewport
		// it stays pinned to the top so its first buttons remain reachable.
		const int y = std::max(kOverlayMargin, (host->height() - height()) / 2);
		move(kOverlayMargin, y);
		raise();
	}

	void ViewerWindowTitleBar::resizeEvent(QResizeEvent* p_event)
	{
		QToolBar::resizeEvent(p_event);
		// Keep the center: only moving here, so this cannot recurse.
		PlaceOverlay();
	}

	void ViewerWindowTitleBar::showEvent(QShowEvent* p_event)
	{
		QToolBar::showEvent(p_event);
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
		return QToolBar::eventFilter(p_watched, p_event);
	}
}
