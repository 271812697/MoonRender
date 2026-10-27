#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTimer>
#include "renderer/QtGLContextHost.h"
#include "viewerwidget.h"
#include <Rendering/View/InputEvent.h>
#include <QElapsedTimer>
#include "glloader.h"
#define  __glad_h_
#include "core/callbackManager.h"
#include "renderer/Context.h"
#include "renderer/SceneView.h"
#include "Core/Global/ServiceLocator.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/SceneSystem/Scene.h>
#include "Core/ECS/Components/CMaterialRenderer.h"
#include "Rendering/Resources/Mesh.h"
#include "Rendering/Geometry/bbox.h"
#include "editor/parsescene.h"
#include "editor/UI/HomePanel/HomePanel.h"
#include "feature/MoonDocument.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "editor/UI/PropertyPanel/PropertyWidget.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include "editor/UI/SettingPanel/PassSettingWidget.h"

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "core/log.h"
#include "Core/Rendering/GbufferPass.h"
#include "Core/Rendering/HzbBuildPass.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include <Rendering/Overlay/Im2DRender.h>
#include "Settings/DebugSetting.h"
#include "core/SelectionManager.h"
#include "renderer/GizmoRenderPass.h"
#include "Rendering/Features/FrameInfoRenderFeature.h"
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <cstdlib>

namespace MOON {
	struct OpenGLProcAddressHelper {
		inline static QOpenGLContext* ctx;
		static void* getProcAddress(const char* name) {
			return (void*)ctx->getProcAddress(name);
		}
	};

	class ViewerWidget::ViewerWindowInternal {
	public:
		ViewerWindowInternal(ViewerWidget* view) :mSelf(view) {

		}
		/** The editor's rendering context: the driver, the shared managers, the default scene.
		 * Created on the widget's context as soon as one exists, because everything in the editor
		 * asks for these services through the service locator - a panel built before them talks
		 * to an empty service and dies.
		 *
		 * The view itself is not created here: it owns framebuffers, and those must be created on
		 * the context that will render with them (see initializeView). */
		void initializeEditorContext() {
			auto& tree = GetService(TreeViewPanel);
			QObject::connect(&tree, &TreeViewPanel::setSelectActor, mSelf, &onActorSelected);
			QObject::connect(&tree, &TreeViewPanel::itemHovered, mSelf, &onActorHovered);

			QObject::connect(&tree, &TreeViewPanel::itemLeave, mSelf, &onActorHoverLeaved);
			mEditorContext = new Editor::Core::Context("", "");
			mEditorContext->sceneManager.LoadDefaultScene();
			// The pending file is NOT read here. It used to be, and that made a file opened at
			// start-up (start page, command line, "open with") be read twice: the deferred read
			// in paintGL - which waits for the viewport, see mDoReadFile - already does the work.
			// Reading it twice put two identical bodies in the scene (every surface count
			// doubled, which is exactly what kills the stencil-parity section cap), and reading
			// it *here* is even worse: this runs before initializeView(), so a STEP import would
			// go through TopoActor::TopoActor -> GetSceneView, which is not a service yet.
		}
		/** The view, and with it the renderer and every render target of the frame. Runs with the
		 * render module's own context current: framebuffers and vertex arrays belong to the
		 * context that created them. The render pass settings panel reads the view's passes, so it
		 * is refreshed here, once there is a view to read. */
		void initializeView() {
			mSceneView = new Editor::Panels::SceneView("SceneView");
			GetService(RenderPassSettingWidget).Refresh();
		}
		~ViewerWindowInternal() {
			delete mEditorContext;
			delete mSceneView;
		}
		void debugImgui() {
			bool value=MOON::DebugSettings::instance().getNode("DebugImgui")->getData<bool>();
			if (value) {
				ImVec2 a = { 0,1 }, b = { 1,0 };
				ImVec2 size = ImVec2(mViewWidth, mViewHeight);
				auto& gbufferData = mSceneView->GetRenderer().GetPass<::Core::Rendering::GbufferPass>("Gbuffer").GetGbufferData();
				ImGui::Image(gbufferData.position->GetID(), size, a, b);
				ImGui::Image(gbufferData.normal->GetID(), size, a, b);
				ImGui::Image(gbufferData.occlusion->GetID(), size, a, b);
				ImGui::Image(gbufferData.occlusionBlur->GetID(), size, a, b);
			}
		}

