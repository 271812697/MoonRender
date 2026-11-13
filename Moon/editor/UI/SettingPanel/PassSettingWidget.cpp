#pragma once
#include "core/SceneSystem/BvhService.h"
#include "editor/UI/SettingPanel/PassSettingWidget.h"
#include "Core/Global/ServiceLocator.h"
#include "Settings/DebugSetting.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "renderer/SceneView.h"
#include "Rendering/Core/ARenderPass.h"
#include "renderer/DebugSceneRenderer.h"
#include "renderer/SceneView.h"
#include "Widgets/PropertyComponent.h"
#include "Widgets/Property.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/FVec3Property.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/SliderIntProperty.h"
#include "Widgets/ColorPickerProperty.h"
#include "Widgets/TextureProperty.h"
#include "Core/Rendering/GbufferPass.h"
#include "Core/Rendering/HzbBuildPass.h"
#include "Core/Rendering/SkyBoxRenderPass .h"
#include "renderer/GizmoRenderPass.h"
#include "renderer/PickingRenderPass.h"
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

	class RenderPassComponent :public PropertyComponent
	{
	public:
		RenderPassComponent(Rendering::Core::ARenderPass* p) :pass(p) {
			mProperties.push_back(new BoolProperty("Enable", this));
		}
		virtual ~RenderPassComponent() {
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
		}
		virtual QString getComponentName()override {
			return "ABaseRenderPass";
		}
	protected:
		Rendering::Core::ARenderPass* pass = nullptr;
	};
	class GbufferPassComponent :public RenderPassComponent
	{
	public:
		GbufferPassComponent(::Core::Rendering::GbufferPass* p) :RenderPassComponent(p) {
			mProperties.push_back(new SliderFloatProperty("SSAO Radius", this, 0.0f, 10.0f));
			mProperties.push_back(new SliderFloatProperty("SSAO Bias", this, 0.001f, 10.0f));
			mProperties.push_back(new SliderFloatProperty("SSAO Blur Tolerance", this, 0.0f, 2.0f));
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto gbpass = dynamic_cast<::Core::Rendering::GbufferPass*>(pass);
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			else if (propertyName == "SSAO Radius") {
				return QVariant::fromValue(gbpass->getGbufferParam().ssaoParam.radius);
			}
			else if (propertyName == "SSAO Bias") {
				return QVariant::fromValue(gbpass->getGbufferParam().ssaoParam.bias);
			}
			else if (propertyName == "SSAO Blur Tolerance") {
				return QVariant::fromValue(gbpass->getGbufferParam().ssaoParam.blurTolerance);
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto gbpass = dynamic_cast<::Core::Rendering::GbufferPass*>(pass);
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
			else if (propertyName == "SSAO Radius") {
				gbpass->getGbufferParam().ssaoParam.radius = value.value<float>();
			}
			else if (propertyName == "SSAO Bias") {
				gbpass->getGbufferParam().ssaoParam.bias = value.value<float>();
			}
			else if (propertyName == "SSAO Blur Tolerance") {
				gbpass->getGbufferParam().ssaoParam.blurTolerance = value.value<float>();
			}
		}
	};
	class SkyBoxPassComponent :public RenderPassComponent
	{
	public:
		SkyBoxPassComponent(::Core::Rendering::SkyboxRenderPass* p) :RenderPassComponent(p) {
			mProperties.push_back(new EnumProperty("BackGround mode", this));
			// The two ends of the dome gradient: the colour overhead and the colour below.
			mProperties.push_back(new ColorPickerProperty("Zenith Color", this));
			mProperties.push_back(new ColorPickerProperty("Ground Color", this));
			mProperties.push_back(new SliderFloatProperty("Gradient Center", this, 0.0f, 1.0f));
			mProperties.push_back(new SliderFloatProperty("Gradient Spread", this, 0.05f, 2.0f));
			mProperties.push_back(new SliderFloatProperty("Gradient Curve", this, 0.2f, 4.0f));
			mProperties.push_back(new SliderFloatProperty("Vignette", this, 0.0f, 1.0f));
			mProperties.push_back(new ColorPickerProperty("Clear Color", this));
			mProperties.push_back(new TextureProperty("SkyBox Texture", this));
			//mProperties.push_back(new SliderFloatProperty("SSAO Bias", this, 0.001f, 10.0f));
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto skypass = dynamic_cast<::Core::Rendering::SkyboxRenderPass*>(pass);
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			else if (propertyName == "BackGround mode") {
				QList<QString>list = {"Gradient"  ,"SkyBox","PureColor"};
				return QVariant::fromValue(list);
			}
			else if (propertyName == "Zenith Color") {
				
				auto v = skypass->GetSetting().topColor;
				return QVariant::fromValue(QColor(v.x * 255, v.y * 255, v.z * 255, v.w * 255));
			}
			else if (propertyName == "Ground Color") {
				auto v = skypass->GetSetting().bottomColor;
				return QVariant::fromValue(QColor(v.x * 255, v.y * 255, v.z * 255, v.w * 255));
			}
			else if (propertyName == "Gradient Center") {
				return QVariant::fromValue(skypass->GetSetting().gradientCenter);
			}
			else if (propertyName == "Gradient Spread") {
				return QVariant::fromValue(skypass->GetSetting().gradientSpread);
			}
			else if (propertyName == "Gradient Curve") {
				return QVariant::fromValue(skypass->GetSetting().gradientCurve);
			}
			else if (propertyName == "Vignette") {
				return QVariant::fromValue(skypass->GetSetting().gradientVignette);
			}
			else if (propertyName == "Clear Color") {
				auto v = skypass->GetSetting().clearColor;
				return QVariant::fromValue(QColor(v.x * 255, v.y * 255, v.z * 255, v.w * 255));
			}
			else if (propertyName== "SkyBox Texture") {
			
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto skypass = dynamic_cast<::Core::Rendering::SkyboxRenderPass*>(pass);
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
			else if (propertyName == "BackGround mode") {
				skypass->GetSetting().mode = static_cast<::Core::Rendering::SkyMode>(value.value<int>());
			}
			else if (propertyName == "Zenith Color") {
				auto color = value.value<QColor>();
				skypass->GetSetting().topColor = Maths::FVector4{ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha() / 255.0f };
			}
			else if (propertyName=="Ground Color") {
				auto color = value.value<QColor>();
				skypass->GetSetting().bottomColor = Maths::FVector4{ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha() / 255.0f };
			}
			else if (propertyName == "Gradient Center") {
				skypass->GetSetting().gradientCenter = value.toFloat();
			}
			else if (propertyName == "Gradient Spread") {
				skypass->GetSetting().gradientSpread = value.toFloat();
			}
			else if (propertyName == "Gradient Curve") {
				skypass->GetSetting().gradientCurve = value.toFloat();
			}
			else if (propertyName == "Vignette") {
				skypass->GetSetting().gradientVignette = value.toFloat();
			}
			else if (propertyName == "Clear Color") {
				auto color = value.value<QColor>();
				skypass->GetSetting().clearColor = Maths::FVector4{ color.red() / 255.0f,color.green() / 255.0f,color.blue() / 255.0f,color.alpha() / 255.0f };
			}
			else if (propertyName == "SkyBox Texture") {
				skypass->updateSkyTexture(value.toString().toStdString());
			}
		}
	};
	class GizmoPassComponent :public RenderPassComponent
	{
	public:
		GizmoPassComponent(::Editor::Rendering::GizmoRenderPass* p) :RenderPassComponent(p) {
			;
			for (auto& item : p->getGizmoWidgets()) {
				mProperties.push_back(new BoolProperty(QString::fromStdString(item.first), this));
			}

			//mProperties.push_back(new SliderFloatProperty("SSAO Bias", this, 0.001f, 10.0f));
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto gizmopass = dynamic_cast<::Editor::Rendering::GizmoRenderPass*>(pass);
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			else {
				return QVariant::fromValue(gizmopass->isEnableGizmoWidget(propertyName.toStdString()));
			}
			
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto gizmopass = dynamic_cast<::Editor::Rendering::GizmoRenderPass*>(pass);
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
			else {
				gizmopass->enableGizmoWidget(propertyName.toStdString(), value.value<bool>());
			}
		}
	};
	class PickPassComponent :public RenderPassComponent
	{
	public:
		PickPassComponent(::Editor::Rendering::PickingRenderPass* p) :RenderPassComponent(p) {
			mProperties.pop_back();
			mProperties.push_back(new BoolProperty("DebugIdTexture", this));

			//mProperties.push_back(new SliderFloatProperty("SSAO Bias", this, 0.001f, 10.0f));
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto pickPass = dynamic_cast<::Editor::Rendering::PickingRenderPass*>(pass);
			auto& pickOption = pickPass->GetPickPassOption();
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			else if(propertyName == "DebugIdTexture"){
				return QVariant::fromValue(pickOption.debug);
			}

			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto pickPass = dynamic_cast<::Editor::Rendering::PickingRenderPass*>(pass);
			auto& pickOption = pickPass->GetPickPassOption();
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
			else if (propertyName == "DebugIdTexture") {
				pickOption.debug = value.value<bool>();
			}
		}
	};
	class HzbPassComponent :public RenderPassComponent
	{
	public:
		HzbPassComponent(::Core::Rendering::HzbBuildPass* p) :RenderPassComponent(p) {
			mProperties.push_back(new SliderFloatProperty("Depth Bias", this, 0.0f, 0.005f));
			mProperties.push_back(new SliderFloatProperty("Static Bias", this, 0.0f, 0.0001f));
		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto hzbPass = dynamic_cast<::Core::Rendering::HzbBuildPass*>(pass);
			if (propertyName == "Enable") {
				return QVariant::fromValue(pass->IsEnabled());
			}
			else if (propertyName == "Depth Bias") {
				return QVariant::fromValue(hzbPass->GetDepthBias());
			}
			else if (propertyName == "Static Bias") {
				return QVariant::fromValue(hzbPass->GetStaticDepthBias());
			}
			return QVariant();
		}
		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			auto hzbPass = dynamic_cast<::Core::Rendering::HzbBuildPass*>(pass);
			if (propertyName == "Enable") {
				pass->SetEnabled(value.value<bool>());
			}
			else if (propertyName == "Depth Bias") {
				hzbPass->SetDepthBias(value.value<float>());
			}
			else if (propertyName == "Static Bias") {
				hzbPass->SetStaticDepthBias(value.value<float>());
			}
		}
	};
	RenderPassComponent* CreateRenderPassComponent(Rendering::Core::ARenderPass* pass) {
		if (dynamic_cast<::Core::Rendering::GbufferPass*>(pass)) {
			return new GbufferPassComponent(dynamic_cast<::Core::Rendering::GbufferPass*>(pass));
		}
		else if (dynamic_cast<::Core::Rendering::SkyboxRenderPass*>(pass)) {
			return new SkyBoxPassComponent(dynamic_cast<::Core::Rendering::SkyboxRenderPass*>(pass));
		}
		else if (dynamic_cast<::Editor::Rendering::GizmoRenderPass*>(pass)) {
			return new GizmoPassComponent(dynamic_cast<::Editor::Rendering::GizmoRenderPass*>(pass));
		}
		else if (dynamic_cast<::Editor::Rendering::PickingRenderPass*>(pass)) {
			return new PickPassComponent(dynamic_cast<::Editor::Rendering::PickingRenderPass*>(pass));
		}
		else if (dynamic_cast<::Core::Rendering::HzbBuildPass*>(pass)) {
			return new HzbPassComponent(dynamic_cast<::Core::Rendering::HzbBuildPass*>(pass));
		}
		else {
			return new RenderPassComponent(pass);
		}
	}


	class RenderPassSettingWidget::RenderPassSettingWidgetInternal {
	public:
		RenderPassSettingWidgetInternal(RenderPassSettingWidget* tree) :mSelf(tree) {

		}		
		void setUp() {
			// 布局
			layout_ = new QVBoxLayout(mSelf);
			layout_->setContentsMargins(0, 0, 0, 0);
			mSelf->setLayout(layout_);
			//Refresh();
		}
		void Refresh() {
			while (auto item = layout_->takeAt(0)) {
				delete item;
			}
			for (auto p : m_comps) {
				delete p.first;
				delete p.second;
			}	
			m_comps.clear();
			auto& renderer=GetService(Editor::Panels::SceneView).GetRenderer();
			auto& passMap=renderer.GetPasses();
			for (const auto& pass : passMap | std::views::values)
			{
				auto p = CreateRenderPassComponent(pass.second.get());
				
				auto collpase = new CollapsibleGroupBoxWidget(QString::fromStdString(pass.first), mSelf);
				layout_->addWidget(collpase);
				for (auto u : p->getProperties()) {
					collpase->addProperty(u);
				}
				m_comps.push_back({ collpase ,p });
			}
			layout_->addStretch();
		}
		~RenderPassSettingWidgetInternal() {
			for (auto p : m_comps) {
				delete p.second;
			}
		}
	private:
		friend class RenderPassSettingWidget;
		RenderPassSettingWidget* mSelf = nullptr;
		QVBoxLayout* layout_ = nullptr;
		
		std::vector<std::pair<CollapsibleGroupBoxWidget*, PropertyComponent*>>m_comps;
	};
	RenderPassSettingWidget::RenderPassSettingWidget(QWidget* parent):QWidget(parent),mInternal(new RenderPassSettingWidgetInternal(this))
	{
		RegService(RenderPassSettingWidget, *this);
		mInternal->setUp();
	}
	void RenderPassSettingWidget::Refresh() {
		mInternal->Refresh();
	}
	RenderPassSettingWidget::~RenderPassSettingWidget()
	{
		delete mInternal;
	}
}
