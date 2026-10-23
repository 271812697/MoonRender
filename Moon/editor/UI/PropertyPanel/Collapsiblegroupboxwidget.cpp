#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/PropertyQtWidgets.h"
#include "editor/Language.h"
#include "editor/Theme.h"
#include "Widgets/utils.h"
#include <QToolButton>
#include <QGridLayout>   // for QGridLayout
#include <QHBoxLayout>   // for QHBoxLayout
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
namespace MOON {
	namespace {
		// Property label whose text is elided with an ellipsis when it would
		// exceed the fixed label column width. The full name stays available in
		// the tooltip.
		class PropertyElidedLabel : public QLabel {
		public:
			PropertyElidedLabel(const QString& fullText, int maxWidth, QWidget* parent)
				: QLabel(fullText, parent)
				, fullText_(fullText)
				, maxWidth_(maxWidth) {
				setObjectName("PropertyLabel");
				setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				// The label must not push the editor column to the right: it
				// can always shrink down to the available width.
				setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
				setMaximumWidth(maxWidth_);
				setMinimumWidth(0);
				setToolTip(fullText);
			}

		protected:
			void showEvent(QShowEvent* event) override
			{
				QLabel::showEvent(event);
				refreshElidedText();
			}

			void resizeEvent(QResizeEvent* event) override
			{
				QLabel::resizeEvent(event);
				refreshElidedText();
			}

		private:
			void refreshElidedText()
			{
				const int available = std::max(0, std::min(maxWidth_, width()));
				if (fontMetrics().horizontalAdvance(fullText_) <= available) {
					setText(fullText_);
				}
				else {
					setText(fontMetrics().elidedText(fullText_, Qt::ElideRight, available));
				}
			}

			QString fullText_;
			int maxWidth_ = 0;
		};

		int propertyLabelMaxWidth(const QWidget* ref)
		{
			const auto em = ref->fontMetrics().boundingRect('M').width();
			return std::max(110, 12 * em);
		}
	}  // namespace

	class CollapsibleGroupBoxWidget::CollapsibleGroupBoxWidgetInternal {
	public:
		std::unique_ptr<QWidget> createPropertyLayoutWidget() {
			auto widget = std::make_unique<QWidget>();
			widget->setObjectName("CompositeContents");

			auto propertyLayout = std::make_unique<QGridLayout>();
			propertyLayout->setObjectName("PropertyWidgetLayout");
			propertyLayout->setAlignment(Qt::AlignTop);

			// Match Inviwo's property rows: a visible gap between the label
			// column and the editor column, with consistent margins and spacing.
			const auto space = refSpacePx(widget.get());
			propertyLayout->setContentsMargins(space * 2, space, space, space);
			propertyLayout->setHorizontalSpacing(space * 2);
			propertyLayout->setVerticalSpacing(space + 1);
			

			auto layout = propertyLayout.release();
			widget->setLayout(layout);

			// add default label to widget layout, this will also set the correct stretching and spacing
			//layout->addWidget(defaultLabel, 0, 0);
			layout->addItem(new QSpacerItem(0, 1, QSizePolicy::Fixed), 0, 1);
			layout->setColumnStretch(0, 0);
			layout->setColumnStretch(1, 1);

			return widget;
		}
		void updateFocusPolicy() {
			mSelf->setFocusPolicy(btnCollapse_->focusPolicy());
			mSelf->setFocusProxy(btnCollapse_);
		}
		CollapsibleGroupBoxWidgetInternal(CollapsibleGroupBoxWidget* self) :mSelf(self),
			