		void drawFpsOverlay() {
			if (!MOON::DebugSettings::instance().getOrDefault<bool>("showFPS", false)) {
				return;
			}
			const auto& frameInfo =
				mSceneView->GetRenderer().GetFeature<Rendering::Features::FrameInfoRenderFeature>().GetFrameInfo();
			const auto& hzb = mSceneView->GetRenderer().GetHzbStats();
			const auto& hzbPass
				= mSceneView->GetRenderer().GetPass<Core::Rendering::HzbBuildPass>("HZB");
			char text[1536];
			sprintf_s(text, sizeof(text),
				"FPS %.1f\n"
				"Frame %.2f ms\n"
				"Total vertex Count %llu\n"
				"Batch Triangle Count %llu\n"
				"Triangle Count %llu\n"
				"Triangle Vertex Count %llu\n"
				"Triangle instance Count %llu\n"
				"Batch line Count %llu\n"
				"line Count %llu\n"
				"line Vertex Count %llu\n"
				"line instance Count %llu\n"
				"[HZB] grid %ux%u depth[min %.6f max %.6f mean %.6f]\n"
				"[HZB] bvh instances %u | visited %u | culled nodes %u | occluded meshes %u\n"
				"[HZB] tests: occluded %u | bias rejected %u | bg rejected %u | best margin %.6f | bias %.6f\n"
				"[HZB] skipped drawables %u | cull %.3f ms\n"
				"[HZB] readback slots %u (pending %u) | skipped frames %u | latency %u frames\n",
				m_fps, m_frameMs,
				(unsigned long long)frameInfo.vertexCount,
				(unsigned long long)frameInfo.batchPolyCount,
				(unsigned long long)frameInfo.polyCount,
				(unsigned long long)frameInfo.vertexPolyCount,
				(unsigned long long)frameInfo.instancePolyCount,
				(unsigned long long)frameInfo.batchLineCount,
				(unsigned long long)frameInfo.lineCount,
				(unsigned long long)frameInfo.vertexLineCount,
				(unsigned long long)frameInfo.instancelineCount,
				hzb.gridWidth, hzb.gridHeight,
				hzb.gridMinDepth, hzb.gridMaxDepth, hzb.gridMeanDepth,
				hzb.bvhInstances, hzb.visitedNodes, hzb.culledNodes, hzb.occludedInstances,
				hzb.occludedNodeTests, hzb.biasRejectedNodes, hzb.backgroundRejectedNodes,
				hzb.bestMargin, mSceneView->GetRenderer().GetHzbCuller().GetDepthBias(),
				mSceneView->GetRenderer().GetHzbSkippedDrawables(),
				hzb.cullTimeMs,
				hzbPass.GetReadbackSlotCount(),
				hzbPass.GetReadbackPendingSlots(),
				hzbPass.GetReadbackSkippedFrames(),
				hzbPass.GetLastReadbackLatencyFrames()
			);
			ImGui::GetForegroundDrawList()->AddText({20,20}, IM_COL32(255, 255, 100, 255), text);
		}

