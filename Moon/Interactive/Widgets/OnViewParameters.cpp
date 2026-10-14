#include "Interactive/Widgets/OnViewParameters.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "Core/Global/ServiceLocator.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace MOON
{
	namespace
	{
		/** The widget the boxes are children of: the viewport they are drawn over. The
		 * tools are configured once the workspace is up, so this is only a safety net. */
		QWidget* BoxParent()
		{
			if (!::Core::Global::ServiceLocator::IsProvided<::MOON::ViewerWidget>()) {
				return nullptr;
			}
			return static_cast<QWidget*>(&GetService(::MOON::ViewerWidget));
		}

		/** The text of a value, short enough to be read and to be typed over. */
		QString TextOf(double p_value)
		{
			return QString::number(p_value, 'g', 6);
		}
	}

	OnViewParameter::OnViewParameter(std::string p_label) : m_label(std::move(p_label))
	{
	}

	void OnViewParameter::buildWidget(QWidget* p_parent)
	{
		if (p_parent == nullptr || m_frame != nullptr) {
			return;
		}

		m_frame = new QWidget(p_parent);
		// The mouse goes straight through the box and into the viewport under it, the way
		// FreeCAD's spin box does: the box is there to be typed in, and clicking next to it
		// is still how the geometry is placed with the cursor.
		m_frame->setAttribute(Qt::WA_TransparentForMouseEvents);
		m_frame->setStyleSheet(
			"QWidget { background: rgba(38, 38, 43, 210); border-radius: 3px; }"
			"QLabel { color: #c8c8cc; }"
			"QLineEdit { background: rgba(22, 22, 26, 220); color: #ffffff;"
			" border: 1px solid #6c6c74; border-radius: 2px; padding: 1px 3px; }");

		auto* layout = new QHBoxLayout(m_frame);
		layout->setContentsMargins(6, 2, 6, 2);
		layout->setSpacing(6);
		layout->addWidget(new QLabel(QString::fromStdString(m_label), m_frame));

		m_edit = new QLineEdit(m_frame);
		m_edit->setFixedWidth(78);
		m_edit->installEventFilter(this);
		layout->addWidget(m_edit);

		// textEdited, not textChanged: it is only what the user types that makes the value
		// his (the tool goes on writing the live value into the box until then).
		QObject::connect(m_edit, &QLineEdit::textEdited, this, [this]() {
			onTextEdited();
		});
		QObject::connect(m_edit, &QLineEdit::returnPressed, this, [this]() {
			onReturnPressed();
		});

		valueToText();
		m_frame->adjustSize();
		m_frame->hide();
	}

	void OnViewParameter::valueToText()
	{
		if (m_edit == nullptr) {
			return;
		}
		const QString text = TextOf(m_value);
		if (m_edit->text() == text) {
			return;
		}
		m_settingText = true;
		m_edit->setText(text);
		m_settingText = false;
	}

	bool OnViewParameter::valueFromText(double& p_out) const
	{
		if (m_edit == nullptr) {
			return false;
		}
		bool ok = false;
		const double parsed = m_edit->text().trimmed().toDouble(&ok);
		if (!ok || !std::isfinite(parsed)) {
			return false;
		}
		p_out = parsed;
		return true;
	}

	void OnViewParameter::onTextEdited()
	{
		if (m_settingText) {
			return;
		}
		// From the first keystroke on the text is the user's: the tool's live value is no
		// longer written into it, and the value (as far as it is a number yet) drives the
		// geometry.
		m_isSet = true;
		m_hasFinishedEditing = false;
		double value = m_value;
		if (valueFromText(value)) {
			m_value = value;
		}
		notifyEdited();
	}

	void OnViewParameter::onReturnPressed()
	{
		double value = m_value;
		if (!valueFromText(value)) {
			// Nothing that can be read as a number: the box goes back to showing the value
			// the tool has, and the parameter is not confirmed.
			m_isSet = false;
			valueToText();
			return;
		}
		m_value = value;
		m_isSet = true;
		m_hasFinishedEditing = true;
		notifyConfirmed();
	}

	void OnViewParameter::notifyEdited()
	{
		if (m_owner != nullptr) {
			m_owner->parameterEdited(*this);
		}
	}

	void OnViewParameter::notifyConfirmed()
	{
		if (m_owner != nullptr) {
			m_owner->parameterConfirmed(*this);
		}
	}

	void OnViewParameter::notifyReleased()
	{
		if (m_owner != nullptr) {
			m_owner->parameterReleased(*this);
		}
	}

	bool OnViewParameter::eventFilter(QObject* p_watched, QEvent* p_event)
	{
		if (p_watched == m_edit && p_event->type() == QEvent::KeyPress) {
			auto* key = static_cast<QKeyEvent*>(p_event);
			if (key->key() == Qt::Key_Escape) {
				// The mouse takes this parameter back.
				release();
				notifyReleased();
				return true;
			}
			if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
				// Tab walks through the boxes of the step, the way it does in FreeCAD.
				if (m_owner != nullptr) {
					m_owner->focusNextUnfinished();
				}
				return true;
			}
		}
		return QObject::eventFilter(p_watched, p_event);
	}

	void OnViewParameter::setValue(double p_value)
	{
		m_value = p_value;
		if (!m_isSet) {
			valueToText();
		}
	}

	void OnViewParameter::finish()
	{
		m_isSet = true;
		m_hasFinishedEditing = true;
	}

	void OnViewParameter::release()
	{
		m_isSet = false;
		m_hasFinishedEditing = false;
		if (m_edit != nullptr) {
			m_edit->clearFocus();
		}
		valueToText();
	}

	void OnViewParameter::setVisible(bool p_visible)
	{
		m_visible = p_visible;
	}

	void OnViewParameter::setScreenPosition(float p_x, float p_y)
	{
		m_x = p_x;
		m_y = p_y;
	}

	bool OnViewParameter::isEditing() const
	{
		return m_edit != nullptr && m_edit->hasFocus();
	}

	void OnViewParameter::update()
	{
		if (m_frame == nullptr) {
			return;
		}
		if (!m_visible) {
			m_frame->hide();
			return;
		}

		m_frame->move(static_cast<int>(m_x), static_cast<int>(m_y));
		if (!m_frame->isVisible()) {
			m_frame->show();
		}
		if (m_wantsFocus) {
			m_wantsFocus = false;
			m_edit->setFocus(Qt::OtherFocusReason);
		}
		if (!m_isSet) {
			// Nothing typed yet: the box shows the tool's live value, and the number stays
			// selected so that the first keystroke replaces it (FreeCAD's spin box selects
			// its number the same way).
			valueToText();
			if (m_edit->hasFocus()) {
				m_edit->setSelection(0, m_edit->text().size());
			}
		}
		m_isEditing = m_edit->hasFocus();
	}

	OnViewParameter& OnViewParameters::add(const std::string& p_label)
	{
		m_parameters.emplace_back(std::make_unique<OnViewParameter>(p_label));
		OnViewParameter& parameter = *m_parameters.back();
		parameter.m_owner = this;
		parameter.buildWidget(BoxParent());
		return parameter;
	}

	void OnViewParameters::clear()
	{
		m_parameters.clear();
	}

	OnViewParameter* OnViewParameters::at(int p_index)
	{
		if (p_index < 0 || p_index >= size()) {
			return nullptr;
		}
		return m_parameters[p_index].get();
	}

	const OnViewParameter* OnViewParameters::at(int p_index) const
	{
		if (p_index < 0 || p_index >= size()) {
			return nullptr;
		}
		return m_parameters[p_index].get();
	}

	int OnViewParameters::indexOf(const OnViewParameter& p_parameter) const
	{
		for (int i = 0; i < size(); i++) {
			if (m_parameters[i].get() == &p_parameter) {
				return i;
			}
		}
		return -1;
	}

	void OnViewParameters::parameterEdited(OnViewParameter& p_parameter)
	{
		const int index = indexOf(p_parameter);
		if (index >= 0 && onValueChanged) {
			onValueChanged(index, p_parameter.value());
		}
	}

	void OnViewParameters::parameterConfirmed(OnViewParameter& p_parameter)
	{
		const int index = indexOf(p_parameter);
		if (index >= 0 && onEditingFinished) {
			onEditingFinished(index, p_parameter.value());
		}
	}

	void OnViewParameters::parameterReleased(OnViewParameter& p_parameter)
	{
		const int index = indexOf(p_parameter);
		if (index >= 0 && onReleased) {
			onReleased(index);
		}
	}

	bool OnViewParameters::isEditing() const
	{
		for (const auto& parameter : m_parameters) {
			if (parameter->isEditing()) {
				return true;
			}
		}
		return false;
	}

	bool OnViewParameters::focusFirstUnfinished()
	{
		for (auto& parameter : m_parameters) {
			if (parameter->isVisible() && !parameter->hasFinishedEditing()) {
				parameter->setFocus();
				return true;
			}
		}
		return false;
	}

	bool OnViewParameters::focusNextUnfinished()
	{
		// From the one that has the keyboard, then wrapping around to the first: what Tab
		// does in FreeCAD (passFocusToNextOnViewParameter).
		int start = 0;
		for (int i = 0; i < size(); i++) {
			if (m_parameters[i]->isEditing()) {
				start = i + 1;
				break;
			}
		}
		for (int step = 0; step < size(); step++) {
			const int i = (start + step) % size();
			OnViewParameter& parameter = *m_parameters[i];
			if (parameter.isVisible() && !parameter.hasFinishedEditing()) {
				parameter.setFocus();
				return true;
			}
		}
		return false;
	}

	void OnViewParameters::update()
	{
		for (auto& parameter : m_parameters) {
			parameter->update();
		}
	}
}
