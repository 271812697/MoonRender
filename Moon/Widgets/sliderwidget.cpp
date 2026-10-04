#include "Widgets/sliderwidget.h"
#include "Widgets/numberlineedit.h"     // for NumberLineEdit

#include <cmath>   // for fabs
#include <limits>  // for numeric_limits

#include <QDoubleSpinBox>  // for QDoubleSpinBox
#include <QEvent>          // for QEvent, QEvent::MouseButtonRelease
#include <QHBoxLayout>     // for QHBoxLayout
#include <QMouseEvent>     // for QMouseEvent
#include <QPoint>          // for QPoint
#include <QSignalBlocker>  // for QSignalBlocker
#include <QSizePolicy>     // for QSizePolicy, QSizePolicy::Fixed
#include <QSlider>         // for QSlider
#include <QStyle>          // for QStyle
#include <QVariant>        // for QVariant
#include <Qt>      
namespace MOON {
    namespace {

        class Slider : public QSlider {
            using QSlider::QSlider;

            virtual void wheelEvent(QWheelEvent* e) override {
                if (hasFocus()) QSlider::wheelEvent(e);
            }
        };

    }  // namespace

    BaseSliderWidgetQt::BaseSliderWidgetQt(QWidget* parent,bool intMode)
        : PropertyQtWidget(parent)
        , spinBox_(new NumberLineEdit(intMode))
        , slider_(new Slider())
        , spinnerValue_(0.0)
        , sliderValue_(0)
        