		void paintGL() {
			// The render module is built one event-loop turn after initializeGL (its context has
			// to exist first, see ViewerWidget::CreateRenderContext), so the first couple of
			// paints can land before there is a view to draw with.
			if (mSceneView == nullptr) {
				return;
			}
			// The view is created after the widget has already been laid out, and the layout can
			// also resize it while a frame is in flight. Rather than trusting the resize events
			// to have reached it, the frame brings the view to the widget's current size first:
			// everything below - the document read, its camera fit, the paste - depends on it.
			{
				const auto [viewWidth, viewHeight] = mSceneView->GetSafeSize();
				if (viewWidth != mSelf->width() || viewHeight != mSelf->height()) {
					mViewWidth = mSelf->width();
					mViewHeight = mSelf->height();
					mSceneView->Resize(mViewWidth, mViewHeight);
				}
			}
			// From here to the paste below this frame belongs to the render module, and the
			// module's context is the one it draws on: its render targets and vertex arrays live
			// there, not in this widget's context.
			auto& editorContext = GetService(Editor::Core::Context);
			auto* moduleHost = editorContext.driver != nullptr ? editorContext.driver->GetContextHost() : nullptr;
			if (moduleHost != nullptr) {
				moduleHost->MakeCurrent();
			}
			// Raw per-frame FPS / frame time, measured between consecutive
			// paint calls and shown without any smoothing.
			if (!m_fpsTimer.isValid()) {
				m_fpsTimer.start();
				m_fps = 0.0;
				m_frameMs = 0.0;
			}
			else {
				const double frameMs = m_fpsTimer.restart();
				if (frameMs > 0.0) {
					m_fps = 1000.0 / frameMs;
					m_frameMs = frameMs;
				}
			}
			// ImGui's and the 2D draw list's frames are opened here, on the module's context:
			// Im2DRender::newFrame creates its shader and vertex array on first use, and those
			// belong to the context that draws with them.
			ImRenderer::instance().newImgui();
			Rendering::Overlay::Im2DRender::Instance().NewFrame();
			// The frames that follow a document read are traced step by step: they
			// build the scene query, frame the view, refresh the tree and draw, and a
			// crash in one of those steps is otherwise impossible to place. Nothing is
			// logged once the read is done.
			bool openFrame = false;
			if (mSceneView->GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").IsEnabled()) {
				ImRenderer::instance().newFrame(mSceneView);
			}
			mSceneView->Update(0.01);
			// The read waits for the viewport to be up: the start page is the page that
			// is shown first, and the viewport behind it is only initialized the moment
			// it comes to the front.
			if (mDoReadFile && mSceneView != nullptr) {
				mDoReadFile = false;
				const std::string path = mReadFilePath.toUtf8().toStdString();
				// A .moon file is a feature chain, not a mesh: it is read by rebuilding
				// the body instead of by the scene parser. Everything here runs in the
				// render loop because building features touches the scene and the
				// viewer.
				bool read = false;
				if (mReadFilePath.endsWith(QString(".") + MoonDocument::extension(), Qt::CaseInsensitive)) {
					read = MoonDocument::open(path);
					mRefreshTreeView = read;
				}
				else {
					parser->ParseFile(path);
					read = true;
				}
				if (read) {
					// The document is up: whoever keeps the recent projects hears about
					// it here, where the read really happened.
					emit mSelf->documentOpened(mReadFilePath);
				}
				openFrame = true;
				mSceneView->UnselectActor();
				// A file that was just read - an imported model or a rebuilt feature
				// chain - arrives without scene query structures and with the camera
				// still on the previous one, so it has to be built and framed. That
				// cannot be done here unconditionally: a STEP import builds its shape
				// on a job thread, so its actors can already be in the scene while
				// the models they will hold are still empty, and framing those would
				// frame the previous file. The request is left pending for the frames
				// below, which fit as soon as there is something to frame.
				mPendingViewFit = true;
				mPendingViewFitFrames = kViewFitTimeoutFrames;
			}

			if (mPendingViewFit)
			{
				if (hasModelToFrame())
				{
					// The triangles of the read come first: a scene query build that
					// runs before them comes back empty, and an empty scene BVH is
					// worse than none - the camera controller reads the scene bounds
					// to scale its zoom and pan, and the default (empty) box gives it
					// infinities to scale by. Once there is something to index - or
					// once the wait has run out, so a triangle-free document is still
					// framed - the build and the fit happen in the same frame, still
					// inside the render loop, which is what BuildBvh needs: it copies
					// the mesh domain palettes through the GL context.
					if (hasTrianglesToQuery() || mPendingViewFitFrames <= 1)
					{
						mPendingViewFit = false;
						openFrame = true;
						CORE_INFO("[Viewer] the document was read; building the scene query");
						mSceneView->BuildBvh();
						CORE_INFO("[Viewer] scene query built; framing the view");
						mSceneView->FitToFocus(mSceneView->GetCamera()->GetTransform().GetWorldForward());
						CORE_INFO("[Viewer] view framed");
						applyClipDemo();
					}
					else
					{
						--mPendingViewFitFrames;
					}
				}
				else if (--mPendingViewFitFrames <= 0)
				{
					// Nothing frameable ever came out of the read (an unreadable
					// file): stop waiting, so an unrelated later actor cannot
					// trigger this fit after the fact.
					mPendingViewFit = false;
				}
			}
				
			if (mAddActors.size() > 0|| mRemoveActors.size() > 0||mModifyActors.size()>0) {
				std::vector<TreeViewPanel::Operation> operations;
				operations.push_back(TreeViewPanel::Operation( TreeViewPanel::OperationType::Add,mAddActors ));
				operations.push_back(TreeViewPanel::Operation(TreeViewPanel::OperationType::Remove, mRemoveActors));
				operations.push_back(TreeViewPanel::Operation(TreeViewPanel::OperationType::Update, mModifyActors));
				GetTreeView.updateActorInTree(operations);
				mAddActors.clear();
				mRemoveActors.clear();
				mModifyActors.clear();
			}
			else if (mRefreshTreeView) {
				mRefreshTreeView = false;
				// TopoShape discretization may rebuild the topology actors
				// (Solid/Shell/Face_*/Edge_*), so refresh the tree to reflect
				// the new hierarchy. Runs after the queued adds so a freshly
				// loaded topo actor is not added twice.
				openFrame = true;
				CORE_INFO("[Viewer] refreshing the tree view of the read document");
				GetTreeView.updateTreeViewSceneRoot();
				CORE_INFO("[Viewer] tree view refreshed");
			}

			// The first frames of a read are traced as well: a crash a few frames later
			// says the scene or the topology actors are not ready for what was read, and
			// the counter shows which frame it happened on.
			const bool traceFrame = openFrame
				|| (mPendingViewFit && mPendingViewFitFrames > kViewFitTimeoutFrames - 8);
			if (traceFrame) {
				CORE_INFO(
					"[Viewer] drawing a frame ({0} fit frame(s) left)",
					mPendingViewFit ? mPendingViewFitFrames : 0);
			}
			// The overlays are drawn *inside* the frame, into the module's present target: ImGui,
			// the 2D draw list and the gizmos are part of what the window pastes, not something
			// the window draws afterwards on its own context. That is what leaves the module
			// free of the window layer - it draws its overlays wherever it runs.
			mSceneView->Render([this] {
				debugImgui();
				drawFpsOverlay();
				const auto [overlayWidth, overlayHeight] = mSceneView->GetSafeSize();
				Rendering::Overlay::Im2DRender::Instance().EndFrame(*mEditorContext->driver,
					static_cast<uint16_t>(overlayWidth), static_cast<uint16_t>(overlayHeight));
				ImRenderer::instance().endImgui();
			});
			if (traceFrame) {
				CORE_INFO("[Viewer] frame drawn");
			}
			if (moduleHost != nullptr) {
				moduleHost->DoneCurrent();
			}
			// The window layer's only job on the frame: paste it into its own framebuffer. A blit
			// of the module's colour texture, because drawing it would need the module's quad VAO
			// and vertex arrays are per context.
			mSelf->makeCurrent();
			mSelf->glBindFramebuffer(GL_FRAMEBUFFER, mSelf->defaultFramebufferObject());
			const auto [frameWidth, frameHeight] = mSceneView->GetSafeSize();
			mSelf->PasteFrame(mSceneView->GetFrameTextureID(), frameWidth, frameHeight);
			mSceneView->getInutState().ClearEvents();
		}
		/** Translates a Qt event into the render module's own input event.
		 *
		 * This is the whole of the toolkit's involvement in input: the view, the camera
		 * controller and the input state only ever see keys, buttons, positions and wheel
		 * deltas (see InputEvent). Put another window framework under the module and only
		 * this function changes. */
		static bool TranslateQtEvent(QEvent* p_event, ::Editor::Panels::InputEvent& p_out)
		{
			using ::Editor::Panels::InputEvent;
			// The engine's own key/button enums live in that namespace; the case labels below are
			// its values, not Qt's.
			using namespace ::Editor::Panels;
			switch (p_event->type())
			{
			case QEvent::KeyPress:
			case QEvent::KeyRelease:
			{
				auto* keyEvent = static_cast<QKeyEvent*>(p_event);
				p_out.type = p_event->type() == QEvent::KeyPress ? InputEvent::EType::KeyPress : InputEvent::EType::KeyRelease;
				p_out.autoRepeat = keyEvent->isAutoRepeat();
				switch (static_cast<Qt::Key>(keyEvent->key()))
				{
				case Qt::Key_W: p_out.key = KEYW; break;
				case Qt::Key_A: p_out.key = KEYA; break;
				case Qt::Key_S: p_out.key = KEYS; break;
				case Qt::Key_D: p_out.key = KEYD; break;
				case Qt::Key_Q: p_out.key = KEYQ; break;
				case Qt::Key_E: p_out.key = KEYE; break;
				case Qt::Key_R: p_out.key = KEYR; break;
				case Qt::Key_L: p_out.key = KEYL; break;
				case Qt::Key_T: p_out.key = KEYT; break;
				case Qt::Key_F: p_out.key = KEYF; break;
				case Qt::Key_Alt: p_out.key = ALTA; break;
				case Qt::Key_Right: p_out.key = RIGHT; break;
				case Qt::Key_Left: p_out.key = LEFT; break;
				case Qt::Key_Up: p_out.key = UP; break;
				case Qt::Key_Down: p_out.key = DOWN; break;
				case Qt::Key_PageUp: p_out.key = PageUp; break;
				case Qt::Key_PageDown: p_out.key = PageDown; break;
				case Qt::Key_Control: p_out.key = Control; break;
				case Qt::Key_Shift: p_out.key = Shift; break;
				default: return false;
				}
				return true;
			}
			case QEvent::MouseMove:
			{
				auto* mouseEvent = static_cast<QMouseEvent*>(p_event);
				p_out.type = InputEvent::EType::MouseMove;
				p_out.x = mouseEvent->x();
				p_out.y = mouseEvent->y();
				return true;
			}
			case QEvent::MouseButtonPress:
			case QEvent::MouseButtonRelease:
			{
				auto* mouseEvent = static_cast<QMouseEvent*>(p_event);
				p_out.type = p_event->type() == QEvent::MouseButtonPress
					? InputEvent::EType::MouseButtonPress : InputEvent::EType::MouseButtonRelease;
				switch (mouseEvent->button())
				{
				case Qt::LeftButton: p_out.button = MOUSE_BUTTON_LEFT; break;
				case Qt::RightButton: p_out.button = MOUSE_BUTTON_RIGHT; break;
				case Qt::MiddleButton: p_out.button = MOUSE_BUTTON_MIDDLE; break;
				default: return false;
				}
				p_out.x = mouseEvent->x();
				p_out.y = mouseEvent->y();
				return true;
			}
			case QEvent::Wheel:
			{
				auto* wheelEvent = static_cast<QWheelEvent*>(p_event);
				p_out.type = InputEvent::EType::Wheel;
				p_out.wheelDelta = wheelEvent->angleDelta().y();
				return true;
			}
			default:
				return false;
			}
		}

