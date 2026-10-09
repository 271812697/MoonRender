#include <QMouseEvent>
#include "viewerwidget.h"
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
#include "feature/MoonDocument.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "editor/UI/SettingPanel/PassSettingWidget.h"

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "core/log.h"
#include "Core/Rendering/GbufferPass.h"
#include "Core/Rendering/HzbBuildPass.h"
#include "Qtimgui/imgui/imgui.h"
#include "Interactive/Im2DRenderer.h"
#include "Settings/DebugSetting.h"
#include "core/SelectionManager.h"
#include "renderer/GizmoRenderPass.h"
#include "Rendering/Features/FrameInfoRenderFeature.h"

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
		void initializeGL() {
			auto& tree = GetService(TreeViewPanel);
			QObject::connect(&tree, &TreeViewPanel::setSelectActor, mSelf, &onActorSelected);
			QObject::connect(&tree, &TreeViewPanel::itemHovered, mSelf, &onActorHovered);

			QObject::connect(&tree, &TreeViewPanel::itemLeave, mSelf, &onActorHoverLeaved);
			mEditorContext = new Editor::Core::Context("", "");
			mEditorContext->sceneManager.LoadDefaultScene();
			
			mSceneView = new Editor::Panels::SceneView("SceneView");
			GetService(RenderPassSettingWidget).Refresh();
			parser->ParseFile(mReadFilePath.toStdString());
			ImRenderer::instance().init();

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
			ImRenderer::instance().newImgui();
			Render2D::Im2DRender::instance().newFrame();
			// The frames that follow a document read are traced step by step: they
			// build the scene query, frame the view, refresh the tree and draw, and a
			// crash in one of those steps is otherwise impossible to place. Nothing is
			// logged once the read is done.
			bool openFrame = false;
			if (mSceneView->GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").IsEnabled()) {
				ImRenderer::instance().newFrame(mSceneView);
			}
			mSceneView->Update(0.01);
			if (mDoReadFile) {
				mDoReadFile = false;
				const std::string path = mReadFilePath.toUtf8().toStdString();
				// A .moon file is a feature chain, not a mesh: it is read by rebuilding
				// the body instead of by the scene parser. Everything here runs in the
				// render loop because building features touches the scene and the
				// viewer.
				if (mReadFilePath.endsWith(QString(".") + MoonDocument::extension(), Qt::CaseInsensitive)) {
					if (MoonDocument::open(path)) {
						mRefreshTreeView = true;
					}
				}
				else {
					parser->ParseFile(path);
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
			mSceneView->Render();
			if (traceFrame) {
				CORE_INFO("[Viewer] frame drawn");
			}
			mSelf->glBindFramebuffer(GL_FRAMEBUFFER, mSelf->defaultFramebufferObject());
			mSceneView->Present();
			debugImgui();
			drawFpsOverlay();
			Render2D::Im2DRender::instance().endFrame();
			ImRenderer::instance().endImgui();
			mSceneView->getInutState().ClearEvents();
		}
		bool event(QEvent* evt)
		{
			if (mSceneView != nullptr)
				mSceneView->ReceiveEvent(evt);
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
		void onReadFile(const QString& path)
		{
			mReadFilePath = path;
			mDoReadFile = true;
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
		int mViewWidth;
		int mViewHeight;
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
		QElapsedTimer m_fpsTimer;
		double m_fps = 0.0;
		double m_frameMs = 0.0;

	};
	ViewerWidget::ViewerWidget(QWidget* parent) :
		QOpenGLWidget(parent), mInternal(new ViewerWindowInternal(this))
	{
		//设置可以捕获鼠标移动消息
		// default to strong focus
		this->setFocusPolicy(Qt::StrongFocus);
		//this->setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
		this->setMouseTracking(true);
		QSurfaceFormat format;
		format.setSamples(1);
		this->setFormat(format);
		RegService(ViewerWidget, *this);
	}

	ViewerWidget::~ViewerWidget()
	{
		delete mInternal;
	}

	void ViewerWidget::initializeGL()
	{
		QOpenGLWidget::initializeGL();
		// opengl funcs
		bool flag = initializeOpenGLFunctions();
		OpenGLProcAddressHelper::ctx = context();
		GlLoader::CustomLoadGL(OpenGLProcAddressHelper::getProcAddress);
		//开启计时器
		this->startTimer(0);
		mInternal->initializeGL();
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
	void ViewerWidget::onActorSelected(::Core::ECS::Actor* actor) {
		if (actor != nullptr) {
			
			mInternal->mSceneView->SelectActor(*actor);
			GetSelection.select({ actor->GetID() });
		}
	}
}