        {

        QHBoxLayout* hLayout = new QHBoxLayout();

        slider_->setOrientation(Qt::Horizontal);
        slider_->setPageStep(1);
        slider_->setMaximum(sliderMax_);
        slider_->installEventFilter(this);
        slider_->setFocusPolicy(Qt::ClickFocus);
        slider_->setObjectName("PropertySlider");
        // Inviwo-style slim slider: thin groove with a circular draggable handle.
        slider_->setStyleSheet(R"(
            QSlider#PropertySlider::groove:horizontal {
                height: 5px;
                background: #65696d;
                border: none;
                border-radius: 2px;
            }
            QSlider#PropertySlider::sub-page:horizontal {
                height: 5px;
                background: #1e70a8;
                border: none;
                border-radius: 2px;
            }
            QSlider#PropertySlider::add-page:horizontal {
                height: 5px;
                background: #65696d;
                border: none;
                border-radius: 2px;
            }
            QSlider#PropertySlider::handle:horizontal {
                width: 16px;
                height: 16px;
                min-width: 16px;
                min-height: 16px;
                max-width: 16px;
                max-height: 16px;
                margin: -6px 0;
                border-radius: 8px;
                background: #c8ccd0;
                border: 1px solid #c8ccd0;
            }
            QSlider#PropertySlider::handle:horizontal:hover {
                background: #268bd2;
                border: 1px solid #268bd2;
            }
            QSlider#PropertySlider::handle:horizontal:disabled {
                background: #5d656b;
                border: 1px solid #5d656b;
            }
            QSlider#PropertySlider::sub-page:horizontal:disabled {
                background: #4f555b;
            }
            QSlider#PropertySlider::add-page:horizontal:disabled,
            QSlider#PropertySlider::groove:horizontal:disabled {
                background: #404246;
            }
        )");

        setFocusPolicy(spinBox_->focusPolicy());
        setFocusProxy(spinBox_);
        slider_->setFocusProxy(spinBox_);

        spinBox_->setKeyboardTracking(false);  // don't emit the valueChanged() signal while typing

        hLayout->addWidget(slider_);
        hLayout->addWidget(spinBox_);
        hLayout->setContentsMargins(0, 0, 0, 0);
        hLayout->setSpacing(refSpacePx(this));
        hLayout->setStretch(0, 3);
        hLayout->setStretch(1, 1);

        setLayout(hLayout);
        connect(slider_, &QSlider::valueChanged, this, &BaseSliderWidgetQt::updateFromSlider);
        connect(spinBox_, static_cast<void (QDoubleSpinBox::*)()>(&QDoubleSpinBox::editingFinished),
            this, &BaseSliderWidgetQt::updateFromSpinBox);
        connect(this, &BaseSliderWidgetQt::valueChanged, [this]() {
			OnValueChanged();
            });

        QSizePolicy sp = sizePolicy();
        sp.setVerticalPolicy(QSizePolicy::Fixed);
        setSizePolicy(sp);
    }

    void BaseSliderWidgetQt::setWrapping(bool wrap) { spinBox_->setWrapping(wrap); }

    bool BaseSliderWidgetQt::wrapping() const { return spinBox_->wrapping(); }

    void BaseSliderWidgetQt::applyInit() {
        updateSlider();
        updateSpinBox();
    }

    void BaseSliderWidgetQt::applyValue() {
        applyInit();
        emit valueChanged();
    }
    void BaseSliderWidgetQt::applyMinValue() {
        QSignalBlocker spinBlock(spinBox_);
        QSignalBlocker slideBlock(slider_);


        {
            spinBox_->setMinimum(transformMinValueToSpinner());
        }
        slider_->setMinimum(transformMinValueToSlider());
        updateSlider();
        // The number box clamps what it is shown, so a value that arrived while the
        // range was still the widget's default has to be put back once the real
        // range is known.
        updateSpinBox();
    }
    void BaseSliderWidgetQt::applyMaxValue() {
        QSignalBlocker spinBlock(spinBox_);
        QSignalBlocker slideBlock(slider_);

       {
            spinBox_->setMaximum(transformMaxValueToSpinner());
        }
        slider_->setMaximum(transformMaxValueToSlider());
        updateSlider();
        updateSpinBox();
    }
    void BaseSliderWidgetQt::applyIncrement() {
        QSignalBlocker spinBlock(spinBox_);
        QSignalBlocker slideBlock(slider_);

        spinBox_->setIncrement(transformIncrementToSpinner());
        spinBox_->setDecimals(transformIncrementToSpinnerDecimals());
        slider_->setSingleStep(transformIncrementToSlider());
    }

    void BaseSliderWidgetQt::updateFromSlider() {
        const int newValue = slider_->value();
        if (newValue != sliderValue_) {
            sliderValue_ = newValue;
            updateOutOfBounds();
            newSliderValue(sliderValue_);
            updateSpinBox();
            emit valueChanged();
        }
    }

    void BaseSliderWidgetQt::updateFromSpinBox() {
        const double newValue = spinBox_->value();
        if (fabs(newValue - spinnerValue_) > std::numeric_limits<double>::epsilon()) {
            spinnerValue_ = newValue;
            newSpinnerValue(spinnerValue_);
            updateSlider();
            emit valueChanged();
        }
    }

    void BaseSliderWidgetQt::updateSpinBox() {
        QSignalBlocker spinBlock(spinBox_);

        spinnerValue_ = transformValueToSpinner();
        spinBox_->setValue(spinnerValue_);
    }

    void BaseSliderWidgetQt::updateSlider() {
        QSignalBlocker slideBlock(slider_);

        sliderValue_ = transformValueToSlider();
        updateOutOfBounds();

        slider_->setValue(sliderValue_);
    }

    void BaseSliderWidgetQt::updateOutOfBounds() {
        bool isOutOfBounds = (slider_->maximum() < sliderValue_ || slider_->minimum() > sliderValue_);
        if (isOutOfBounds != slider_->property("outOfBounds").toBool()) {
            slider_->setProperty("outOfBounds", isOutOfBounds);
            slider_->style()->unpolish(slider_);
            slider_->style()->polish(slider_);
        }
    }

    bool BaseSliderWidgetQt::eventFilter(QObject* watched, QEvent* event) {
        if (watched == slider_ && event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && !slider_->isSliderDown() && isEnabled()) {
                auto newPos = slider_->minimum() + static_cast<double>(me->pos().x()) *
                    (slider_->maximum() - slider_->minimum()) /
                    slider_->width();
                slider_->setValue(static_cast<int>(newPos));
                me->accept();
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }


}
