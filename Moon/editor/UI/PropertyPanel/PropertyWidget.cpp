#pragma once
#include "PropertyWidget.h"
#include "core/ECS/Actor.h"
#include "core/ECS/Components/CMaterialRenderer.h"
#include "Core/ECS/Components/CPostProcessStack.h"
#include "Core/ECS/Components/CPointLight.h"
#include "core/ECS/Components/CDirectionalLight.h"
#include "core/component/CTopoShape.h"
#include "core/Resources/Material.h"
#include <Rendering/Resources/Shader.h>
#include <Rendering/Resources/Texture.h>
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
#include "Widgets/Vec2Property.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "core/log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
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
	namespace {
		std::string ToLower(std::string p_text)
		{
			std::ranges::transform(p_text, p_text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return p_text;
		}

		// "u_TextureTiling" -> "Texture Tiling", the same label the material designer uses.
		std::string PrettyLabel(const std::string& p_name)
		{
			std::string name = p_name;
			if (name.starts_with("u_")) { name.erase(0, 2); }
			else if (name.starts_with("_")) { name.erase(0, 1); }

			std::string label;
			label.reserve(name.size() + 4);
			for (size_t i = 0; i < name.size(); ++i)
			{
				const char c = name[i];
				if (i > 0
					&& std::isupper(static_cast<unsigned char>(c))
					&& !std::isupper(static_cast<unsigned char>(name[i - 1]))
					&& name[i - 1] != '_')
				{
					label += ' ';
				}
				label += (c == '_' ? ' ' : c);
			}
			if (!label.empty())
			{
				label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
			}
			return label;
		}

		bool IsAlbedoParameter(const std::string& p_name)
		{
			return ToLower(p_name).find("albedo") != std::string::npos;
		}

		// "PARALLAX_MAPPING" -> "Parallax Mapping", the label of a feature toggle.
		std::string FeatureLabel(const std::string& p_name)
		{
			std::string label;
			label.reserve(p_name.size());
			bool start = true;
			for (const char c : p_name)
			{
				if (c == '_')
				{
					label += ' ';
					start = true;
					continue;
				}
				label += start
					? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
					: static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				start = false;
			}
			return label;
		}

		struct SliderRange
		{
			float min = 0.0f;
			float max = 1.0f;
			float step = 0.005f;
		};

		// The range a unit-less material scalar gets - same heuristic as the designer.
		SliderRange RangeFor(float p_value, const std::string& p_name)
		{
			const std::string name = ToLower(p_name);
			const auto has = [&name](const char* p_part) {
				return name.find(p_part) != std::string::npos;
			};

			SliderRange range;
			const bool unit = has("roughness") || has("metallic") || has("specular")
				|| has("glossiness") || has("transmission") || has("opacity")
				|| has("threshold") || has("alpha") || has("dither") || has("parallax");
			const bool wide = has("intensity") || has("strength") || has("scale")
				|| has("fade") || has("distance") || has("emissive") || has("height");

			if (unit)
			{
				range.min = 0.0f;
				range.max = 1.0f;
			}
			else if (wide)
			{
				range.min = 0.0f;
				range.max = std::max(10.0f, p_value * 2.0f);
			}
			else
			{
				range.min = p_value < 0.0f ? std::min(-1.0f, p_value * 2.0f) : 0.0f;
				range.max = p_value > 1.0f ? p_value * 2.0f : 1.0f;
			}

			range.step = std::max(0.0005f, (range.max - range.min) / 500.0f);
			return range;
		}
	}

	class TopoShapePropertyComponent :public ActorPropertyComponent
	{
	public:
		TopoShapePropertyComponent(Core::ECS::Components::CTopoShape* comp) :ActorPropertyComponent(comp) {
			// The colour of the shape itself - the u_Albedo of the AllFaces material of
			// this actor (see CTopoShape::SetColor) - which is what a feature is told
			// apart by. The material panel below an anchor edits the same uniform.
			mProperties.push_back(new ColorPickerProperty("Color", this));
			mProperties.push_back(new EnumProperty("HighLight Mode", this));
			mProperties.push_back(new ColorPickerProperty("HoverColor", this));
			mProperties.push_back(new ColorPickerProperty("SeletedColor", this));
		}
		virtual ~TopoShapePropertyComponent() {

		}
		virtual QVariant getPropertyValue(const QString& propertyName)override {
			auto comp = dynamic_cast<Core::ECS::Components::CTopoShape*>(component);
			auto& highLightOption = comp->getHightLightOption();
			if (propertyName == "Color") {
				const Maths::FVector4 color = comp->GetColor();
				return QVariant::fromValue(QColor(
					color.x * 255, color.y * 255, color.z * 255, color.w * 255));
			}
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
			if (propertyName == "Color") {
				const auto color = value.value<QColor>();
				comp->SetColor(Maths::FVector4(
					color.red() / 255.0f,
					color.green() / 255.0f,
					color.blue() / 255.0f,
					color.alpha() / 255.0f));
			}
			else if (propertyName == "HighLight Mode") {
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
				FeatureBody::Active()->populateFeature(feature);
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
	/** The material section of the actor inspector.
	 *
	 * It is generated from the shader's parameter table, exactly like the material designer's
	 * form (the same labels, groups, ranges, colour pickers and texture slots), instead of
	 * listing whatever names the instance happens to carry. The widgets edit the actor's own
	 * material instance, so the viewport updates live. */
	class MaterialPropertyComponent :public ActorPropertyComponent
	{
	public:
		MaterialPropertyComponent(Core::ECS::Components::CMaterialRenderer* comp) :ActorPropertyComponent(comp) {
			mat = comp->GetMaterialAtIndex(0);
			BuildForm();
		}
		virtual ~MaterialPropertyComponent() = default;

		QWidget* createCustomWidget(QWidget* p_parent) override {
			(void)p_parent;
			return m_form;
		}

		virtual QVariant getPropertyValue(const QString& propertyName)override {
			if (mat == nullptr) { return QVariant(); }

			if (const auto feature = m_featureNames.find(propertyName); feature != m_featureNames.end()) {
				return QVariant::fromValue(mat->HasFeature(feature->second));
			}

			const auto uniform = m_uniformNames.find(propertyName);
			if (uniform == m_uniformNames.end()) { return QVariant(); }

			const auto ref = mat->GetProperty(uniform->second);
			if (!ref.has_value()) { return QVariant(); }

			const auto& value = ref.value().value;
			if (std::holds_alternative<bool>(value)) {
				return QVariant::fromValue(std::get<bool>(value));
			}
			if (std::holds_alternative<int>(value)) {
				return QVariant::fromValue(std::get<int>(value));
			}
			if (std::holds_alternative<float>(value)) {
				return QVariant::fromValue(std::get<float>(value));
			}
			if (std::holds_alternative<Maths::FVector2>(value)) {
				return QVariant::fromValue(std::get<Maths::FVector2>(value));
			}
			if (std::holds_alternative<Maths::FVector3>(value)) {
				const auto& vector = std::get<Maths::FVector3>(value);
				if (m_colorNames.contains(uniform->second)) {
					return QVariant::fromValue(QColor::fromRgbF(vector.x, vector.y, vector.z));
				}
				return QVariant::fromValue(vector);
			}
			if (std::holds_alternative<Maths::FVector4>(value)) {
				const auto& vector = std::get<Maths::FVector4>(value);
				if (m_colorNames.contains(uniform->second)) {
					return QVariant::fromValue(QColor::fromRgbF(vector.x, vector.y, vector.z, vector.w));
				}
				return QVariant::fromValue(vector);
			}
			if (std::holds_alternative<Rendering::Resources::Texture*>(value)) {
				const auto* texture = std::get<Rendering::Resources::Texture*>(value);
				return texture != nullptr ? QString::fromStdString(texture->path) : QString();
			}
			return QVariant();
		}

		virtual void setPropertyValue(const QString& propertyName, const QVariant& value)override {
			if (mat == nullptr) { return; }

			if (const auto feature = m_featureNames.find(propertyName); feature != m_featureNames.end()) {
				if (value.toBool()) { mat->AddFeature(feature->second); }
				else { mat->RemoveFeature(feature->second); }
				return;
			}

			const auto uniform = m_uniformNames.find(propertyName);
			if (uniform == m_uniformNames.end()) { return; }

			const std::string& name = uniform->second;
			const auto ref = mat->GetProperty(name);
			if (!ref.has_value()) { return; }

			const auto& current = ref.value().value;
			if (std::holds_alternative<bool>(current)) {
				mat->SetProperty(name, value.toBool());
			}
			else if (std::holds_alternative<int>(current)) {
				mat->SetProperty(name, value.toInt());
			}
			else if (std::holds_alternative<float>(current)) {
				mat->SetProperty(name, value.toFloat());
			}
			else if (std::holds_alternative<Maths::FVector2>(current)) {
				mat->SetProperty(name, value.value<Maths::FVector2>());
			}
			else if (std::holds_alternative<Maths::FVector3>(current)) {
				if (m_colorNames.contains(name)) {
					const QColor color = value.value<QColor>();
					const auto& stored = std::get<Maths::FVector3>(current);
					const float red = static_cast<float>(color.redF());
					const float green = static_cast<float>(color.greenF());
					const float blue = static_cast<float>(color.blueF());
					// The colour widget pushes its initial value while it is created; that is
					// not an edit and must not touch the material. QColor is 8-bit, so allow
					// one quantization step rather than demanding bit equality.
					constexpr float epsilon = 1.0f / 255.0f;
					if (std::abs(stored.x - red) <= epsilon
						&& std::abs(stored.y - green) <= epsilon
						&& std::abs(stored.z - blue) <= epsilon) {
						return;
					}
					mat->SetProperty(name, Maths::FVector3{ red, green, blue });
				}
				else {
					mat->SetProperty(name, value.value<Maths::FVector3>());
				}
			}
			else if (std::holds_alternative<Maths::FVector4>(current)) {
				if (m_colorNames.contains(name)) {
					const QColor color = value.value<QColor>();
					const auto& stored = std::get<Maths::FVector4>(current);
					const float red = static_cast<float>(color.redF());
					const float green = static_cast<float>(color.greenF());
					const float blue = static_cast<float>(color.blueF());
					const float alpha = static_cast<float>(color.alphaF());
					constexpr float epsilon = 1.0f / 255.0f;
					if (std::abs(stored.x - red) <= epsilon
						&& std::abs(stored.y - green) <= epsilon
						&& std::abs(stored.z - blue) <= epsilon
						&& std::abs(stored.w - alpha) <= epsilon) {
						return;
					}
					// Keep the old inspector behaviour: changing the albedo alpha toggles the
					// transparent queue, so a colour edit is enough to make a face translucent.
					if (IsAlbedoParameter(name)) {
						if (color.alpha() < 255) {
							mat->SetTransparent(true);
							mat->SetDepthWriting(true);
						}
						else {
							mat->SetBlendable(false);
							mat->SetTransparent(false);
							mat->SetDepthWriting(true);
						}
					}
					mat->SetProperty(name, Maths::FVector4{ red, green, blue, alpha });
				}
				else {
					mat->SetProperty(name, value.value<Maths::FVector4>());
				}
			}
			else if (std::holds_alternative<Rendering::Resources::Texture*>(current)) {
				mat->LateUpdateTexture(name, value.toString().toStdString());
			}
		}

	private:
		void BuildForm() {
			m_form = new QWidget();
			auto* layout = new QVBoxLayout(m_form);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);

			if (mat == nullptr || mat->GetShader() == nullptr) {
				auto* hint = new QLabel(QStringLiteral("This material has no shader."), m_form);
				hint->setMargin(8);
				layout->addWidget(hint);
				return;
			}

			auto* resetButton = new QPushButton(QStringLiteral("Reset to Defaults"), m_form);
			resetButton->setToolTip(QStringLiteral(
				"Reset every material parameter to the shader's declared default"));
			layout->addWidget(resetButton);
			QObject::connect(resetButton, &QPushButton::clicked, resetButton,
				[this] { ResetToDefaults(); });

			auto* shader = mat->GetShader();
			const auto& parameters = shader->GetParameters();

			// First pass: labels and the label -> uniform map. The widgets are keyed by the
			// label, the material by the shader's own name; duplicate labels fall back to the
			// uniform name, as in the material designer.
			std::vector<QString> labels(parameters.size());
			std::set<QString> usedLabels;
			for (size_t i = 0; i < parameters.size(); ++i) {
				const auto& parameter = parameters[i];
				if (!parameter.isUi || parameter.count > 0) { continue; }

				QString label = parameter.label.empty()
					? QString::fromStdString(PrettyLabel(parameter.name))
					: QString::fromStdString(parameter.label);
				if (usedLabels.contains(label)) {
					label = QString::fromStdString(parameter.name);
				}
				usedLabels.insert(label);
				labels[i] = label;
				m_uniformNames.emplace(label, parameter.name);

				if (parameter.isColor) {
					m_colorNames.insert(parameter.name);
				}
			}

			std::vector<std::string> features;
			if (!shader->GetDeclaredFeatures().empty()) {
				features = shader->GetDeclaredFeatures();
			}
			else {
				features.assign(shader->GetFeatures().begin(), shader->GetFeatures().end());
			}
			std::ranges::sort(features);

			std::map<std::string, CollapsibleGroupBoxWidget*> groups;
			const auto groupFor = [&](const std::string& p_name) -> CollapsibleGroupBoxWidget* {
				if (const auto it = groups.find(p_name); it != groups.end()) {
					return it->second;
				}
				auto* group = new CollapsibleGroupBoxWidget(QString::fromStdString(p_name), m_form);
				group->setCollapsed(false);
				layout->addWidget(group);
				groups.emplace(p_name, group);
				return group;
			};

			const auto registerProperty = [this](CollapsibleGroupBoxWidget* p_group, WidgetProperty* p_property) {
				p_group->addProperty(p_property);
				addProperty(p_property);
				p_property->updateWidgetValue(getPropertyValue(p_property->getPropertyName()));
			};

			// Features first: they select the shader variant, and the parameters that only some
			// variants read come after them.
			if (!features.empty()) {
				auto* group = groupFor("Features");
				for (const std::string& feature : features) {
					QString label = QString::fromStdString(FeatureLabel(feature));
					if (usedLabels.contains(label)) {
						label = QString::fromStdString(feature);
					}
					usedLabels.insert(label);
					m_featureNames.emplace(label, feature);
					registerProperty(group, new BoolProperty(label, this, BoolProperty::Style::InviwoRect));
				}
			}

			for (size_t i = 0; i < parameters.size(); ++i) {
				const auto& parameter = parameters[i];
				if (!parameter.isUi || parameter.count > 0) { continue; }

				const QString& label = labels[i];
				const std::string groupName = parameter.group.empty()
					? (parameter.isSampler ? "Textures" : "Parameters")
					: parameter.group;
				auto* group = groupFor(groupName);

				if (parameter.isSampler) {
					registerProperty(group, new TextureProperty(label, this));
					continue;
				}

				const std::string& type = parameter.type;
				if (type == "bool") {
					registerProperty(group, new BoolProperty(label, this, BoolProperty::Style::InviwoRect));
				}
				else if (type == "int") {
					auto* property = new SliderIntProperty(label, this);
					const int value = getPropertyValue(label).toInt();
					property->setMinMax(
						parameter.hasRange ? static_cast<int>(std::lround(parameter.min)) : std::min(0, value),
						parameter.hasRange ? static_cast<int>(std::lround(parameter.max)) : std::max(16, std::abs(value) * 2));
					property->setIncrement(parameter.hasStep
						? std::max(1, static_cast<int>(std::lround(parameter.step)))
						: 1);
					registerProperty(group, property);
				}
				else if (type == "float") {
					const float value = getPropertyValue(label).toFloat();
					SliderRange range;
					if (parameter.hasRange) {
						range.min = parameter.min;
						range.max = parameter.max;
						range.step = parameter.hasStep
							? parameter.step
							: std::max(0.0005f, (parameter.max - parameter.min) / 500.0f);
					}
					else {
						range = RangeFor(value, parameter.name);
					}

					auto* property = new SliderFloatProperty(label, this, range.min, range.max);
					registerProperty(group, property);
					// setStep needs the editor widget, so it comes after addProperty above.
					property->setStep(range.step);
				}
				else if (type == "vec2") {
					registerProperty(group, new Vec2Property(label, this));
				}
				else if (type == "vec3") {
					if (parameter.isColor) {
						registerProperty(group, new ColorPickerProperty(label, this));
					}
					else {
						registerProperty(group, new FVec3Property(label, this));
					}
				}
				else if (type == "vec4") {
					if (parameter.isColor) {
						registerProperty(group, new ColorPickerProperty(label, this));
					}
					else {
						registerProperty(group, new FVec4Property(label, this));
					}
				}
			}

			layout->addStretch(1);
		}

		void ResetToDefaults() {
			if (mat == nullptr || mat->GetShader() == nullptr) {
				return;
			}
			// Rebuild the property map from the shader's declared initializers. This keeps the
			// material state and features; only the parameters (including texture slots) reset.
			mat->GetProperties().clear();
			mat->UpdateProperties();
			for (auto* property : mProperties) {
				property->updateWidgetValue(getPropertyValue(property->getPropertyName()));
			}
		}

		Core::Resources::Material* mat = nullptr;
		QWidget* m_form = nullptr;
		std::map<QString, std::string> m_uniformNames;
		std::map<QString, std::string> m_featureNames;
		std::set<std::string> m_colorNames;
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
						const bool isMaterial = dynamic_cast<MaterialPropertyComponent*>(p) != nullptr;
						auto collpase = new CollapsibleGroupBoxWidget(
							isMaterial ? QStringLiteral("Material") : p->getComponentName(), mSelf);
						layout_->addWidget(collpase);
						if (auto* custom = p->createCustomWidget(collpase)) {
							// The material component renders the same grouped form as the
							// material designer, so it is one custom widget instead of a flat
							// list of properties.
							collpase->addSubWidget(custom);
							if (isMaterial) {
								collpase->setCollapsed(false);
							}
						}
						else {
							for (auto u : p->getProperties()) {
								collpase->addProperty(u);
							}
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
