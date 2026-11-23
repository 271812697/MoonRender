#pragma once
#include <Rendering/Overlay/Interactive/EventObject.h>

namespace MOON {
	class EventMap;
	class RenderWindowInteractor;
	class CallbackCommand;
	class GizmoEvent;
	class AbstractWidget;
	class GizmoEventData;

	// This is a lightweight class that should be used internally by the widgets
	class WidgetEventTranslator : public EventObject
	{
	public:
		/**
		 * Instantiate the object.
		 */
		static WidgetEventTranslator* New();


		///@{
		/**
		 * Use these methods to create the translation from a  event to a widget
		 * event. Specifying WidgetEvent::NoEvent or an empty
		 * string for the (toEvent) erases the mapping for the event.
		 */
		void SetTranslation(unsigned long GIZMOEvent, unsigned long widgetEvent);
		void SetTranslation(const char* GIZMOEvent, const char* widgetEvent);
		void SetTranslation(unsigned long GIZMOEvent, int modifier, char keyCode, int repeatCount,
			const char* keySym, unsigned long widgetEvent);
		void SetTranslation(GizmoEvent* GIZMOevent, unsigned long widgetEvent);
		void SetTranslation(unsigned long GIZMOEvent, GizmoEventData* edata, unsigned long widgetEvent);
		///@}

		///@{
		/**
		 * Translate a  event into a widget event. If no event mapping is found,
		 * then the methods return WidgetEvent::NoEvent or a nullptr string.
		 */
		unsigned long GetTranslation(unsigned long GIZMOEvent);
		const char* GetTranslation(const char* GIZMOEvent);
		unsigned long GetTranslation(
			unsigned long GIZMOEvent, int modifier, char keyCode, int repeatCount, const char* keySym);
		unsigned long GetTranslation(unsigned long GIZMOEvent, GizmoEventData* edata);
		unsigned long GetTranslation(GizmoEvent* GIZMOEvent);
		///@}

		///@{
		/**
		 * Remove translations for a binding.
		 * Returns the number of translations removed.
		 */
		int RemoveTranslation(
			unsigned long GIZMOEvent, int modifier, char keyCode, int repeatCount, const char* keySym);
		int RemoveTranslation(GizmoEvent* e);
		int RemoveTranslation(GizmoEventData* e);
		int RemoveTranslation(unsigned long GIZMOEvent);
		int RemoveTranslation(const char* GIZMOEvent);
		///@}

		/**
		 * Clear all events from the translator (i.e., no events will be
		 * translated).
		 */
		void ClearEvents();

		///@{
		/**
		 * Add the events in the current translation table to the interactor.
		 */
		void AddEventsToParent(AbstractWidget*, CallbackCommand*, float priority);
		void AddEventsToInteractor(RenderWindowInteractor*, CallbackCommand*, float priority);
		///@}

	protected:
		// Constructors/destructors made public for widgets to use
		WidgetEventTranslator();
		~WidgetEventTranslator() override;

		// Map events to widget events
		EventMap* eventMap;

		// Used for performance reasons to avoid object construction/deletion
		GizmoEvent* Event;

	private:
		WidgetEventTranslator(const WidgetEventTranslator&) = delete;
		void operator=(const WidgetEventTranslator&) = delete;
	};

}