		bool event(QEvent* evt)
		{
			if (mSceneView != nullptr)
			{
				::Editor::Panels::InputEvent inputEvent;
				if (TranslateQtEvent(evt, inputEvent))
				{
					mSceneView->ReceiveEvent(inputEvent);
				}
			}
			// OCCT's interactor is UI glue and keeps taking the Qt event as is.
			RenderWindowInteractor::Instance()->ReceiveEvent(evt);
			return true;
		}
		void resizeEvent(QResizeEvent* event)
		{
			mViewWidth = event->size().width();
			mViewHeight = event->size().height();
			if (mSceneView != nullptr)
				mSceneView->Resize(mViewWidth, mViewHeight);
			RenderWindowInteractor::Instance()->UpdateSize(mViewWidth,mViewHeight);
		}
		/** The view is created one event-loop turn after the widget is first shown, so it misses
		 * the resize events that happened before it existed: give it the size the widget has
		 * now. Without this the view renders at its 1x1 construction size and the paste shows
		 * nothing. */
		void applyViewSize()
		{
			if (mSceneView != nullptr && mViewWidth > 1 && mViewHeight > 1)
				mSceneView->Resize(mViewWidth, mViewHeight);
		}
		/** TEMPORARY PROBE for the "imported STEP has no section" investigation: with
		 * MOON_CLIP_DEMO=<ratio> set, cut the framed document with a horizontal plane
		 * `<ratio>` of its height above the centre, so a headless run (Moon.exe file.step
		 * plus MOON_SHOT) renders the section without anyone dragging the clip widget.
		 * Remove together with the MOON_SECTION_DEBUG switch in SceneRenderer.cpp. */
		void applyClipDemo()
		{
			if (mClipDemoApplied) return;
			const char* p_ratio = std::getenv("MOON_CLIP_DEMO");
			if (p_ratio == nullptr || *p_ratio == '\0') return;
			auto* scene = mSceneView != nullptr ? mSceneView->GetScene() : nullptr;
			if (scene == nullptr) return;
			const auto box = scene->GetSceneBoundingBox();
			if (!box.isValid()) return;

			const float ratio = static_cast<float>(std::atof(p_ratio));
			const Maths::FVector3 center = box.center();
			const float height = box.lengthZ();
			const Maths::FVector3 normal{ 0.0f, 0.0f, 1.0f };
			const float d = center.z + ratio * height * 0.5f;

			auto& feature = mSceneView->GetRenderer()
				.GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
			feature.SetClipPlane(normal.x, normal.y, normal.z, -d);
			feature.EnableClip(true);
			mClipDemoApplied = true;
			CORE_INFO("[Section] clip demo plane z = {0} (ratio {1}, bbox height {2})", d, ratio, height);
		}
		void onReadFile(const QString& path)
		{
			// The request is recorded first, then the workspace is brought up: raising the
			// workspace is what makes the viewport paint, and a paint that lands while that is
			// happening must already see the request.
			mReadFilePath = path;
			mDoReadFile = true;
			GetService(::MOON::HomePanel).ShowWorkspace();
		}
		/** True when some model in the scene has bounds to frame. A read can put
		 * its actors in the scene before the shapes they will hold exist (a STEP
		 * import builds them on a job thread), and framing those would frame the
		 * previous file instead of the one that was just read. */
		bool hasModelToFrame()
		{
			auto* scene = mSceneView->GetScene();
			if (scene == nullptr)
				return false;
			for (auto* modelRenderer : scene->GetFastAccessComponents().modelRenderers)
			{
				if (modelRenderer == nullptr || !modelRenderer->owner.IsActive())
					continue;
				auto* model = modelRenderer->GetModel();
				if (model != nullptr && model->GetBoundingSphere().radius > 0.0f)
					return true;
			}
			return false;
		}
		/** True when some model in the scene holds a triangle mesh with bounds, i.e.
		 * when a scene query build would have something to index.
		 *
		 * A build that runs before the read's topology meshes exist comes out empty,
		 * and an empty scene BVH keeps the default (empty) bounding box - whose
		 * extents are infinite and which every reader of the scene bounds has to
		 * defend against. Waiting for one triangle means the build is worth doing and
		 * the fit frames the geometry that was read. */
		bool hasTrianglesToQuery()
		{
			auto* scene = mSceneView->GetScene();
			if (scene == nullptr)
				return false;
			for (auto* modelRenderer : scene->GetFastAccessComponents().modelRenderers)
			{
				if (modelRenderer == nullptr || !modelRenderer->owner.IsActive())
					continue;
				auto* model = modelRenderer->GetModel();
				if (model == nullptr)
					continue;
				for (auto* mesh : model->GetMeshes())
				{
					if (mesh == nullptr)
						continue;
					if (mesh->GetPrimitiveMode() != ::Rendering::Settings::EPrimitiveMode::TRIANGLES)
						continue;
					if (mesh->GetBoundingBox().isValid())
						return true;
				}
			}
			return false;
		}
	private:
		friend ViewerWidget;
		ViewerWidget* mSelf = nullptr;
		Editor::Core::Context* mEditorContext = nullptr;
		Editor::Panels::SceneView* mSceneView = nullptr;
		std::vector<Core::ECS::Actor*>mAddActors;
		std::vector<Core::ECS::Actor*>mRemoveActors;
		std::vector<Core::ECS::Actor*>mModifyActors;
		ParseScene* parser = nullptr;
		int mViewWidth = 1;
		int mViewHeight = 1;
		bool mInitFlag = false;
		
