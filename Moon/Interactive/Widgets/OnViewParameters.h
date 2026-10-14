#pragma once
#include <QObject>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QLineEdit;
class QWidget;
class QEvent;

namespace MOON
{
	class OnViewParameters;

	/** One numeric input drawn over the viewport: FreeCAD's "on-view parameter", which is
	 * what lets a drawing tool be given a number instead of a mouse position (the spin
	 * boxes FreeCAD shows next to the geometry while a tool is running).
	 *
	 * It is a Qt widget over the viewport, the way FreeCAD's is - a spin box that is a child
	 * of the 3D view, sitting where the geometry is:
	 *   - the mouse goes straight through it (WA_TransparentForMouseEvents), so placing
	 *     geometry with the cursor keeps working while the box holds the keyboard;
	 *   - the tool says where it belongs (setScreenPosition) and for which step it is shown
	 *     (setVisible), and asks for the keyboard to be put in it (setFocus) when a step is
	 *     entered, so a number can be typed straight away;
	 *   - typing goes to the box, not to the tool: keys the box consumes never reach the
	 *     viewport, so a tool's shortcuts stay out of the way by themselves.
	 *
	 * Two flags say what the tool has to do with the value:
	 *   - isSet: the user has typed something. From then on that value drives the geometry
	 *     and the mouse is ignored for this parameter, until it is released again (escape,
	 *     release(), or the tool resetting it). While it is *not* set, the tool's live value
	 *     is what the box shows, so it follows the mouse;
	 *   - hasFinishedEditing: the value was confirmed (enter). A tool that has all the values
	 *     of the step it is in confirmed is done with that step and goes on to the next one
	 *     on its own, without a click.
	 */
	class OnViewParameter : public QObject
	{
	public:
		explicit OnViewParameter(std::string p_label);
		~OnViewParameter() override = default;

		const std::string& label() const { return m_label; }

		double value() const { return m_value; }
		/** Puts a value in the box without it counting as typed in: the mouse goes on
		 * driving the parameter (this is how the tool shows the live value). */
		void setValue(double p_value);

		bool isSet() const { return m_isSet; }
		bool hasFinishedEditing() const { return m_hasFinishedEditing; }

		/** The value is typed and confirmed: the tool takes it and the mouse does not touch
		 * this parameter any more. */
		void finish();
		/** Gives the parameter back to the mouse. */
		void release();

		void setVisible(bool p_visible);
		bool isVisible() const { return m_visible; }
		void setScreenPosition(float p_x, float p_y);

		/** True while the keyboard is inside this box. */
		bool isEditing() const;
		/** True when the box is waiting for the keyboard (what the next update does). */
		bool wantsFocus() const { return m_wantsFocus; }
		/** Asks for the keyboard to be put in this box at the next update. */
		void setFocus() { m_wantsFocus = true; }

	protected:
		bool eventFilter(QObject* p_watched, QEvent* p_event) override;

	private:
		friend class OnViewParameters;

		/** Builds the widget, as a child of the viewport it is drawn over. */
		void buildWidget(QWidget* p_parent);
		/** Puts the box where the tool wants it and shows it (or hides it), gives it the
		 * keyboard when it asked for it, and keeps its text at the tool's live value while
		 * the user has not typed in it. */
		void update();
		/** A keystroke: the text only becomes the user's from here on. */
		void onTextEdited();
		/** Enter: the value stands for the tool. */
		void onReturnPressed();
		/** Tells the set (which holds the tool's callbacks) what happened. */
		void notifyEdited();
		void notifyConfirmed();
		void notifyReleased();
		/** The number in the box, as text (only while nothing has been typed). */
		void valueToText();
		/** Parses what is typed. @return false when it is not a number. */
		bool valueFromText(double& p_out) const;

		std::string m_label;
		double m_value = 0.0;
		bool m_isSet = false;
		bool m_hasFinishedEditing = false;
		bool m_visible = true;
		float m_x = 0.0f;
		float m_y = 0.0f;
		bool m_isEditing = false;
		bool m_wantsFocus = false;
		bool m_settingText = false;   // a change of the text that is not the user's
		/** The set this box belongs to; it holds the tool's callbacks. */
		OnViewParameters* m_owner = nullptr;
		/** The strip the box is: its name, and the number that can be typed in. */
		QWidget* m_frame = nullptr;
		QLineEdit* m_edit = nullptr;
	};

	/** The set of on-view parameters of one running tool.
	 *
	 * The tool decides which of them belong to the step it is in (see
	 * DrawSketchDefaultHandler::stateOfOnViewParameter) and puts them up from its own
	 * per-frame update; this class only knows how to show one box per parameter, where the
	 * tool says, and how to tell the tool what the user did with it.
	 */
	class OnViewParameters
	{
	public:
		/** A parameter was typed in. The index is the one it was added with. */
		std::function<void(int, double)> onValueChanged;
		/** A value was confirmed (enter). */
		std::function<void(int, double)> onEditingFinished;
		/** The input was given back to the mouse (escape). */
		std::function<void(int)> onReleased;

		/** Adds a box. The widgets are children of the viewport. */
		OnViewParameter& add(const std::string& p_label);
		void clear();

		int size() const { return static_cast<int>(m_parameters.size()); }
		OnViewParameter* at(int p_index);
		const OnViewParameter* at(int p_index) const;

		/** Puts the boxes where the tool set them, shows the ones the tool turned on, gives
		 * the keyboard to the one that asked for it, and keeps the values of the ones that
		 * have not been typed in at the tool's live value. Call it once per frame. */
		void update();

		bool isEditing() const;
		/** Puts the keyboard in the first parameter that has not been finished yet. */
		bool focusFirstUnfinished();
		/** Moves the keyboard to the next parameter that has not been finished yet,
		 * wrapping around; @return false when there is none. */
		bool focusNextUnfinished();

	private:
		friend class OnViewParameter;

		/** What one of the boxes did, told to the tool through the callbacks above. */
		void parameterEdited(OnViewParameter& p_parameter);
		void parameterConfirmed(OnViewParameter& p_parameter);
		void parameterReleased(OnViewParameter& p_parameter);
		/** @return where p_parameter sits in the set, or -1. */
		int indexOf(const OnViewParameter& p_parameter) const;

		std::vector<std::unique_ptr<OnViewParameter>> m_parameters;
	};
}
