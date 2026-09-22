#pragma once
#include "PropertyWidget.h"
#include "core/ECS/Actor.h"
#include "core/ECS/Components/CMaterialRenderer.h"
#include "Core/ECS/Components/CPostProcessStack.h"
#include "Core/ECS/Components/CPointLight.h"
#include "core/ECS/Components/CDirectionalLight.h"
#include "core/component/CTopoShape.h"
#include "core/Resources/Material.h"
#include "Core/Global/ServiceLocator.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Widgets/PropertyComponent.h"
#include "Widgets/Property.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/FVec3Property.h"
#include "Widgets/FVec4Property.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/SliderIntProperty.h"
#include "Widgets/ColorPickerProperty.h"
#include "Widgets/TextureProperty.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "core/log.h"
#include <QTreeWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QColorDialog>
#include <QFontDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QStyleFactory>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <fstream>

namespace MOON {
	class TopoShapePropertyComponent :public ActorPropertyComponent
	{
	public:
		TopoShapePropertyComponent(Core::ECS::Components::CTopoShape* comp) :ActorPropertyComponent(comp) {
			mProperties.push_back(new EnumProperty("HighLight Mode", this));
			mProperties.push_back(new ColorPickerProperty("HoverColor", this));
			mProperties.push_back(new ColorPickerProperty("SeletedColor", this));
		}
		virtual ~TopoShapePropertyComponent() {

		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CTopoShape*>(component);
			auto& highLightOption = comp->getHightLightOption();
			if (propertyName == "HighLight Mode") {
				QList<QString>list = { "Color","Transparent" };
				return QVariant::fromValue(list);
			}else if (propertyName == "HoverColor") {
				auto color = highLightOption.hoverColor;
				return QVariant::fromValue(QColor(color.x * 255, color.y * 255, color.z * 255,color.w*255));
			}
			else if (propertyName == "SeletedColor") {
				auto color = highLightOption.hoverColor;
				return QVariant::fromValue(QColor(color.x * 255, color.y * 255, color.z * 255,color.w*255));
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto comp = dynamic_cast<Core::ECS::Components::CTopoShape*>(component);
			auto& highLightOption = comp->getHightLightOption();
			if (propertyName == "HighLight Mode") {
				comp->switchHighLightMode(static_cast<Core::ECS::Components::HighLightOption::Mode>(value.value<int>()));
			}
			else if (propertyName == "HoverColor") {
				auto color = value.value<QColor>();
				highLightOption.hoverColor={ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha()/255.0f};
			}
			else if (propertyName == "SeletedColor") {
				auto color = value.value<QColor>();
				highLightOption.selectColor = { color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha() / 255.0f };

         	}
		}
	};
	class PostProcessStackPropertyComponent :public ActorPropertyComponent
	{
	public:
		PostProcessStackPropertyComponent(Core::ECS::Components::CPostProcessStack* comp) :ActorPropertyComponent(comp) {
			mProperties.push_back(new BoolProperty("Bloom Enable",this));
			mProperties.push_back(new SliderFloatProperty("Bloom Intensity", this));
			mProperties.push_back(new SliderIntProperty("Bloom Pass Count", this));
			mProperties.push_back(new BoolProperty("FXAA Enable", this));
			mProperties.push_back(new BoolProperty("Tonemap Enable", this));
			mProperties.push_back(new SliderFloatProperty("Tonemap exposure", this));
			mProperties.push_back(new BoolProperty("Tonemap gamma", this));
			mProperties.push_back(new EnumProperty("Tonemap mode",this));
			mProperties.push_back(new BoolProperty("Exposure Enable", this));
		}
		virtual ~PostProcessStackPropertyComponent() {

		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CPostProcessStack*>(component);
			auto bloomSetting=comp->GetBloomSettings();
			auto fxaaSetting = comp->GetFXAASettings();
			auto tonemappingSetting = comp->GetTonemappingSettings();
			auto exposureSetting = comp->GetAutoExposureSettings();
			if (propertyName == "Bloom Intensity")
				return QVariant::fromValue(bloomSetting.intensity);
			else if (propertyName == "Bloom Pass Count")
				return QVariant::fromValue(bloomSetting.passes);
			else if (propertyName == "Bloom Enable") {
				return QVariant::fromValue(bloomSetting.enabled);
			}
			else if (propertyName == "FXAA Enable") {
				return QVariant::fromValue(fxaaSetting.enabled);
			}
			else if (propertyName == "Tonemap Enable") {
				return QVariant::fromValue(tonemappingSetting.enabled);
			}
			else if (propertyName == "Tonemap exposure") {
				return QVariant::fromValue(tonemappingSetting.exposure);
			}
			else if (propertyName == "Tonemap gamma") {
				return QVariant::fromValue(tonemappingSetting.gammaCorrection);
			}
			else if (propertyName == "Exposure Enable") {
				return QVariant::fromValue(exposureSetting.enabled);
			}
			else if (propertyName == "Tonemap mode") {
				QList<QString>list = {"NEUTRAL","REINHARD","REINHARD_JODIE","UNCHARTED2","UNCHARTED2_FILMIC","ACES"};
				return QVariant::fromValue(list);
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto comp = dynamic_cast<Core::ECS::Components::CPostProcessStack*>(component);
			auto bloomSetting = comp->GetBloomSettings();
			auto fxaaSetting = comp->GetFXAASettings();
			auto tonemappingSetting = comp->GetTonemappingSettings();
			auto exposureSetting = comp->GetAutoExposureSettings();

			if (propertyName == "Bloom Intensity") {
				bloomSetting.intensity = value.value<float>();
				comp->SetBloomSettings(bloomSetting);
			}
			else if (propertyName == "Bloom Pass Count") {
				bloomSetting.passes= value.value<int>();
				comp->SetBloomSettings(bloomSetting);
			}
			else if (propertyName == "Bloom Enable") {
				bloomSetting.enabled = value.value<bool>();
				comp->SetBloomSettings(bloomSetting);
			}
			else if (propertyName == "FXAA Enable")
			{
				fxaaSetting.enabled = value.value<bool>();
				comp->SetFXAASettings(fxaaSetting);
			}
			else if (propertyName == "Tonemap Enable") {
				tonemappingSetting.enabled = value.value<bool>();
				comp->SetTonemappingSettings(tonemappingSetting);
			}
			else if (propertyName == "Tonemap mode") {
				tonemappingSetting.mode = static_cast<Core::Rendering::PostProcess::ETonemappingMode>(value.value<int>());
				comp->SetTonemappingSettings(tonemappingSetting);
			}
			else if (propertyName== "Tonemap exposure") {
				tonemappingSetting.exposure = value.value<float>();
				comp->SetTonemappingSettings(tonemappingSetting);
			}
			else if ("Tonemap gamma") {
				tonemappingSetting.gammaCorrection = value.value<bool>();
				comp->SetTonemappingSettings(tonemappingSetting);
			}
			else if (propertyName == "Exposure Enable") {
				exposureSetting.enabled = value.value<bool>();
				comp->SetAutoExposureSettings(exposureSetting);
			}
			
		}
	};
	class TransFormPropertyComponent:public ActorPropertyComponent
	{
	public:
		TransFormPropertyComponent(Core::ECS::Components::CTransform* comp):ActorPropertyComponent(comp) {
			mProperties.push_back(new FVec3Property("position", this));
			mProperties.push_back(new FVec3Property("scale", this));
			mProperties.push_back(new FVec3Property("rotation", this));
		}
		virtual ~TransFormPropertyComponent() {

		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CTransform*>(component);
			if (propertyName == "position")
				return QVariant::fromValue(comp->GetWorldPosition());
			else if (propertyName == "scale")
				return QVariant::fromValue(comp->GetWorldScale());
			else if (propertyName == "rotation") {
				return QVariant::fromValue(comp->GetWorldRotation().EulerAngles());
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto comp = dynamic_cast<Core::ECS::Components::CTransform*>(component);
			if (propertyName == "position") {
				comp->SetWorldPosition(value.value<Maths::FVector3>());
			}
			else if (propertyName == "scale") {
				comp->SetWorldScale(value.value<Maths::FVector3>());
			}
			else if (propertyName == "rotation") {
				auto euler=value.value<Maths::FVector3>();
				comp->SetWorldRotation(Maths::FQuaternion(euler));
			}
			// A feature hands its shape to the features built on top of it together with
			// this transform (see Feature::getWorldTopoShape), so they have to be rebuilt
			// when it changes - otherwise only what is drawn would move.
			if (Feature* feature = dynamic_cast<Feature*>(&comp->owner)) {
				CORE_INFO(
					"[Feature] {0}: transform changed, rebuilding the features on top of it",
					feature->GetName());
				FeatureBody::instance().populateFeature(feature);
			}
		}
	};
	class PointLightPropertyComponent :public ActorPropertyComponent
	{
	public:
		PointLightPropertyComponent(Core::ECS::Components::CPointLight* comp) :ActorPropertyComponent(comp) {
			mProperties.push_back(new SliderFloatProperty("intensity", this));
			mProperties.push_back(new ColorPickerProperty("color", this));
		}
		virtual ~PointLightPropertyComponent() {
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CPointLight*>(component);
			if (propertyName== "intensity") {
				return QVariant::fromValue(comp->GetIntensity());
			}else if (propertyName == "color") {
				auto color=comp->GetColor();
				return QVariant::fromValue(QColor(color.x * 255, color.y * 255, color.z * 255));
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto comp = dynamic_cast<Core::ECS::Components::CPointLight*>(component);
			if (propertyName == "intensity") {
				comp->SetIntensity(value.value<float>());
			}
			else if (propertyName == "color") {
				auto color=value.value<QColor>();
				comp->SetColor({color.red()/255.0f,color.green() / 255.0f,color.blue() / 255.0f });
			}
		}
	};
	class DirectionLightPropertyComponent :public ActorPropertyComponent
	{
	public:
		DirectionLightPropertyComponent(Core::ECS::Components::CDirectionalLight* comp) :ActorPropertyComponent(comp) {
			mProperties.push_back(new SliderFloatProperty("intensity", this));
			mProperties.push_back(new ColorPickerProperty("color", this));
		}
		virtual ~DirectionLightPropertyComponent() {
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CDirectionalLight*>(component);
			if (propertyName == "intensity") {
				return QVariant::fromValue(comp->GetIntensity());
			}
			else if (propertyName == "color") {
				auto color = comp->GetColor();
				return QVariant::fromValue(QColor(color.x * 255, color.y * 255, color.z * 255));
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto comp = dynamic_cast<Core::ECS::Components::CDirectionalLight*>(component);
			if (propertyName == "intensity") {
				comp->SetIntensity(value.value<float>());
			}
			else if (propertyName == "color") {
				auto color = value.value<QColor>();
				comp->SetColor({ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f });
			}
		}
	};
	class MaterialPropertyComponent :public ActorPropertyComponent
	{
	public:
		MaterialPropertyComponent(Core::ECS::Components::CMaterialRenderer* comp) :ActorPropertyComponent(comp) {
			mat = comp->GetMaterialAtIndex(0);
			featureList = mat->GetShader()->GetFeatures();
			for (auto feature : featureList) {
				mProperties.push_back(new BoolProperty(feature.c_str(), this, BoolProperty::Style::InviwoRect));
			}
			for (auto& mprop:mat->GetProperties()) {
				if (std::holds_alternative<Maths::FVector3>(mprop.second.value)) {
					mProperties.push_back(new FVec3Property(mprop.first.c_str(), this));
				}
				else if (std::holds_alternative<float>(mprop.second.value)) {
					mProperties.push_back(new SliderFloatProperty(mprop.first.c_str(), this));
				}
				else if (std::holds_alternative<Maths::FVector4>(mprop.second.value)) {
					std::string mainLower = mprop.first;
					std::transform(mainLower.begin(), mainLower.end(), mainLower.begin(),
						[](unsigned char c) { return std::tolower(c); });
					if (mainLower.find("u_albedo") != std::string::npos) {
						mProperties.push_back(new ColorPickerProperty(mprop.first.c_str(), this));
					}
					else
					{
						mProperties.push_back(new FVec4Property(mprop.first.c_str(), this));
					}
				}
				else if (std::holds_alternative<Rendering::Resources::Texture*>(mprop.second.value)) {
					mProperties.push_back(new TextureProperty(mprop.first.c_str(), this));

				}
			}
		}
		virtual ~MaterialPropertyComponent() {

		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			std::string propName = propertyName.toStdString();
			auto ref = mat->GetProperty(propName);
			if (ref.has_value()) {
				if (std::holds_alternative<Maths::FVector3>(ref.value().value)) {
					auto v = std::get<Maths::FVector3>(ref.value().value);
					return QVariant::fromValue(v);
				}else if (std::holds_alternative<Maths::FVector4>(ref.value().value)) {
					auto v = std::get<Maths::FVector4>(ref.value().value);

					std::string mainLower = propName;
					std::transform(mainLower.begin(), mainLower.end(), mainLower.begin(),
						[](unsigned char c) { return std::tolower(c); });
					if (mainLower.find("u_albedo") != std::string::npos) {
						return QVariant::fromValue(QColor(v.x * 255, v.y* 255, v.z * 255,v.w*255));
						
					}
					return QVariant::fromValue(v);
				}
				else if (std::holds_alternative<float>(ref.value().value)) {
					auto v = std::get<float>(ref.value().value);
					return QVariant::fromValue(v);
				}
			}
			else if (featureList.count(propName))
			{
				return QVariant::fromValue(mat->HasFeature(propName)?true:false);
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			std::string propName = propertyName.toStdString();
			auto ref = mat->GetProperty(propName);
			if (mat->HasProperty(propName)) {
				if (std::holds_alternative<Maths::FVector3>(ref.value().value)) {
					mat->SetProperty(propName,value.value<Maths::FVector3>());
				}else if (std::holds_alternative<Maths::FVector4>(ref.value().value)) {
					std::string mainLower = propName;
					std::transform(mainLower.begin(), mainLower.end(), mainLower.begin(),
						[](unsigned char c) { return std::tolower(c); });
					if (mainLower.find("u_albedo") != std::string::npos) {
						auto color = value.value<QColor>();
						float alpha = color.alpha();
						if (alpha < 255) {
							mat->SetTransparent(true);
							mat->SetDepthWriting(true);
						}
						else
						{
							mat->SetBlendable(false);
							mat->SetTransparent(false);
							mat->SetDepthWriting(true);
						}
						mat->SetProperty(propName, Maths::FVector4{ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha() / 255.0f });						
					}
					else
					{
						mat->SetProperty(propName, value.value<Maths::FVector4>());
					}
				}
				else if (std::holds_alternative<Rendering::Resources::Texture*>(ref.value().value)) {
					auto texturePath=value.toString().toStdString();
					mat->LateUpdateTexture(propName, texturePath);
				}
				else if (std::holds_alternative<float>(ref.value().value)) {
					mat->SetProperty(propName, value.value<float>());
				}
			}
			else if (featureList.count(propName))
			{
				bool enable = value.value<bool>();
				if (enable) {
					mat->AddFeature(propName);
				}
				else
				{
					mat->RemoveFeature(propName);
				}
			}
		}
	private:
		std::unordered_set<std::string>featureList;
		Core::Resources::Material* mat;
	};
	ActorPropertyComponent* transferActorPropertyComponent(Core::ECS::Components::AComponent* comp) {

		if (auto trans = dynamic_cast<Core::ECS::Components::CTransform*>(comp)) {
			return new  TransFormPropertyComponent(trans);
		}
		else if (auto trans = dynamic_cast<Core::ECS::Components::CMaterialRenderer*>(comp)) {
			return new  MaterialPropertyComponent(trans);
		}
		else if (auto trans = dynamic_cast<Core::ECS::Components::CPostProcessStack*>(comp)) {
			return new  PostProcessStackPropertyComponent(trans);
		}
		else if (auto trans=dynamic_cast<Core::ECS::Components::CPointLight*>(comp))
		{
			return new PointLightPropertyComponent(trans);
		}
		else if (auto trans = dynamic_cast<Core::ECS::Components::CDirectionalLight*>(comp))
		{
			return new DirectionLightPropertyComponent(trans);
		}
		else if (auto trans = dynamic_cast<Core::ECS::Components::CTopoShape*>(comp)) {
			return new TopoShapePropertyComponent(trans);
		}
		return new ActorPropertyComponent(comp);
	}
	class PropertyWidget::PropertyWidgetInternal {
	public:
		PropertyWidgetInternal(PropertyWidget* tree) :mSelf(tree) {

		}		
		void setUp() {
			// 布局
			layout_ = new QVBoxLayout(mSelf);
			layout_->setContentsMargins(0, 0, 0, 0);

			// Light Inviwo-inspired polish on top of the global dark theme:
			// readable labels and editors with a clear hover/focus state.
			mSelf->setStyleSheet(R"(
				QLabel#PropertyLabel {
					color: #c8ccd0;
				}
				QLabel#GroupTitle {
					color: #e0e0e0;
					font-weight: 600;
				}
				QLineEdit, QDoubleSpinBox, QComboBox {
					background: #2a2a2d;
					border: 1px solid #3a3a3e;
					border-radius: 3px;
					padding: 2px 4px;
					color: #c8ccd0;
					selection-background-color: #268bd2;
				}
				QLineEdit:hover, QDoubleSpinBox:hover, QComboBox:hover,
				QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus {
					background: #333338;
					border: 1px solid #268bd2;
				}
				QLineEdit#NumberWidget {
					background: transparent;
					border: 1px solid transparent;
				}
				QLineEdit#NumberWidget:hover,
				QLineEdit#NumberWidget:focus {
					background: #47474b;
					border: 1px solid #268bd2;
				}
				QLineEdit#NumberWidget[invalid=true] {
					border: 1px solid #801717;
				}
				QComboBox::drop-down {
					border: none;
					width: 18px;
				}
				QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
					border: none;
					background: #333338;
					width: 16px;
				}
				QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
					background: #47474b;
				}
			)");