		bool mRefreshTreeView = false;
		QString mReadFilePath = "";
		bool mDoReadFile = false;
		// A read is framed as soon as the scene has a model to frame - see
		// hasModelToFrame - and gives up after this many frames, so an unreadable
		// file cannot leave a fit waiting to fire when a later feature adds one.
		static constexpr int kViewFitTimeoutFrames = 600;
		bool mPendingViewFit = false;
		int mPendingViewFitFrames = 0;
		/** TEMPORARY: set once applyClipDemo() cut the document (MOON_CLIP_DEMO). */
		bool mClipDemoApplied = false;
		QElapsedTimer m_fpsTimer;
		double m_fps = 0.0;
		double m_frameMs = 0.0;


	};
	ViewerWidget::ViewerWidget(QWidget* parent) :
		QOpenGLWidget(parent), mInternal(new ViewerWindowInternal(this))
	{
		// default to strong focus
		this->setFocusPolicy(Qt::StrongFocus);
		//this->setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
		this->setMouseTracking(true);
		QSurfaceFormat format;
		// Single sample: the frame is pasted into this widget's framebuffer with a blit, and
		// blitting into a multisampled framebuffer (even with one sample) is invalid. The render
		// module does its own MSAA on its side (AView::m_msaaframebuffer).
		format.setSamples(0);
		this->setFormat(format);
		RegService(ViewerWidget, *this);
	}

