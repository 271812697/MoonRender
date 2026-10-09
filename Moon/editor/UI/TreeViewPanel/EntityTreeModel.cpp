#include "EntityTreeModel.h"
#include "Core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include "Core/SceneSystem/Scene.h"
#include "renderer/Context.h"
#include "treeViewpanel.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/log.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <QTimer>

namespace MOON {

	class EntityTreeModel::EntityTreeModelInternal {
	public:
		EntityTreeModelInternal(EntityTreeModel* model, TreeViewPanel* tree) :self(model), mTreeView(tree) {

		}
		void init() {
			QList<QStandardItem*> arrays;
			sceneRoot = new QStandardItem;
			sceneRoot->setText(QString("SceneActor"));
			sketcherRoot = new QStandardItem;
			sketcherRoot->setText(QString("Sketcher"));
			arrays.push_back(sceneRoot);
			arrays.push_back(sketcherRoot);
			self->invisibleRootItem()->appendColumn(arrays);
			mIconMaps["eyeOpen"] = QIcon(":/entityTree/icons/pqEyeball.svg");
			mIconMaps["eyeClose"] = QIcon(":/entityTree/icons/pqEyeballClosed.svg");
			
			mIconMaps["Geomerty"] = QIcon(":/widgets/icons/Geomerty.png");
			mIconMaps["Solid"] = QIcon(":/widgets/icons/Model.png");
			mIconMaps["Shell"] = QIcon(":/widgets/icons/Model.png");
			mIconMaps["TopoGroup"] = QIcon(":/widgets/icons/Model.png");
			mIconMaps["TopoFace"] = QIcon(":/widgets/icons/Geomerty.png");
			mIconMaps["TopoEdge"] = QIcon(":/widgets/icons/Geomerty.png");
			mIconMaps["TopoVertex"] = QIcon(":/widgets/icons/Geomerty.png");
			mIconMaps["PointLight"] = QIcon(":/widgets/icons/PointLight.png");
			mIconMaps["DirectionalLight"] = QIcon(":/widgets/icons/DirectionalLight.png");
			mIconMaps["SkyBox"] = QIcon(":/widgets/icons/awesomeface.png");
			mIconMaps["PostProcessStack"] = QIcon(":/widgets/icons/awesomeface.png");
			mIconMaps[""]= QIcon(":/widgets/icons/Model.png");
			mIconMaps["Sketcher"] = QIcon(":/widgets/icons/Sketcher_NewSketch.svg");

			mIconMaps["Pad"] = QIcon(":/widgets/icons/partdesign/PartDesign_Pad.svg");
			mIconMaps["Revolve"] = QIcon(":/widgets/icons/partdesign/PartDesign_Revolution.svg");
			mIconMaps["Thickness"] = QIcon(":/widgets/icons/partdesign/PartDesign_Thickness.svg");
			mIconMaps["Fillet"] = QIcon(":/widgets/icons/partdesign/PartDesign_Fillet.svg");
			mIconMaps["Chamfer"] = QIcon(":/widgets/icons/partdesign/PartDesign_Chamfer.svg");
			mIconMaps["Pocket"] = QIcon(":/widgets/icons/partdesign/PartDesign_Pocket.svg");
			mIconMaps["Groove"] = QIcon(":/widgets/icons/partdesign/PartDesign_Groove.svg");
			mIconMaps["DatumLine"] = QIcon(":/widgets/icons/partdesign/PartDesign_Line.svg");
			mIconMaps["DatumPlane"] = QIcon(":/widgets/icons/partdesign/PartDesign_Plane.svg");
			// A body is the node its features hang under, and it wears FreeCAD's body icon.
			mIconMaps["Body"] = QIcon(":/widgets/icons/partdesign/PartDesign_Body.svg");
			mIconMaps["Pipe"] = QIcon(":/widgets/icons/partdesign/PartDesign_AdditivePipe.svg");
			mIconMaps["PipeCut"] = QIcon(":/widgets/icons/partdesign/PartDesign_SubtractivePipe.svg");
			// The transform features show the icons their toolbar buttons wear.
			mIconMaps["PolarPattern"] = QIcon(":/widgets/icons/partdesign/PartDesign_PolarPattern.svg");
			mIconMaps["Mirror"] = QIcon(":/widgets/icons/partdesign/PartDesign_Mirrored.svg");
			mIconMaps["LinearPattern"] = QIcon(":/widgets/icons/partdesign/PartDesign_LinearPattern.svg");
		}
	private:
		friend EntityTreeModel;
		TreeViewPanel* mTreeView = nullptr;// treeView
		bool manaulCheck = false;
		QModelIndex	mCurrentSelect;
		EntityTreeModel* self = nullptr;
		QStandardItem* sceneRoot = nullptr;
		QStandardItem* sketcherRoot = nullptr;
		std::unordered_map<std::string, QIcon>mIconMaps;
		std::unordered_map<Core::ECS::Actor*, QStandardItem*>actorToItem;

	};
	EntityTreeModel::EntityTreeModel(TreeViewPanel* parent) :
		QStandardItemModel(parent), mInternal(new EntityTreeModelInternal(this, parent))
	{
		mInternal->init();
		RegService(EntityTreeModel, *this);
		connect(this, &QStandardItemModel::itemChanged, this, &EntityTreeModel::onCheckStageChange);
	}
	EntityTreeModel::~EntityTreeModel()
	{
		delete mInternal;
	}
	void EntityTreeModel::beginBatchOperation()
	{		
		mInternal->mTreeView->clearHighlight();
		mInternal->mTreeView->clearLastHoverIndex();
		m_batchMode = true;
		m_batchCounter++;
		m_pendingOps.clear();

		// 🔥 暂停所有视图更新
		mInternal->mTreeView->setUpdatesEnabled(false);
		blockSignals(true);

		// 🔥 停止定时器，避免中途触发
		if (m_pendingTimer) {
			m_pendingTimer->stop();
		}
	}
	void EntityTreeModel::endBatchOperation()
	{
		m_batchCounter--;
		if (m_batchCounter > 0) {
			return;  // 嵌套批量操作
		}

		m_batchMode = false;

		// 🔥 处理所有待定操作
		processPendingUpdates();

		// 🔥 恢复视图更新
		blockSignals(false);
		mInternal->mTreeView->setUpdatesEnabled(true);

		// 🔥 一次性刷新视图
		emit layoutChanged();
		emit dataChanged(QModelIndex(), QModelIndex());
	}
	QStandardItem* EntityTreeModel::acquireItem()
	{
		return m_itemPool.acquire();
	
	}
	void EntityTreeModel::releaseItem(QStandardItem* item)
	{
		m_itemPool.release(item);
	}
	void EntityTreeModel::notifyActorsCreated(const std::vector<Core::ECS::Actor*>& actors)
	{
		if (actors.empty()) return;

		if (m_batchMode) {
			// 批量模式：收集操作
			PendingOperation op;
			op.type = PendingOperation::Add;
			op.actors = actors;
			m_pendingOps.push_back(std::move(op));
			return;
		}

		// 直接处理
		processBatchAdd(actors);
	}
	void EntityTreeModel::notifyActorsRemoved(const std::vector<Core::ECS::Actor*>& actors)
	{
		if (actors.empty()) return;

		if (m_batchMode) {
			PendingOperation op;
			op.type = PendingOperation::Remove;
			op.actors = actors;
			m_pendingOps.push_back(std::move(op));
			return;
		}

		processBatchRemove(actors);
	}
	void EntityTreeModel::notifyActorsModified(const std::vector<Core::ECS::Actor*>& actors)
	{
		if (actors.empty()) return;

		if (m_batchMode) {
			PendingOperation op;
			op.type = PendingOperation::Modify;
			op.actors = actors;
			m_pendingOps.push_back(std::move(op));
			return;
		}
		processBatchModify(actors);
	}
	void EntityTreeModel::notifyActorCreated(Core::ECS::Actor* actor)
	{
		if (m_batchMode) {
			// 合并到批量操作
			bool found = false;
			for (auto& op : m_pendingOps) {
				if (op.type == PendingOperation::Add) {
					op.actors.push_back(actor);
					found = true;
					break;
				}
			}
			if (!found) {
				PendingOperation op;
				op.type = PendingOperation::Add;
				op.actors.push_back(actor);
				m_pendingOps.push_back(op);
			}
			return;
		}

		// 🔥 使用定时器延迟50ms，合并相同时间片内的更新
		if (!m_pendingTimer) {
			m_pendingTimer = new QTimer(this);
			m_pendingTimer->setSingleShot(true);
			connect(m_pendingTimer, &QTimer::timeout, this, &EntityTreeModel::processPendingUpdates);
		}

		// 添加到待处理列表
		PendingOperation op;
		op.type = PendingOperation::Add;
		op.actors = { actor };
		m_pendingOps.push_back(op);

		m_pendingTimer->start(50);  // 50ms内所有更新合并为一次
	}
	void EntityTreeModel::notifyActorRemoved(Core::ECS::Actor* actor)
	{
		if (m_batchMode) {
			// 合并到批量操作
			for (auto& op : m_pendingOps) {
				if (op.type == PendingOperation::Remove) {
					op.actors.push_back(actor);
					return;
				}
			}
			PendingOperation op;
			op.type = PendingOperation::Remove;
			op.actors = { actor };
			m_pendingOps.push_back(op);
			return;
		}

		// 使用延迟合并
		PendingOperation op;
		op.type = PendingOperation::Remove;
		op.actors = { actor };
		m_pendingOps.push_back(op);

		if (m_pendingTimer) {
			m_pendingTimer->start(50);
		}
	}
	void EntityTreeModel::onSketcherChange()
	{
		//mInternal->manaulCheck = false;
		//mInternal->sketcherRoot->removeRows(0, mInternal->sketcherRoot->rowCount());
		//auto sketcherList=SketcherObjManager::instance().GetAllSketcherObjs();
		//auto curActiveSketch=SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		//for (int i = 0;i < sketcherList.size();i++) {
		//	QStandardItem* temp = new QStandardItem;
		//	temp->setText(QString::fromStdString("sketcher"));
		//	temp->setIcon(mInternal->mIconMaps["Sketcher"]);
		//	mInternal->sketcherRoot->setChild(mInternal->sketcherRoot->rowCount(), temp);
		//	temp->setData(QVariant::fromValue((void*)sketcherList[i]), Qt::UserRole+1);
		//	temp->setData(QVariant::fromValue((void*)nullptr), Qt::UserRole);
		//	temp->setCheckable(true);
		//	temp->setCheckState(curActiveSketch== sketcherList[i] ? Qt::Checked : Qt::Unchecked);
		//}
		//mInternal->manaulCheck =true;
	}
	void EntityTreeModel::onSceneRootChange()
	{
		Core::SceneSystem::Scene* scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene();
		if (scene == nullptr) {
			return;
		}
		mInternal->mTreeView->clearHighlight();
		mInternal->mTreeView->clearLastHoverIndex();
		mInternal->manaulCheck = false;
		mInternal->sceneRoot->removeRows(0, mInternal->sceneRoot->rowCount());
		mInternal->actorToItem.clear();
		auto& actors = scene->GetActors();
		beginBatchOperation();
		notifyActorsCreated(actors);
		
		endBatchOperation();
	
		mInternal->manaulCheck = true;
	}
	void EntityTreeModel::processPendingUpdates()
	{
		if (m_pendingOps.empty()) return;
		// 🔥 合并操作（同类型且同Actor只保留最后一次）
		std::unordered_map<Core::ECS::Actor*, PendingOperation::Type> lastOp;
		// 从后往前遍历，保留最后一次操作
		for (auto it = m_pendingOps.rbegin(); it != m_pendingOps.rend(); ++it) {
			for (auto* actor : it->actors) {
				if (lastOp.find(actor) == lastOp.end()) {
					lastOp[actor] = it->type;
				}
				else if (it->type== PendingOperation::Type::Remove) {
					lastOp[actor] = it->type;
				}
			}
		}

		// 按类型分组处理
		std::vector<Core::ECS::Actor*> toAdd;
		std::vector<Core::ECS::Actor*> toRemove;
		std::vector<Core::ECS::Actor*> toModify;

		// Walked in the order the actors were queued in rather than over the map: the
		// map only says *what* happened to each actor, and its own order is a hash
		// order, which is how the features of a document that was just read ended up
		// in the tree in an order that was not the one they were built in.
		std::unordered_set<Core::ECS::Actor*> queued;
		for (const PendingOperation& op : m_pendingOps) {
			for (Core::ECS::Actor* actor : op.actors) {
				if (!queued.insert(actor).second) {
					continue;  // already placed by its first appearance
				}
				switch (lastOp[actor]) {
				case PendingOperation::Add:
					toAdd.push_back(actor);
					break;
				case PendingOperation::Remove:
					toRemove.push_back(actor);
					break;
				case PendingOperation::Modify:
					toModify.push_back(actor);
					break;
				}
			}
		}

		m_pendingOps.clear();

		// 🔥 执行批量操作
		if (!toRemove.empty()) {
			processBatchRemove(toRemove);
		}
		if (!toAdd.empty()) {
			processBatchAdd(toAdd);
		}
		if (!toModify.empty()) {
			processBatchModify(toModify);
		}

		// 发射聚合信号
		if (!toAdd.empty() || !toRemove.empty()) {
			emit layoutChanged();
		}
	}
	QStandardItem* EntityTreeModel::sceneRoot()
	{
		return mInternal->sceneRoot;
	}
	QStandardItem* EntityTreeModel::actorItem(Core::ECS::Actor* actor)
	{
		if (mInternal->actorToItem.find(actor) != mInternal->actorToItem.end()) {
			return mInternal->actorToItem[actor];
		}
		return nullptr;
	}
	// 同步子节点状态（父节点勾选/取消时调用）
	void syncChildItems(QStandardItem* parent, Qt::CheckState state) {
		for (int i = 0; i < parent->rowCount(); ++i) {
			QStandardItem* child = parent->child(i);
			if (child && child->isCheckable() && child->checkState() != state) {
				child->setCheckState(state);
				// 如果子节点还有子节点，递归同步
				if (child->hasChildren()) {
					syncChildItems(child, state);
				}
			}
		}
	}

