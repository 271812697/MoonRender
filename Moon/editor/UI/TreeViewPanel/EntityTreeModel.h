#pragma once
#include <QStandardItemModel>
namespace Core::ECS {
	class Actor;
}
namespace MOON
{
	class TreeViewPanel;
	class EntityTreeModel : public QStandardItemModel
	{
		Q_OBJECT
	public:
		EntityTreeModel(TreeViewPanel* parent);
		~EntityTreeModel();

		// 🔥 批量操作接口
		void beginBatchOperation();
		void endBatchOperation();

		// 🔥 对象池接口
		QStandardItem* acquireItem();
		void releaseItem(QStandardItem* item);

		// 🔥 外部通知接口
		void notifyActorsCreated(const std::vector<Core::ECS::Actor*>& actors);
		void notifyActorsRemoved(const std::vector<Core::ECS::Actor*>& actors);
		void notifyActorsModified(const std::vector<Core::ECS::Actor*>& actors);

		// 🔥 增量更新（单个）
		void notifyActorCreated(Core::ECS::Actor* actor);
		void notifyActorRemoved(Core::ECS::Actor* actor);
		/** Shows a new name for an actor that was renamed: the item of the tree keeps the
		 * name it was made with, so it is written over here. */
		void updateActorName(Core::ECS::Actor* p_actor);
		/** Puts the row of an actor into edit mode, so its name is typed where it is shown
		 * instead of in a dialog (the model applies it in setData). */
		void startRename(Core::ECS::Actor* p_actor);
		/** Applies a name typed on a row: the rows that stand for a feature or a body
		 * carry a name of their own, and that name goes to the actor (or the body).
		 * A name that is empty or already taken is refused, and the row keeps its text. */
		bool setData(
			const QModelIndex& index,
			const QVariant& value,
			int role = Qt::EditRole) override;
		/** Whether a row can be edited in place. It is asked on every edit instead of
		 * being written on the item when it is made: a body's row is created by the scene
		 * (which tells the tree about the actor right away) before the body itself is
		 * registered, so at that moment it could not know it is editable. */
		Qt::ItemFlags flags(const QModelIndex& index) const override;
		QStandardItem* sceneRoot();
		QStandardItem* actorItem(Core::ECS::Actor* actor);
	
		void onSketcherChange();
		void onSceneRootChange();

	private slots:
		void onCheckStageChange(QStandardItem* item);
		void processPendingUpdates();
    private:
        struct PendingOperation {
            enum Type { Add, Remove, Modify };
            Type type;
            Core::ECS::Actor* actor;
            std::vector<Core::ECS::Actor*> actors;  // 批量操作
        };

        class ItemPool {
        private:
            std::vector<QStandardItem*> m_pool;
           // std::mutex m_mutex;

            // 🔥 重置 Item 状态
            void resetItem(QStandardItem* item) {
                if (!item) return;

                // 清空所有数据
                item->setText(QString());
                item->setIcon(QIcon());
                item->setData(QVariant(), Qt::UserRole);
                item->setData(QVariant(), Qt::UserRole + 1);
                item->setCheckState(Qt::Unchecked);
                item->setCheckable(false);
                item->setEditable(false);
                item->setToolTip(QString());
                item->setWhatsThis(QString());
                item->setStatusTip(QString());
                item->setAccessibleText(QString());
                item->setAccessibleDescription(QString());
            }

        public:
            ItemPool(int initialSize = 200) {
                m_pool.reserve(initialSize);
                for (int i = 0; i < initialSize; ++i) {
                    m_pool.push_back(new QStandardItem());
                }
            }

            ~ItemPool() {
                //std::lock_guard<std::mutex> lock(m_mutex);
                for (auto* item : m_pool) {
                    delete item;
                }
                m_pool.clear();
            }

            QStandardItem* acquire() {
               // std::lock_guard<std::mutex> lock(m_mutex);

                if (m_pool.empty()) {
                    for (int i = 0; i < 50; ++i) {
                        m_pool.push_back(new QStandardItem());
                    }
                }

                QStandardItem* item = m_pool.back();
                m_pool.pop_back();

                // 🔥 重置后再给出去
                resetItem(item);
                item->setCheckable(true);  // 默认可勾选

                return item;
            }

            void release(QStandardItem* item) {
                //we should never use this
                //if (!item) return;

                ////std::lock_guard<std::mutex> lock(m_mutex);

                //// 🔥 重置后回收
                //resetItem(item);
                //m_pool.push_back(item);
            }

            void clear() {
                //std::lock_guard<std::mutex> lock(m_mutex);
                for (auto* item : m_pool) {
                    delete item;
                }
                m_pool.clear();
            }

            size_t size() const {
               // std::lock_guard<std::mutex> lock(m_mutex);
                return m_pool.size();
            }
        };
        void processBatchAdd(const std::vector<Core::ECS::Actor*>& actors);
        void processBatchRemove(const std::vector<Core::ECS::Actor*>& actors);
        void processBatchModify(const std::vector<Core::ECS::Actor*>& actors);
        /** Puts the items that stand for features in the order the features are built
         * in - the order of the body, which is the chain - instead of the order they
         * happened to be added to the tree in (see the definition). */
        void sortFeatureItems();
        QStandardItem* createItemFromActor(Core::ECS::Actor* actor);
        void addActorToTree(Core::ECS::Actor* actor, QStandardItem* parent = nullptr);
        void removeActorFromTree(Core::ECS::Actor* actor);
        void updateActorInTree(Core::ECS::Actor* actor);

        // 辅助函数
        Core::ECS::Actor* getActorFromItem(QStandardItem* item);
        void updateTopoShapeRecursive(Core::ECS::Actor* actor);
       

        //struct Internal;
        //std::unique_ptr<Internal> m_internal;

        // 🔥 新成员变量
        ItemPool m_itemPool;
        std::vector<PendingOperation> m_pendingOps;
        QTimer* m_pendingTimer = nullptr;
        bool m_batchMode = false;
        bool m_isProcessing = false;
        int m_batchCounter = 0;

        // 性能统计（可选）
        struct Statistics {
            std::atomic<int> totalCreated{ 0 };
            std::atomic<int> totalRemoved{ 0 };
            std::atomic<int> poolHitCount{ 0 };
            std::atomic<int> poolMissCount{ 0 };
        } m_stats;
	private:
		class EntityTreeModelInternal;
		EntityTreeModelInternal* mInternal = nullptr;
	};
}