	ViewerWidget::~ViewerWidget()
	{
		delete mInternal;
		// After the driver (owned by the context mInternal holds) is gone, the module context
		// itself can go.
		mRenderContextHost.reset();
	}

	// The render module gets a context of its own, in the same share group as this widget (and,
	// with Qt::AA_ShareOpenGLContexts, the material preview's): the module renders there, the
	// widgets only paste the result. Created from the event loop rather than from initializeGL
	// because wglShareLists refuses to share from a context that is current, and this widget's
	// context is current inside initializeGL.
	void ViewerWidget::CreateRenderContext()
	{
		if (mRenderContextHost != nullptr && mRenderContextHost->IsValid()) {
			return;
		}

		// The editor's rendering context (driver, managers, default scene) and the view with its
		// render targets are all built here, on the module's own context: framebuffers and vertex
		// arrays are per context, so they have to be created on the one that renders with them.
		//
		// The whole module is built in this one call, before anything else runs: a service that is
		// missing when a panel or a timer asks for it throws, and in a release build that is an
		// uncaught exception that takes the process down without a word.
		// The editor sets Qt::AA_ShareOpenGLContexts, so every QOpenGLWidget lives in Qt's global
		// share group and the module joins the root of that group: the material panel's preview
		// widget pastes a frame the module rendered, and a texture name only means something in
		// the group that created it. Without a global share context (an application that did not
		// set the attribute) the module shares this widget's context, as before.
		QOpenGLContext* shareContext = QOpenGLContext::globalShareContext();
		if (shareContext == nullptr) {
			shareContext = context();
		}
		mRenderContextHost = std::make_unique<Editor::Core::QtGLContextHost>(shareContext);
		const bool moduleReady = mRenderContextHost->IsValid() && mRenderContextHost->MakeCurrent();

		if (moduleReady) {
			mInternal->initializeEditorContext();
			mInternal->initializeView();
			mInternal->applyViewSize();
			// The overlays belong to the module's frame, so their GL objects belong to its context
			// too: the gizmo geometry (vertex array, vertex buffer, materials) and ImGui's font
			// texture and vertex array. Vertex arrays and textures are per context - a name from
			// the other one is not the object at all, which is how the ViewCube and the rotation
			// centre silently stopped drawing when only half of this was on the right context.
			ImRenderer::instance().initGizmoGl();
			ImRenderer::instance().init();

			if (auto* driver = ::Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get(); driver != nullptr) {
				driver->SetContextHost(mRenderContextHost.get());
				// The global GL state the backend sets up is per context, and the module's context
				// is new: reversed-Z depth range, blend func, seamless cubemap, debug output.
				driver->ReapplyGlobalState();
			}

			mRenderContextHost->DoneCurrent();
		}
		else {
			// No module context (sharing refused, or it could not be made current): build the
			// module on the widget's context, which is how it worked before the split. Nothing is
			// lost visually - only the separation of the two contexts is.
			CORE_WARN("[GL] the render module has no context of its own; rendering on the widget's");
			mRenderContextHost.reset();
			this->makeCurrent();
			mInternal->initializeEditorContext();
			mInternal->initializeView();
			ImRenderer::instance().initGizmoGl();
			ImRenderer::instance().init();
		}

		// Leave the widget's context current: this is the caller's state (initializeGL).
		this->makeCurrent();
	}