	// 同步父节点状态（子节点勾选/取消时调用）
	void syncParentItem(QStandardItem* parent) {
		int checkedCount = 0;
		int totalCount = parent->rowCount();

		// 统计已勾选的子节点数量
		for (int i = 0; i < totalCount; ++i) {
			QStandardItem* child = parent->child(i);
			if (child && child->isCheckable() && child->checkState() == Qt::Checked) {
				checkedCount++;
			}
		}

		// 根据子节点勾选情况更新父节点状态
		Qt::CheckState newState;
		if (checkedCount == 0) {
			newState = Qt::Unchecked;      // 全未勾选
		}
		else if (checkedCount == totalCount) {
			newState = Qt::Checked;        // 全勾选
		}
		else {
			newState = Qt::PartiallyChecked; // 部分勾选（半选状态）
		}

		if (parent->checkState() != newState) {
			parent->setCheckState(newState);
			// 如果父节点还有父节点，递归检查
			if (parent->parent()) {
				syncParentItem(parent->parent());
			}
		}
	}

	void EntityTreeModel::onCheckStageChange(QStandardItem* item)
	{
		//// 防止递归调用导致的死循环
		static bool isProcessing = false;
		if (isProcessing) {
			return;
		}
		isProcessing = true;

		// 如果是父节点，同步所有子节点状态
		if (item->hasChildren()) {
			syncChildItems(item, item->checkState());
		}
		// 如果是子节点，检查是否需要同步父节点状态
		if (item->parent()) {
			syncParentItem(item->parent());
		}

		if (item->isCheckable()) {
			Core::ECS::Actor* actor = static_cast<Core::ECS::Actor*>(item->data(Qt::UserRole).value<void*>());
			if (actor) {
				Qt::CheckState currentState = item->checkState();
				if (currentState == Qt::Checked) {
					actor->SetActive(true);
				}
				else //if (currentState == Qt::Unchecked)
				{
					actor->SetActive(false);
				}
				// Walk up the hierarchy and refresh the TopoShape render buffer
				// whenever any node of a topo actor is toggled. The topology
				// leaves (Face_*/Edge_*) can now live at arbitrary depth below
				// the topo actor (Solid/Shell groups), so a plain parent lookup
				// is no longer enough.
				for (Core::ECS::Actor* cur = actor; cur; cur = cur->HasParent() ? cur->GetParent() : nullptr) {
					if (cur->HasComponent("CTopoShape")) {
						cur->GetComponent<::Core::ECS::Components::CTopoShape>()->updateChildBuffer();
						break;
					}
				}
			}
		}
		isProcessing = false;
	}
	void EntityTreeModel::processBatchAdd(const std::vector<Core::ECS::Actor*>& actors)
	{
		for (int i = 0; i < actors.size(); i++) {
			if (!actors[i]->HasParent()) {
				std::vector <QStandardItem*> root = { mInternal->sceneRoot };
				std::vector<Core::ECS::Actor*> s = { actors[i] };
				while (!s.empty()) {
					Core::ECS::Actor* cur = s.back(); s.pop_back();
					QStandardItem* parent = root.back(); root.pop_back();
					QStandardItem* temp = m_itemPool.acquire();;
					mInternal->actorToItem[cur] = temp;
					auto name = cur->GetName();
					auto tag = cur->GetTag();
					temp->setText(QString::fromStdString(name));
					if (mInternal->mIconMaps.find(tag) != mInternal->mIconMaps.end()) {
						temp->setIcon(mInternal->mIconMaps[tag]);
					}
					temp->setCheckable(true);
					temp->setCheckState(cur->IsActive()?Qt::Checked:Qt::Unchecked);
					temp->setData(QVariant::fromValue((void*)cur), Qt::UserRole);
					parent->appendRow( temp);

					QModelIndex index = this->indexFromItem(temp);
					std::vector<Core::ECS::Actor*>& childList = cur->GetChildren();
					if (childList.size() > 0) {
						for (int i = childList.size() - 1;i >= 0;i--) {
							s.push_back(childList[i]);
							root.push_back(temp);
						}
					}

				}
			}
		}
		// 统计
		m_stats.totalCreated += actors.size();
		m_stats.poolHitCount += actors.size();
		sortFeatureItems();
	}
	/** The features at the scene root are listed in the order they are built in, the
	 * order of the body - which is the chain, and what a document stores. That order
	 * is not the one the tree is filled in with: a refresh hands every actor over in
	 * one batch (see processPendingUpdates), so a feature could end up above the one
	 * it is built on, and reading a document showed the downstream feature first.
	 *
	 * Only the feature items are rearranged, and only among themselves: each one is
	 * put back into a slot that already held a feature, so a light or an imported
	 * model keeps the place it was created in. */
	void EntityTreeModel::sortFeatureItems()
	{
		if (mInternal == nullptr || mInternal->sceneRoot == nullptr) {
			return;
		}
		// Where a feature sits in the chain of its own body, by identity: a body is the
		// one list that has the order right, and every body's features hang under that
		// body's node - so this runs for the scene root and for each body node under it.
		const auto chainIndexOf = [](const Feature* p_feature) {
			const FeatureBody* body = p_feature != nullptr ? p_feature->getBody() : nullptr;
			if (body == nullptr) {
				return -1;
			}
			const std::vector<Feature*>& chain = body->getFeatures();
			for (int i = 0; i < static_cast<int>(chain.size()); ++i) {
				if (chain[i] == p_feature) {
					return i;
				}
			}
			return -1;
			};
		const std::function<void(QStandardItem*)> sortUnder = [&](QStandardItem* root) {
		if (root == nullptr || root->rowCount() < 2) {
			return;
		}
		// "slots" would be the Qt keyword macro, hence the longer name.
		std::vector<int> featureSlots;
		std::vector<std::pair<int, QStandardItem*>> featureItems;
		for (int row = 0; row < root->rowCount(); ++row) {
			QStandardItem* item = root->child(row);
			auto* feature = item != nullptr
				? dynamic_cast<Feature*>(getActorFromItem(item))
				: nullptr;
			if (feature == nullptr) {
				continue;
			}
			featureItems.emplace_back(
				// Not in a body's chain (one that was created but not committed yet,
				// say): after every feature that is.
				chainIndexOf(feature) >= 0 ? chainIndexOf(feature) : 1000000,
				item);
			featureSlots.push_back(row);
		}
		if (featureItems.size() < 2) {
			return;
		}
		std::stable_sort(
			featureItems.begin(), featureItems.end(),
			[](const std::pair<int, QStandardItem*>& a,
				const std::pair<int, QStandardItem*>& b) {
				return a.first < b.first;
			});
		bool inOrder = true;
		for (int i = 0; i < static_cast<int>(featureItems.size()); ++i) {
			if (root->child(featureSlots[i]) != featureItems[i].second) {
				inOrder = false;
				break;
			}
		}
		if (inOrder) {
			return;  // the common case: nothing to touch
		}
		// Every feature item is taken out - from the last slot up, so the rows below
		// keep their numbers on the way - and put back into those same slots in chain
		// order. takeRow() hands the items over instead of deleting them.
		for (int i = static_cast<int>(featureSlots.size()) - 1; i >= 0; --i) {
			root->takeRow(featureSlots[i]);
		}
		for (int i = 0; i < static_cast<int>(featureSlots.size()); ++i) {
			root->insertRow(featureSlots[i], featureItems[i].second);
		}
		// The features of each body hang under that body's node, so the same ordering is
		// applied one level down.
		for (int row = 0; row < root->rowCount(); ++row) {
			sortUnder(root->child(row));
		}
		};
		sortUnder(mInternal->sceneRoot);
	}
	void EntityTreeModel::processBatchRemove(const std::vector<Core::ECS::Actor*>& actors)
	{
		std::vector<QStandardItem*> rootItem;
		//remove actors
		for (int i = 0;i < actors.size();i++) {
			auto it = mInternal->actorToItem.find(actors[i]);
			if (it != mInternal->actorToItem.end()) {
				std::vector<QStandardItem*>stack;
				QStandardItem* curRootItem = it->second;
				rootItem.push_back(curRootItem);
				stack.push_back(curRootItem);
				while (!stack.empty()) {
					QStandardItem* item = stack.back();stack.pop_back();
					Core::ECS::Actor* actor = static_cast<Core::ECS::Actor*>(item->data(Qt::UserRole).value<void*>());
					if (actor) {
						mInternal->actorToItem.erase(actor);
					}
					int rowCnt = item->rowCount();
					for (int i = 0;i < rowCnt;i++) {
						QStandardItem* child = item->child(i);
						stack.push_back(child);
					}
				}
			}
		}
		//remove QStandardItem
		for (int i = 0;i < rootItem.size();i++) {
			std::vector<QStandardItem*>stack;
			QStandardItem* curRootItem = rootItem[i];
			//stack.push_back(curRootItem);
			curRootItem->parent()->removeRow(curRootItem->row());
			//while (!stack.empty()) {
			//	QStandardItem* item = stack.back();stack.pop_back();
			//	int rowCnt = item->rowCount();
			//	for (int i = 0;i < rowCnt;i++) {
			//		QStandardItem* child = item->child(i);
			//		item->takeChild(i);
			//		stack.push_back(child);
			//	}
			//	// 🔥 回收Item到池中
			//	m_itemPool.release(item);
			//	m_stats.totalRemoved++;
			//}
		}
	}
	void EntityTreeModel::processBatchModify(const std::vector<Core::ECS::Actor*>& actors)
	{
		// 处理修改（名称、tag、激活状态等）
		for (auto* actor : actors) {
			auto it = mInternal->actorToItem.find(actor);
			if (it != mInternal->actorToItem.end()) {
				QStandardItem* item = it->second;

				// 更新名称
				//auto name = actor->GetName();
				//if (item->text() != QString::fromStdString(name)) {
				//	item->setText(QString::fromStdString(name));
				//}

				// 更新激活状态
				bool active = actor->IsActive();
				Qt::CheckState newState = active ? Qt::Checked : Qt::Unchecked;
				if (item->checkState() != newState) {
					item->setCheckState(newState);
				}

				// 更新图标（如果tag变化）
				//auto tag = actor->GetTag();
				//if (mInternal->mIconMaps.find(tag) != mInternal->mIconMaps.end()) {
				//	QIcon icon = mInternal->mIconMaps[tag];
				//	if (item->icon().cacheKey() != icon.cacheKey()) {
				//		item->setIcon(icon);
				//	}
				//}
			}
		}
	}
	QStandardItem* EntityTreeModel::createItemFromActor(Core::ECS::Actor* actor)
	{
		if (!actor) return nullptr;

		QStandardItem* item = m_itemPool.acquire();

		item->setText(QString::fromStdString(actor->GetName()));

		auto tag = actor->GetTag();
		auto it = mInternal->mIconMaps.find(tag);
		if (it != mInternal->mIconMaps.end()) {
			item->setIcon(it->second);
		}

		item->setCheckable(true);
		item->setCheckState(actor->IsActive() ? Qt::Checked : Qt::Unchecked);
		item->setData(QVariant::fromValue((void*)actor), Qt::UserRole);
		// Whether this row carries a name of its own (see flags()): the actor is alive
		// here, and the answer is kept on the item so that painting a row never has to
		// follow the pointer again - a row can outlive the actor it stands for.
		item->setData(
			dynamic_cast<Feature*>(actor) != nullptr || actor->GetTag() == "Body",
			kRenamableRole
		);
		return item;
	}
	void EntityTreeModel::addActorToTree(Core::ECS::Actor* actor, QStandardItem* parent)
	{
		if (!actor) return;

		QStandardItem* item = createItemFromActor(actor);
		if (!item) return;

		mInternal->actorToItem[actor] = item;

		if (parent) {
			parent->appendRow(item);
		}
		else if (actor->HasParent()) {
			auto parentIt = mInternal->actorToItem.find(actor->GetParent());
			if (parentIt != mInternal->actorToItem.end()) {
				parentIt->second->appendRow(item);
			}
			else {
				mInternal->sceneRoot->appendRow(item);
			}
		}
		else {
			mInternal->sceneRoot->appendRow(item);
		}
	}
	void EntityTreeModel::removeActorFromTree(Core::ECS::Actor* actor)
	{
		if (!actor) return;

		auto it = mInternal->actorToItem.find(actor);
		if (it == mInternal->actorToItem.end()) return;

		QStandardItem* item = it->second;
		QStandardItem* parent = item->parent();

		if (parent) {
			parent->takeRow(item->row());
		}
		else {
			// 尝试从 sceneRoot 移除
			for (int i = 0; i < mInternal->sceneRoot->rowCount(); ++i) {
				if (mInternal->sceneRoot->child(i) == item) {
					mInternal->sceneRoot->takeRow(i);
					break;
				}
			}
		}

		mInternal->actorToItem.erase(it);
		m_itemPool.release(item);
	}
	void EntityTreeModel::updateActorInTree(Core::ECS::Actor* actor)
	{
		if (!actor) return;

		auto it = mInternal->actorToItem.find(actor);
		if (it == mInternal->actorToItem.end()) return;

		QStandardItem* item = it->second;

		// 更新名称
		QString newName = QString::fromStdString(actor->GetName());
		if (item->text() != newName) {
			item->setText(newName);
		}

		// 更新激活状态
		Qt::CheckState newState = actor->IsActive() ? Qt::Checked : Qt::Unchecked;
		if (item->checkState() != newState) {
			item->setCheckState(newState);
		}

		// 更新图标
		auto tag = actor->GetTag();
		auto iconIt = mInternal->mIconMaps.find(tag);
		if (iconIt != mInternal->mIconMaps.end()) {
			if (item->icon().cacheKey() != iconIt->second.cacheKey()) {
				item->setIcon(iconIt->second);
			}
		}
	}
	Core::ECS::Actor* EntityTreeModel::getActorFromItem(QStandardItem* item)
	{
		if (!item) return nullptr;
		return static_cast<Core::ECS::Actor*>(item->data(Qt::UserRole).value<void*>());
	}
	void EntityTreeModel::updateActorName(Core::ECS::Actor* p_actor)
	{
		if (mInternal == nullptr || p_actor == nullptr) {
			return;
		}
		const auto found = mInternal->actorToItem.find(p_actor);
		if (found == mInternal->actorToItem.end() || found->second == nullptr) {
			return;
		}
		// The name an item was made with is its text; renaming an actor does not tell the
		// item, so it is written over here.
		found->second->setText(QString::fromStdString(p_actor->GetName()));
	}