			propertyWidgetGroup_ { createPropertyLayoutWidget().release() }
		, propertyWidgetGroupLayout_{ static_cast<QGridLayout*>(propertyWidgetGroup_->layout()) } {
			btnCollapse_=new QToolButton(mSelf);
			btnCollapse_->setCheckable(true);
			btnCollapse_->setChecked(false);
			btnCollapse_->setObjectName("collapseButton");
			btnCollapse_->setFocusPolicy(Qt::StrongFocus);
			btnCollapse_->setStyleSheet(R"(
        QToolButton {
            border: none;                  /* 移除所有边框 */
            background-color: transparent; /* 背景透明（可选） */
            padding: 5px;                  /* 内边距，避免文字贴边 */
        }
        /* 可选：移除焦点时的虚线边框 */
        QToolButton:focus {
            outline: none;
        }
    )");
			btnCollapse_->setIcon(QIcon(":/widgets/icons/arrow_right.svg"));
			label_ = new QLabel("set", mSelf);
			label_->setObjectName("GroupTitle");
			label_->setProperty("moonTitle", true);
			label_->setStyleSheet(QString("color: %1;").arg(MOON::Theme::Tokens().text.name(QColor::HexRgb)));
			updateFocusPolicy();
			auto* headingWidget = new QWidget(mSelf);
			headingWidget->setObjectName("GroupTitleBar");
			QHBoxLayout* heading = new QHBoxLayout(headingWidget);
			heading->setContentsMargins(0, 0, 0, 0);
			heading->addWidget(btnCollapse_);
			heading->setSpacing(5);
			heading->addWidget(label_);
			QVBoxLayout* layout = new QVBoxLayout();
			
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);
			layout->addWidget(headingWidget);
			layout->addWidget(propertyWidgetGroup_);
			mSelf->setLayout(layout);

