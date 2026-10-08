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