	void EntityTreeModel::startRename(Core::ECS::Actor* p_actor)
	{
		if (mInternal == nullptr || p_actor == nullptr || mInternal->mTreeView == nullptr) {
			CORE_WARN(
				"[TreeView] cannot rename: model {0}, actor {1}, view {2}",
				mInternal != nullptr,
				p_actor != nullptr,
				mInternal != nullptr && mInternal->mTreeView != nullptr);
			return;
		}
		QStandardItem* item = actorItem(p_actor);
		if (item == nullptr) {
			CORE_WARN(
				"[TreeView] cannot rename '{0}': it has no row in the tree",
				p_actor->GetName());
			return;
		}
		const QModelIndex index = indexFromItem(item);
		if (!index.isValid()) {
			CORE_WARN(
				"[TreeView] cannot rename '{0}': its row is not an index",
				p_actor->GetName());
			return;
		}
		CORE_INFO(
			"[TreeView] renaming '{0}': editable {1}, triggers {2}",
			p_actor->GetName(),
			(item->flags() & Qt::ItemIsEditable) != 0,
			static_cast<int>(mInternal->mTreeView->editTriggers()));
		// The editor is put on the row itself: what is typed there arrives in setData().
		mInternal->mTreeView->setCurrentIndex(index);
		mInternal->mTreeView->edit(index);
	}