	void ViewerWidget::initializeGL()
	{
		QOpenGLWidget::initializeGL();
		// opengl funcs
		bool flag = initializeOpenGLFunctions();
		OpenGLProcAddressHelper::ctx = context();
		GlLoader::CustomLoadGL(OpenGLProcAddressHelper::getProcAddress);
		this->startTimer(0);
		// Everything the editor needs to draw is built right here, in one go: the widget's context
		// is released, the module's own (shared) context is created and made current, the driver,
		// the managers, the view and its render targets are created on it, and the widget's context
		// comes back. Doing it in one call is not a style choice: a service that is missing when a
		// timer or a panel asks for it throws, and in a release build that is an uncaught exception
		// that takes the process down without a word.
		this->doneCurrent();
		CreateRenderContext();
		this->makeCurrent();
	}

	void ViewerWidget::timerEvent(QTimerEvent* e)
	{
		this->update();
	}

	void ViewerWidget::paintGL()
	{
		CallBackManager::instance().exectue();
		mInternal->paintGL();
	}

	bool ViewerWidget::event(QEvent* evt)
	{
		mInternal->event(evt);
		return QOpenGLWidget::event(evt);
	}

	void ViewerWidget::leaveEvent(QEvent* event)
	{
	}

	void ViewerWidget::resizeEvent(QResizeEvent* event)
	{
		QOpenGLWidget::resizeEvent(event);
		mInternal->resizeEvent(event);
	}

