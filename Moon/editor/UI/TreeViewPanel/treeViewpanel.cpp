#pragma once
#include "treeViewpanel.h"
#include "editor/UI/TreeViewPanel/EntityTreeModel.h"
#include "editor/UI/TreeViewPanel/EntityTreeStyle.h"
#include "editor/UI/PropertyPanel/PropertyWidget.h"
#include "Core/Global/ServiceLocator.h"
#include "Core/SceneSystem/SceneManager.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
#include "renderer/Context.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "feature/SketcherFeature.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "core/SelectionManager.h"
#include "core/JobSystem.h"
#include "renderer/SceneView.h"
#include <Core/SceneSystem/Scene.h>
#include <Core/ECS/Components/CLight.h>
#include <Core/ECS/Components/CCamera.h>
#include <Core/ECS/Components/CPostProcessStack.h>
#include <Core/ECS/Components/CReflectionProbe.h>
#include <QFileSystemModel>
#include <QAbstractItemModel>
#include <QHeaderView>
#include <QMouseEvent>
#include <QMenu>
#include <algorithm>
#include <string>
#include <vector>

namespace MOON {
	static bool isEntityCheckAble(const std::string& name) {
		if (name == "HeadLight" || name == "PointLight1" || name == "PointLight2" || name == "PointLight3" || name == "PointLight4") {
			return false;
		}
		return true;
	}
	class HighlightDelegate : public QStyledItemDelegate
	{
	public:
		explicit HighlightDelegate(TreeViewPanel* panel, QObject* parent = nullptr)
			: QStyledItemDelegate(parent), m_panel(panel) {
		}

		void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
		{
			QStyleOptionViewItem opt = option;

			if (index == m_panel->m_highlightIndex) {
				opt.state |= QStyle::State_MouseOver;
			}

			QStyledItemDelegate::paint(painter, opt, index);
		}

	private:
		TreeViewPanel* m_panel;
	};
	class TreeViewPanel::TreeViewPanelInternal {
	public:
		TreeViewPanelInternal(TreeViewPanel* tree) :mSelf(tree) {
			mModel = new EntityTreeModel(mSelf);
		}
		~TreeViewPanelInternal() {
		}
	private:
		friend TreeViewPanel;
		EntityTreeModel* mModel = nullptr;
		TreeViewPanel* mSelf = nullptr;
		QModelIndex m_lastIndex;  // 记录上一次悬浮项
	};
	TreeViewPanel::TreeViewPanel(QWidget* parent) :QTreeView(parent), mInternal(new TreeViewPanelInternal(this))
	{
		RegService(TreeViewPanel, *this);
		QSizePolicy sizePolicy8(QSizePolicy::Preferred, QSizePolicy::Expanding);
		sizePolicy8.setHorizontalStretch(0);
		sizePolicy8.setVerticalStretch(0);
		sizePolicy8.setWidthForHeight(true);
		sizePolicy8.setHeightForWidth(this->sizePolicy().hasHeightForWidth());
		this->setSizePolicy(sizePolicy8);
		this->setModel(mInternal->mModel);
		//this->setItemDelegate(new EntityTreeViewStyleDelegate(this));
		this->header()->hide();
		this->setStyleSheet(R"(
    QTreeView::indicator:checked {
        image: url(:/entityTree/icons/pqEyeball.svg);
    }
    QTreeView::indicator:unchecked {
        image: url(:/entityTree/icons/pqEyeballClosed.svg);
    }
    QTreeView::item {
        height: 20px;
        padding-left: 4px;
    }
    QTreeView::item:hover {
        background-color: #cfe2f5;   /* 悬浮浅蓝，可自己改颜色 */
        color: #202020;
    }
    QTreeView::item:selected {
        background-color: #7ab2e8;   /* 选中颜色 */
        color: white;
    }
    QTreeView::branch {
        background: transparent;
    }
    QTreeView::branch:has-siblings:!adjoins-item,
    QTreeView::branch:has-siblings:adjoins-item,
    QTreeView::branch:!has-children:!has-siblings:adjoins-item {
        border-image: none;
    }
    QTreeView::branch:has-children:!has-siblings:closed,
    QTreeView::branch:closed:has-children:has-siblings {
        border-image: none;
        image: url(:/widgets/icons/arrow_right.svg);
    }
    QTreeView::branch:open:has-children:!has-siblings,
    QTreeView::branch:open:has-children:has-siblings {
        border-image: none;
        image: url(:/widgets/icons/arrow_down.svg);
    }
)");
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);   // 获得焦点
		viewport()->setAttribute(Qt::WA_Hover); // 关键：让视图识别hov
		setItemDelegate(new HighlightDelegate(this, this));
		setSelectionBehavior(QAbstractItemView::SelectRows);
		setSelectionMode(QAbstractItemView::SingleSelection);

