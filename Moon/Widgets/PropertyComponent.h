#pragma once
#include<vector>
#include <QString>
#include <QVariant>
class QWidget;
namespace Core::ECS::Components
{
	class AComponent;
}
namespace MOON {
	class WidgetProperty;
	class PropertyComponent
	{
	public:
		PropertyComponent();
		virtual ~PropertyComponent();
		void addProperty(WidgetProperty* prop);
		std::vector<WidgetProperty*>&getProperties();

		/** A component can render itself as one custom widget instead of a flat list of
		 * properties (the material component uses the same grouped form as the material
		 * designer). The panel only calls this once, right after construction. */
		virtual QWidget* createCustomWidget(QWidget* p_parent) { (void)p_parent; return nullptr; }

		virtual QVariant getPropertyValue(const QString& propertyName);
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value);
		void updateWidgetValue();
		virtual QString getComponentName();
	protected:
		std::vector<WidgetProperty*>mProperties;
	};
	class ActorPropertyComponent :public PropertyComponent
	{
	public:
		ActorPropertyComponent(Core::ECS::Components::AComponent* comp) ;
		virtual ~ActorPropertyComponent() ;
		virtual QString getComponentName()override;
	protected:
		Core::ECS::Components::AComponent * component=nullptr;
	};
}