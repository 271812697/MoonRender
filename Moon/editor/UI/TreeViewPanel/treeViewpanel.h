#pragma once
#include <QtWidgets/QTreeView>
#include <QModelIndex>
#include <QStandardItem>
namespace Core::ECS {
	class Actor;
}
namespace MOON {
	class TreeViewPanel;
	class Feature;
	
	class TreeViewPanel : public QTreeView
	{
		Q_OBJECT
	public:
		enum OperationType
		{
			Add,
			Remove,
			Update
		};
		struct Operation
		{
			OperationType type;
			std::vector<Core::ECS::Actor*> actors;
		};
		TreeViewPanel(QWidget* parent);
		~TreeViewPanel();
		void updateTreeViewSketcherRoot();
		void addActorToTree(const std::vector<Core::ECS::Actor*>& actor);
		void removeActorFromTree(const std::vector<Core::ECS::Actor*>& actor);
		void updateActorInTree(const std::vector<Operation>&operations);
	signals:
		void setSelectActor(Core::ECS::Actor* actor);
		void itemHovered(Core::ECS::Actor* actor);   // 悬浮
		void itemLeave(Core::ECS::Actor* actor);
		void selectFeature(void* feature);
	public slots:
		void updateTreeViewSceneRoot();
		// 外部调用：根据 Actor 指针高亮 TreeView 项
		void highlightByActor(Core::ECS::Actor* actor);
		// 清空高亮
		void clearHighlight();	
		void clearLastHoverIndex();
	public:
		QModelIndex m_highlightIndex; // 用来保存当前高亮index

	protected:
		
		void mousePressEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;

	private:
		/** The menu an item offers: deleting the actor it stands for. */
		void onContextMenu(const QPoint& p_pos);
		/** True when the item may be deleted: a feature, or an actor that belongs to
		 * no feature. The nodes under a feature - its render anchors, the solid and
		 * shell groups and the face/edge leaves - are how the feature is displayed,
		 * and the menu keeps the entry turned off for them. */
		bool canDeleteActor(Core::ECS::Actor* p_actor) const;
		void deleteActor(Core::ECS::Actor* p_actor);
		/** Deletes a feature together with every feature built on top of it. */
		void deleteFeatureChain(Feature* p_feature);
		/** Takes what was selected out of the selection and the property panel when
		 * the actors behind it are gone. */
		void refreshSelectionAfterRemoval();
		class TreeViewPanelInternal;
		TreeViewPanelInternal* mInternal;
	};
}