		// 👇 这一句是关键！禁止点行触发勾选
		setEditTriggers(QAbstractItemView::NoEditTriggers);

		setContextMenuPolicy(Qt::CustomContextMenu);
		connect(this, &QTreeView::customContextMenuRequested, this, &TreeViewPanel::onContextMenu);

		connect(this, &QTreeView::doubleClicked, this, [this](const QModelIndex& index)
			{
				if (!index.isValid()) return;
				::Core::ECS::Actor* actor = static_cast<::Core::ECS::Actor*>(index.data(Qt::UserRole).value<void*>());
				
				if (actor) {
					Feature* feature=dynamic_cast<Feature*>(actor);
					if (feature) {
						emit selectFeature(feature);
					}
				}
			});
	}
	TreeViewPanel::~TreeViewPanel()
	{
		delete mInternal;
	}

	void TreeViewPanel::updateTreeViewSketcherRoot()
	{
		//System::JobSystem::DelayExecute([this](JobDispatchArgs) {
			mInternal->mModel->onSketcherChange();
			//});
	}

	void TreeViewPanel::addActorToTree(const std::vector<Core::ECS::Actor*>& actor)
	{
		//System::JobSystem::DelayExecute([this,actor](JobDispatchArgs) {
			mInternal->mModel->beginBatchOperation();
			mInternal->mModel->notifyActorsCreated(actor);
			mInternal->mModel->endBatchOperation();			
		//});
	}

	void TreeViewPanel::removeActorFromTree(const std::vector<Core::ECS::Actor*>& actor)
	{
		//System::JobSystem::DelayExecute([this, actor](JobDispatchArgs) {
			mInternal->mModel->beginBatchOperation();
			mInternal->mModel->notifyActorsRemoved(actor);
			mInternal->mModel->endBatchOperation();
			//});
	}

	void TreeViewPanel::updateActorInTree(const std::vector<Operation>& operations)
	{
		//System::JobSystem::DelayExecute([this, operations](JobDispatchArgs) {

			mInternal->mModel->beginBatchOperation();
			for (int i = 0;i < operations.size();i++) {
				if (operations[i].actors.size() > 0) {
					if (operations[i].type == OperationType::Add) {
						mInternal->mModel->notifyActorsCreated(operations[i].actors);
					}
					else if (operations[i].type == OperationType::Remove) {
						mInternal->mModel->notifyActorsRemoved(operations[i].actors);
					}
					else if (operations[i].type== OperationType::Update) {
						mInternal->mModel->notifyActorsModified(operations[i].actors);
					}
				}
			}
			mInternal->mModel->endBatchOperation();

			//});
	}

	void TreeViewPanel::updateTreeViewSceneRoot() {
		//System::JobSystem::DelayExecute([this](JobDispatchArgs) {
			mInternal->mModel->onSceneRootChange();
		//});
	}

	void TreeViewPanel::highlightByActor(Core::ECS::Actor* actor)
	{
		if (!actor) {
			clearHighlight();
			return;
		}

		auto model = qobject_cast<MOON::EntityTreeModel*>(this->model());
		if (!model) return;
		auto item=model->actorItem(actor);
		if (item) {
			QModelIndex idx = item->index();
			if (idx != m_highlightIndex) {
				// 清空旧的
				QModelIndex old = m_highlightIndex;
				m_highlightIndex = idx;
				if (old.isValid()) update(old);
				if (idx.isValid()) update(idx);

				this->expand(idx.parent());//逐级展开折叠的父节点
				this->scrollTo(idx, QAbstractItemView::PositionAtCenter);//滚动至屏幕中间
			}
		}
	}

	void TreeViewPanel::clearHighlight()
	{
		if (m_highlightIndex.isValid()) {
			QModelIndex old = m_highlightIndex;
			m_highlightIndex = QModelIndex();
			update(old);
		}
	}

	void TreeViewPanel::clearLastHoverIndex()
	{
		mInternal->m_lastIndex = QModelIndex();
	}

	void TreeViewPanel::mousePressEvent(QMouseEvent* event)
	{
		QPoint mousePos = event->pos();
		QTreeView::mousePressEvent(event);
		QModelIndex index = indexAt(mousePos);
		if (!index.isValid()) return;
		QRect itemRect = visualRect(index);
		// 获取item在视图中的矩形
		if (!itemRect.contains(mousePos)) {
			return;
		}
		QStyleOptionViewItem option = viewOptions();
		option.rect = itemRect;
		option.index = index;
		QRect textRect = style()->subElementRect(QStyle::SE_ItemViewItemText, &option, this);
		QPoint posInItem = mousePos;
		if (textRect.contains(posInItem)) {
			::Core::ECS::Actor* actor = static_cast<::Core::ECS::Actor*>(index.data(Qt::UserRole).value<void*>());
			if (actor) {
				if (isEntityCheckAble(actor->GetName())) {
				   GetService(PropertyWidget).setSelectedActor(actor);
                   emit setSelectActor(actor);
				}
			}
			else
			{
				//SketcherObj* sketcher = static_cast<SketcherObj*>(index.data(Qt::UserRole+1).value<void*>());
				//SketcherObjManager::instance().setCurrentActiveSketcherObj(sketcher);
			}
		}
	}
	void TreeViewPanel::mouseMoveEvent(QMouseEvent* event)
	{
		// 获取鼠标下的项
		QModelIndex index = indexAt(event->pos());
		if (index != mInternal->m_lastIndex) {	
			// 如果离开上一项 → 发送离开信号
			if (mInternal->m_lastIndex.isValid()) {
				::Core::ECS::Actor* lastActor = static_cast<::Core::ECS::Actor*>(mInternal->m_lastIndex.data(Qt::UserRole).value<void*>());
				emit itemLeave(lastActor);
			}
			// 如果进入新项 → 发送悬浮信号
			if (index.isValid()) {
				::Core::ECS::Actor* actor = static_cast<::Core::ECS::Actor*>(index.data(Qt::UserRole).value<void*>());
				emit itemHovered(actor);
			}
			mInternal->m_lastIndex = index;
		}
		QTreeView::mouseMoveEvent(event);
	}
	void TreeViewPanel::onContextMenu(const QPoint& p_pos)
	{
		const QModelIndex index = indexAt(p_pos);
		if (!index.isValid()) {
			return;
		}
		Core::ECS::Actor* actor = static_cast<Core::ECS::Actor*>(
			index.data(Qt::UserRole).value<void*>());

		QMenu menu(this);
		QAction* remove = menu.addAction(QString::fromUtf8("Delete"));
		remove->setEnabled(canDeleteActor(actor));
		if (menu.exec(viewport()->mapToGlobal(p_pos)) == remove && actor != nullptr) {
			deleteActor(actor);
		}
	}
	bool TreeViewPanel::canDeleteActor(Core::ECS::Actor* p_actor) const
	{
		if (p_actor == nullptr) {
			// The items of a sketch - its curves and its constraints - stand for the
			// sketch object rather than for an actor.
			return false;
		}
		if (dynamic_cast<Feature*>(p_actor) != nullptr) {
			return true;
		}
		// A light, a camera, a post process stack or a reflection probe is part of how
		// the scene is set up rather than part of the model the tree edits, so those
		// actors are not something this menu deletes. The check is by component and not
		// by name: every kind of light carries a CLight.
		if (p_actor->GetComponent<Core::ECS::Components::CLight>() != nullptr
			|| p_actor->GetComponent<Core::ECS::Components::CCamera>() != nullptr
			|| p_actor->GetComponent<Core::ECS::Components::CPostProcessStack>() != nullptr
			|| p_actor->GetComponent<Core::ECS::Components::CReflectionProbe>() != nullptr) {
			return false;
		}
		// Anything that hangs under a feature is part of it: the render anchors, the
		// solid/shell groups and the face/edge leaves are how the feature is drawn and
		// picked, so deleting one of them on its own would leave the feature looking
		// for something that is no longer there.
		for (Core::ECS::Actor* parent = p_actor->GetParent();
			parent != nullptr;
			parent = parent->GetParent()) {
			if (dynamic_cast<Feature*>(parent) != nullptr) {
				return false;
			}
		}
		return true;
	}
	void TreeViewPanel::deleteActor(Core::ECS::Actor* p_actor)
	{
		if (Feature* feature = dynamic_cast<Feature*>(p_actor)) {
			deleteFeatureChain(feature);
			return;
		}
		// An ordinary actor - a light, an imported model - has nothing built on it, so
		// it goes on its own. What hangs under it goes with it: the scene removes the
		// actors below a removed one.
		auto scene = GetService(Editor::Panels::SceneView).GetScene();
		GetViewerWidget.removeActorFromTreeView(p_actor);
		if (auto* topo = dynamic_cast<TopoActor*>(p_actor)) {
			topo->RemoveFromScene();
		}
		else if (scene) {
			scene->RemoveActor(p_actor);
		}
		delete p_actor;
		refreshSelectionAfterRemoval();
	}
	void TreeViewPanel::deleteFeatureChain(Feature* p_feature)
	{
		// The body is a chain: every feature listed after this one was built on it,
		// directly or through the ones in between, so it goes with it.
		const auto fromFeatureOnwards = [p_feature]() {
			std::vector<Feature*> chain;
			bool reached = false;
			for (Feature* feature : FeatureBody::instance().getFeatures()) {
				if (feature == p_feature) {
					reached = true;
				}
				if (reached) {
					chain.push_back(feature);
				}
			}
			return chain;
			};

		std::vector<Feature*> victims = fromFeatureOnwards();
		if (victims.empty()) {
			return;
		}
		// A panel holds the feature it edits while it is open, and cancelling the panel
		// of a feature that was never committed deletes that feature - so the chain is
		// read again afterwards.
		auto& taskPanel = GetService(TaskViewWidget);
		Feature* editing = taskPanel.editingFeature();
		if (editing != nullptr
			&& std::find(victims.begin(), victims.end(), editing) != victims.end()) {
			taskPanel.clickCancel();
			victims = fromFeatureOnwards();
			if (victims.empty()) {
				return;
			}
		}
		for (Feature* feature : victims) {
			// The drawing tools and the sketch panel reach a sketch through the
			// manager, which would be left with a pointer to something that is gone.
			if (auto* sketch = dynamic_cast<SketcherFeature*>(feature)) {
				SketcherObjManager::instance().removeSketcherFeature(sketch);
			}
		}
		for (Feature* feature : victims) {
			feature->RemoveFromScene();
			GetViewerWidget.removeActorFromTreeView(feature);
		}
		// From the top of the chain down, so a feature never outlives the one it was
		// built on.
		for (int i = static_cast<int>(victims.size()) - 1; i >= 0; --i) {
			delete victims[i];
		}
		// What is left below was hidden when the features above it were built, so the
		// new top of the chain takes their place: without this the body would simply
		// disappear from the viewport with the features that just went.
		const std::vector<Feature*>& remaining = FeatureBody::instance().getFeatures();
		if (!remaining.empty()) {
			Feature* tip = remaining.back();
			for (Feature* feature : remaining) {
				const bool visible = feature == tip;
				if (feature->IsActive() != visible) {
					feature->SetActive(visible);
					GetViewerWidget.modifyActorInTreeView(feature);
				}
			}
		}
		refreshSelectionAfterRemoval();
	}
	void TreeViewPanel::refreshSelectionAfterRemoval()
	{
		auto scene = GetService(Editor::Panels::SceneView).GetScene();
		if (scene == nullptr) {
			return;
		}
		// Everything that was selected and is not in the scene any more has to leave
		// the selection and the property panel as well, or they keep pointing at
		// actors that were deleted.
		bool lostSomething = false;
		for (const SelectID& id : SelectionManager::instance().getSelect()) {
			if (scene->FindActorByID(id.actorId) == nullptr) {
				lostSomething = true;
				break;
			}
		}
		if (lostSomething) {
			SelectionManager::instance().clearSelect();
			GetService(PropertyWidget).setSelectedActor(nullptr);
			clearHighlight();
		}
	}
}