	void ViewerWidget::mousePressEvent(QMouseEvent* e)
	{
		
	}

	void ViewerWidget::mouseMoveEvent(QMouseEvent* event)
	{
	}

	void ViewerWidget::mouseReleaseEvent(QMouseEvent* event)
	{
	}

	void ViewerWidget::wheelEvent(QWheelEvent* event)
	{
	}
	void ViewerWidget::keyPressEvent(QKeyEvent* event)
	{
	}
	void ViewerWidget::keyReleaseEvent(QKeyEvent* event)
	{
	}

	::Editor::Panels::AView* ViewerWidget::getView()
	{
		return mInternal->mSceneView;
	}

	QString ViewerWidget::getDocumentPath() const
	{
		return mInternal->mReadFilePath;
	}

	void ViewerWidget::clearDocumentPath()
	{
		// What was read is what is open, so closing the document is what makes this stale:
		// it is how the picture of the project is kept from being taken again for a
		// document that is no longer there.
		mInternal->mReadFilePath.clear();
	}

	void ViewerWidget::addActorToTreeView(Core::ECS::Actor* actor)
	{
		mInternal->mAddActors.push_back(actor);
	}

	void ViewerWidget::removeActorFromTreeView(Core::ECS::Actor* actor)
	{
		mInternal->mRemoveActors.push_back(actor);
	}

	void ViewerWidget::modifyActorInTreeView(Core::ECS::Actor* actor)
	{
		mInternal->mModifyActors.push_back(actor);
	}

	void ViewerWidget::onActorHovered(Core::ECS::Actor* actor)
	{
		if (actor != nullptr) {
			GetSelection.setPreselect(actor->GetID());
		}
	}

	void ViewerWidget::onActorHoverLeaved(Core::ECS::Actor* actor)
	{
		if (actor != nullptr) {
			GetSelection.clearPreselect();
		}
	}

	void ViewerWidget::onReadFile(const QString& path)
	{
		mInternal->onReadFile(path);
	}
	void ViewerWidget::refreshTreeView()
	{
		mInternal->mRefreshTreeView = true;
	}

	void ViewerWidget::PasteFrame(uint32_t p_textureID, int p_width, int p_height)
	{
		if (p_textureID == 0 || p_width <= 0 || p_height <= 0) {
			return;
		}

		// A glBlitFramebuffer needs the source attached to a framebuffer of *this* context: the
		// texture belongs to the module's context but textures are shared, while framebuffer
		// objects are not, so the read framebuffer is made here once and reused.
		if (m_pasteFramebuffer == 0) {
			glGenFramebuffers(1, &m_pasteFramebuffer);
		}

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pasteFramebuffer);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p_textureID, 0);

		const int targetWidth = static_cast<int>(width() * devicePixelRatioF());
		const int targetHeight = static_cast<int>(height() * devicePixelRatioF());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
		glBlitFramebuffer(0, 0, p_width, p_height, 0, 0, targetWidth, targetHeight,
			GL_COLOR_BUFFER_BIT, GL_LINEAR);

		glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pasteFramebuffer);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
	}

	void ViewerWidget::onActorSelected(::Core::ECS::Actor* actor) {
		if (actor != nullptr) {
			mInternal->mSceneView->SelectActor(*actor);
			GetSelection.select({ actor->GetID() });
		}
	}
}