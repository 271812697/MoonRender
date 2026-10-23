#include "Widgets/ComboBox.h"
#include<QHBoxLayout>
#include "editor/Language.h"
namespace MOON {
	ComboBox::ComboBox(QWidget* parent, WidgetProperty* prop):PropertyQtWidget(parent,prop)
	{
		myCbox = new QComboBox(this);
		QHBoxLayout* hLayout = new QHBoxLayout();
		

		hLayout->addWidget(myCbox);

		hLayout->setContentsMargins(0, 0, 0, 0);
		setLayout(hLayout);
		connect(myCbox, &QComboBox::currentTextChanged, this, &ComboBox::onProvinceSelect);
	}
	void ComboBox::addComboList(const QList<QString>& list)
	{
		// Values are authored in English ("Through All", "UpToFace"); the widget stores the index,
		// so only the text is translated. Both spellings are kept on the combo box, which lets
		// Language::Apply rewrite the items when the language changes.
		QStringList pairs;
		for (int i = 0; i < list.size(); i++) {
			const QString english = list[i];
			const QString chinese = MOON::Language::Localize(english);
			myCbox->addItem(MOON::Language::Pick(chinese, english));
			pairs << chinese + "|" + english;
		}
		myCbox->setProperty("moonItems", pairs);
	}
	int ComboBox::getCurrentIndex()
	{
		return myCbox->currentIndex();
	}
	QVariant ComboBox::widgetValue() {
		return getCurrentIndex();
	}
	void ComboBox::setWidgetValue(const QVariant& value)
	{
		myCbox->setCurrentIndex(value.toInt());
	}
	void ComboBox::onProvinceSelect(const QString& selectText) {
	
		OnValueChanged();
	}
}