	Qt::ItemFlags EntityTreeModel::flags(const QModelIndex& index) const
	{
		Qt::ItemFlags flags = QStandardItemModel::flags(index);
		if (!index.isValid()) {
			return flags;
		}
		const QStandardItem* item = itemFromIndex(index);
		// Whether the row can be edited in place is read off the item, where it was put
		// while the actor was alive (see createItemFromActor). Qt asks this for every row
		// it paints, hovers or selects, and a row can outlive the actor it stands for - so
		// the actor is never looked at from here.
		if (item != nullptr && item->data(kRenamableRole).toBool()) {
			flags |= Qt::ItemIsEditable;
		}
		else {
			flags &= ~Qt::ItemIsEditable;
		}
		return flags;
	}

	bool EntityTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
	{
		if (role == Qt::EditRole && index.isValid()) {
			Core::ECS::Actor* actor = getActorFromItem(itemFromIndex(index));
			if (actor != nullptr) {
				const std::string wanted = value.toString().trimmed().toStdString();
				if (wanted.empty() || wanted == actor->GetName()) {
					return false;  // nothing to change: the row keeps its text
				}
				if (FeatureBody* body = FeatureBody::Of(actor)) {
					// A body's name is what the tree shows it as and what a document
					// writes into its <Body> node, and it has to tell the bodies apart.
					for (FeatureBody* other : FeatureBody::All()) {
						if (other != nullptr && other != body && other->GetName() == wanted) {
							CORE_WARN(
								"[TreeView] '{0}' is already the name of another body; the "
								"name was not changed",
								wanted);
							return false;
						}
					}
					body->SetName(wanted);
				}
				else if (Feature* feature = dynamic_cast<Feature*>(actor)) {
					// The name has to stay unique inside the body: the tree shows it, and a
					// document finds an actor by name when it puts back what the eye
					// switched off.
					if (FeatureBody* body = feature->getBody()) {
						for (Feature* other : body->getFeatures()) {
							if (other != nullptr && other != feature
								&& other->GetName() == wanted) {
								CORE_WARN(
									"[TreeView] '{0}' is already the name of another feature "
									"of this body; the name was not changed",
									wanted);
								return false;
							}
						}
					}
					feature->SetName(wanted);
				}
				else {
					return false;  // a row that has no name of its own
				}
			}
		}
		return QStandardItemModel::setData(index, value, role);
	}
	void EntityTreeModel::updateTopoShapeRecursive(Core::ECS::Actor* actor)
	{
		if (!actor) return;

		if (actor->HasComponent("CTopoShape")) {
			auto topoComp = actor->GetComponent<::Core::ECS::Components::CTopoShape>();
			if (topoComp) {
				topoComp->updateChildBuffer();
			}
		}

		for (auto* child : actor->GetChildren()) {
			updateTopoShapeRecursive(child);
		}
	}
}