			mSelf->setLayout(layout_);
		}
		~PropertyWidgetInternal() {
			for (auto p : m_comps) {
				delete p.second;
			}
		}
		// 设置选中的Actor（核心入口）
		void setSelectedActor(Core::ECS::Actor* actor) {
			if (actor != m_selectedActor) {
				// 记录当前选中的Actor
				m_selectedActor = actor;
				// 构建新面板
				if (m_selectedActor) {
					//m_propertyModel->setCurrentActor(actor);
					// 自动展开所有节点
					//m_treeView->expandAll();
					//m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
					//m_treeView->openPersistentEditor(m_treeView->rootIndex());
					while (auto item = layout_->takeAt(0)) {
						delete item;
					}
					for (auto p : m_comps) {
						delete p.first;
						delete p.second;
					}
					m_comps.clear();
					for (auto& ptr : m_selectedActor->GetComponents()) {
					
						auto p = transferActorPropertyComponent(ptr.get());
						auto collpase = new CollapsibleGroupBoxWidget(p->getComponentName(), mSelf);
						layout_->addWidget(collpase);
						for (auto u:p->getProperties()) {
							collpase->addProperty(u);
						}
						m_comps.push_back({ collpase ,p});
					}
				
					layout_->addStretch();
				}
			}
		}
	private:
		friend class PropertyWidget;
		PropertyWidget* mSelf = nullptr;
		QVBoxLayout* layout_ = nullptr;
		QTreeView* m_treeView;
		
		Core::ECS::Actor* m_selectedActor=nullptr;          // 当前选中的Actor
		std::vector<std::pair<CollapsibleGroupBoxWidget*, ActorPropertyComponent*>>m_comps;
	};
	PropertyWidget::PropertyWidget(QWidget* parent):QWidget(parent),mInternal(new PropertyWidgetInternal(this))
	{
		RegService(PropertyWidget, *this);
		mInternal->setUp();
	}
	void PropertyWidget::setSelectedActor(Core::ECS::Actor* actor) {
		mInternal->setSelectedActor(actor);
	}
	PropertyWidget::~PropertyWidget()
	{
		delete mInternal;
	}
}