			// The two plates come from the theme (surface and a lifted variant), so the group
			// box reads the same in the dark and the light scheme.
			mSelf->setProperty("moonStyle", QStringLiteral(R"(
			QWidget#GroupTitleBar {
			background: @border;
			border-bottom: 1px solid @border;
			}

			QWidget#CompositeContents {
			background: @surface;
			}
			)"));
			mSelf->setStyleSheet(MOON::Theme::Style(mSelf->property("moonStyle").toString()));
		}
		~CollapsibleGroupBoxWidgetInternal() {

		}
		void setCollapsed(bool collapse) {
			mSelf->setUpdatesEnabled(false);
			propertyWidgetGroup_->setVisible(!collapse);
			btnCollapse_->setChecked(collapse);
			btnCollapse_->setIcon(
				collapse ? QIcon(":/widgets/icons/arrow_right.svg") : QIcon(":/widgets/icons/arrow_down.svg"));
			mSelf->setUpdatesEnabled(true);
		}
	private:
		QLabel* defaultLabel_;
		friend class CollapsibleGroupBoxWidget;

		CollapsibleGroupBoxWidget* mSelf = nullptr;
		QToolButton* btnCollapse_;
		QLabel* label_;
		QWidget* propertyWidgetGroup_;
		QGridLayout* propertyWidgetGroupLayout_;
		std::vector<WidgetProperty*> properties_;
		std::vector<PropertyQtWidget*> propertyWidgets_;
	};
	CollapsibleGroupBoxWidget::CollapsibleGroupBoxWidget(const QString& name,QWidget* parent):QWidget(parent),mInternal(new CollapsibleGroupBoxWidgetInternal(this))
	{
		mInternal->label_->setText(name);
		connect(mInternal->btnCollapse_, &QToolButton::toggled, this, &CollapsibleGroupBoxWidget::setCollapsed);
		mInternal->setCollapsed(true);
	}
	CollapsibleGroupBoxWidget::~CollapsibleGroupBoxWidget()
	{
		delete mInternal;
	}
	void CollapsibleGroupBoxWidget::addProperty(WidgetProperty* tmpProperty)
	{
		insertProperty(tmpProperty, mInternal->properties_.size());
	}
	void CollapsibleGroupBoxWidget::insertProperty(WidgetProperty* prop, size_t index)
	{
		setUpdatesEnabled(false);
		mInternal->propertyWidgetGroupLayout_->setEnabled(false);


		const size_t insertIndex = std::min(index, mInternal->properties_.size());
		const bool insertAtEnd = (insertIndex == mInternal->properties_.size());

		auto insertPoint = mInternal->properties_.begin() + insertIndex;
		auto widgetInsertPoint = mInternal->propertyWidgets_.begin() + insertIndex;

		mInternal->properties_.insert(insertPoint, prop);
		

		//auto factory = InviwoApplication::getPtr()->getPropertyWidgetFactory();
		if (auto propertyWidget =prop->createEditorWidget(this)) {
			mInternal->propertyWidgets_.insert(widgetInsertPoint, propertyWidget);

			insertPropertyWidget(prop->getPropertyName(), propertyWidget, insertAtEnd);
			
			//RenderContext::getPtr()->activateDefaultRenderContext();

			// need to re-set tab order for all following widgets to ensure tab order is correct
			// (see http://doc.qt.io/qt-5/qwidget.html#setTabOrder)
			for (auto wit = mInternal->propertyWidgets_.begin() + 1; wit != mInternal->propertyWidgets_.end(); ++wit) {
				setTabOrder(*(wit - 1), *wit);
			}
		}
		else {
			//log::warn("Could not find a widget for property: {}", prop->getClassIdentifier());
			// insert empty element to keep property widget vector in sync with property vector
			mInternal->propertyWidgets_.insert(widgetInsertPoint, nullptr);
		}
		mInternal->propertyWidgetGroupLayout_->setEnabled(true);
		setUpdatesEnabled(true);
	}
	void CollapsibleGroupBoxWidget::insertPropertyWidget(const QString& label, PropertyQtWidget* propertyWidget, bool insertAtEnd)
	{
		auto addPropertyWidget = [&](QGridLayout* layout, int row, PropertyQtWidget* widget) {
			//if (auto collapsibleWidget = dynamic_cast<CollapsibleGroupBoxWidgetQt*>(widget)) {
			//	collapsibleWidget->setNestedDepth(this->getNestedDepth() + 1);
				// make the collapsible widget go all the way to the right border
				//layout->addWidget(widget, row, 0, 1, 2);
			//}
			//else {  // not a collapsible widget
				//widget->setNestedDepth(this->getNestedDepth());
				// property widget should only be added to the left column of the layout
			// The panel's parameter names are authored in English; the two spellings are kept on
			// the label so a language switch rewrites it in place (see Language::Apply).
			auto* labelWidget = new PropertyElidedLabel(
				MOON::Language::Localize(label),
				propertyLabelMaxWidth(this),
				this
			);
			labelWidget->setProperty("moonZh", MOON::Language::Localize(label));
			labelWidget->setProperty("moonEn", label);
			layout->addWidget(labelWidget, row, 0);
			layout->addWidget(widget, row, 1);
			//}

			//if (isChildRemovable()) {
				//addButtonLayout(layout, row, widget->getProperty());
			//}

			//widget->setParentPropertyWidget(this);
			//widget->initState();
			};
		if (insertAtEnd) {
			// append property widget
			addPropertyWidget(mInternal->propertyWidgetGroupLayout_, mInternal->propertyWidgetGroupLayout_->rowCount(),
				propertyWidget);
		}
		else {
	
		}
	}
	void CollapsibleGroupBoxWidget::addSubWidget(QWidget* widget)
	{
		// Embed a full-width widget (e.g. a nested collapsible group) spanning
		// both columns of the property grid, without a property label.
		mInternal->propertyWidgetGroupLayout_->addWidget(
			widget, mInternal->propertyWidgetGroupLayout_->rowCount(), 0, 1, 2);
	}
	QSize CollapsibleGroupBoxWidget::sizeHint() const
	{
		QSize size = layout()->sizeHint();
		const auto em = fontMetrics().boundingRect('M').width();

		size.setWidth(std::max(static_cast<int>(20 * em), size.width()));
		return size;
	}
	QSize CollapsibleGroupBoxWidget::minimumSizeHint() const
	{
		QSize size = layout()->sizeHint();
		QSize minSize = layout()->minimumSize();
		const auto em = fontMetrics().boundingRect('M').width();
		size.setWidth(
			std::max(static_cast<int>(20 * em), minSize.width()));
		return size;
	}
	void CollapsibleGroupBoxWidget::setCollapsed(bool v) {
		mInternal->setCollapsed(v);
	}
}
