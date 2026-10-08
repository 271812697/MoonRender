#pragma once
#include "Interactive/Interactive/AbstractWidget.h"
#include <string>
namespace Editor {
	namespace Panels {
		class SceneView;
	}
}
namespace MOON
{
	class ImRenderer;
	class EventWidget : public AbstractWidget
	{
	public:
		EventWidget(const std::string& name);
		virtual ~EventWidget();
		unsigned int getWidgetId() const { return mWidgetId; }
		const std::string& getName() const { return mName; }
		bool isActived() const { return mActive; }
		bool isVisible() const { return mVisible; }
		void setActive(bool flag);
		void setVisible(bool flag);
		void setImmediateInvoke(bool flag);
		/** Widgets are drawn in ascending order of this value. A widget that has to
		 * cover another one says so here - the drawing tools paint over the sketch
		 * they are editing - instead of racing it for the last word on a primitive
		 * both of them draw (the drawing order used to be whatever the widget
		 * container happened to hand out). Everything else keeps the default. */
		int getDrawOrder() const { return mDrawOrder; }
		void setDrawOrder(int order) { mDrawOrder = order; }
		void update();
		virtual void onUpdate();
		virtual void onSetActive(bool flag);

		virtual void onLeftMousePressed();
		virtual void onLeftMouseReleased();
		virtual void onRightMousePressed();
		virtual void onRightMouseReleased();
		virtual void onMouseMove();
		virtual void onKeyPress(const std::string& key);
		virtual void onKeyRelease(const std::string& key);
		void SetEnabled(int) override;
		static void LeftMousePressed(AbstractWidget*);
		static void LeftMouseReleased(AbstractWidget*);
		static void RightMouseReleased(AbstractWidget*);
		static void RightMousePressed(AbstractWidget*);
		static void MouseMove(AbstractWidget*);
	protected:
		//use for mouse move event
		unsigned int mCurrentFrame = 1;
		unsigned int mPreFrame = 0;
		unsigned int mWidgetId;
		CallbackCommand* KeyEventCallbackCommand;
		static void ProcessKeyEvents(EventObject*, unsigned long, void*, void*);
		std::string mName;	
		//mActive 
		bool mActive = true;
		bool mVisible = true;
		bool mPreflag = false;
		bool mCurflag = false;
		bool mImInvoke = true;
		int mDrawOrder = 0;
		ImRenderer* renderer= nullptr;
		Editor::Panels::SceneView* m_sceneView = nullptr;
	};
}
