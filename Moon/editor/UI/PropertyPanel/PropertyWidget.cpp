#pragma once
#include "PropertyWidget.h"
#include "core/ECS/Actor.h"
#include "core/ECS/Components/CMaterialRenderer.h"
#include "Core/ECS/Components/CModelRenderer.h"
#include "Core/ECS/Components/CPostProcessStack.h"
#include "Core/ECS/Components/CPointLight.h"
#include "core/ECS/Components/CDirectionalLight.h"
#include "core/component/CTopoShape.h"
#include "core/Resources/Material.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include "editor/UI/ContentBrowser/MaterialThumbnailRenderer.h"
#include "renderer/SceneView.h"

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
#include <QFrame>
#include <QMenu>
#include <QLabel>
#include <filesystem>
#include <Tools/Utils/PathParser.h>
#include <QPainter>
#include <QRadialGradient>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>

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
	/** The material file a drag carries, or an empty string. The content browser drags assets as
	 * file URLs; only .mat files can be dropped on a material slot. */
	QString MaterialPathFromMime(const QMimeData* p_mime)
	{
		if (p_mime == nullptr || !p_mime->hasUrls())
		{
			return {};
		}
		for (const QUrl& url : p_mime->urls())
		{
			if (!url.isLocalFile())
			{
				continue;
			}
			const QString local = url.toLocalFile();
			if (QFileInfo(local).suffix().compare(QStringLiteral("mat"), Qt::CaseInsensitive) == 0)
			{
				return local;
			}
		}
		return {};
	}

	/** The material assets the resource manager holds, as the ":Materials/<file>" keys its paths
	 * use. What the slot's drop-down lists. */
	std::vector<std::string> MaterialAssetKeys()
	{
		std::vector<std::string> keys;

		// Listed from the folder the browser shows, not from the manager: building the panel must
		// not pull on a service (the panel is built while the viewport is drawing).
		const std::filesystem::path folder =
			std::filesystem::path(Tools::Utils::PathParser::GetExeDirectory()) / "Moon" / "Data" / "Engine" / "Materials";

		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(folder, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".mat")
			{
				continue;
			}
			keys.push_back(":Materials/" + entry.path().filename().string());
		}
		std::ranges::sort(keys);
		return keys;
	}

	/** A painted stand-in for the material's thumbnail: a sphere lit top-left, tinted with the
	 * material's own base colour. The real preview needs the render module's context and a whole
	 * frame per material, which a property panel must not do while it is being built - the
	 * content browser renders those on a timer of its own. */
	/** ":Materials/Foo.mat" as the file on disk: what the thumbnail cache is keyed by. */
	QString MaterialFilePath(const QString& p_key)
	{
		const QString file = QFileInfo(p_key).fileName();
		const std::filesystem::path folder =
			std::filesystem::path(Tools::Utils::PathParser::GetExeDirectory()) / "Moon" / "Data" / "Engine" / "Materials";
		return QString::fromStdString((folder / file.toStdString()).string());
	}
	QPixmap MaterialSphereIcon(const QColor& p_color, int p_size)
	{
		QPixmap pixmap(p_size, p_size);
		pixmap.fill(Qt::transparent);

		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);

		const QColor base = p_color.isValid() ? p_color : QColor(150, 150, 150);
		QRadialGradient gradient(QPointF(p_size * 0.36, p_size * 0.30), p_size * 0.78);
		gradient.setColorAt(0.0, base.lighter(160));
		gradient.setColorAt(0.55, base);
		gradient.setColorAt(1.0, base.darker(240));
		painter.setBrush(gradient);
		painter.setPen(QPen(QColor(0, 0, 0, 140), 1.0));
		painter.drawEllipse(QRectF(1.5, 1.5, p_size - 3.0, p_size - 3.0));

		return pixmap;
	}

	/** One material slot, laid out like Unreal's element row: the slot's index, the material's
	 * thumbnail, a drop-down of the material assets and a button that empties the slot. A material
	 * dragged from the content browser is taken by the whole row.
	 *
	 * Dragging still works, the drop-down is for what the asset list makes easy to reach: both end
	 * up in the same assignment call. */
	class MaterialSlotRow : public QWidget
	{
	public:
		using AssignHandler = std::function<void(const QString&)>;
		using ClearHandler = std::function<void()>;

		MaterialSlotRow(int p_index, const QString& p_currentKey, const QColor& p_albedo,
			const std::vector<std::string>& p_keys, AssignHandler p_assign, ClearHandler p_clear,
			QWidget* p_parent = nullptr)
			: QWidget(p_parent), m_assign(std::move(p_assign)), m_clear(std::move(p_clear))
		{
			setAcceptDrops(true);

			auto* layout = new QHBoxLayout(this);
			layout->setContentsMargins(8, 5, 8, 5);
			layout->setSpacing(8);

			auto* label = new QLabel(QStringLiteral("Element %1").arg(p_index), this);
			label->setMinimumWidth(56);
			layout->addWidget(label, 0, Qt::AlignVCenter);

			// One menu, shared by the thumbnail, the name field and the arrow: any of them opens
			// the list of material assets.
			auto* menu = new QMenu(this);
			menu->addAction(QStringLiteral("Clear"), this, [this] {
				if (m_clear) { m_clear(); }
			});
			menu->addSeparator();
			for (const std::string& key : p_keys)
			{
				const QString materialKey = QString::fromStdString(key);
				menu->addAction(QFileInfo(materialKey).fileName(), this, [this, materialKey] {
					if (m_assign) { m_assign(materialKey); }
				});
			}

			// The green bar under the thumbnail is Unreal's "this slot is filled" cue.
			m_thumbnail = new QToolButton(this);
			m_thumbnail->setFixedSize(42, 42);
			m_thumbnail->setIconSize(QSize(36, 36));
			m_thumbnail->setPopupMode(QToolButton::InstantPopup);
			m_thumbnail->setMenu(menu);
			m_thumbnail->setToolTip(QStringLiteral("Pick the material of this slot"));
			m_thumbnail->setStyleSheet(QStringLiteral(
				"QToolButton { background: #161616; border: 1px solid #3c3c3c;"
				" border-bottom: 3px solid %1; } QToolButton::menu-indicator { image: none; }")
				.arg(p_currentKey.isEmpty() ? QStringLiteral("#3c3c3c") : QStringLiteral("#5fbf3f")));
			if (!p_currentKey.isEmpty())
			{
				SetThumbnail(m_thumbnail, p_albedo, p_currentKey);
			}
			layout->addWidget(m_thumbnail, 0, Qt::AlignVCenter);

			m_name = new QToolButton(this);
			m_name->setText(p_currentKey.isEmpty()
				? QStringLiteral("<none>")
				: QFileInfo(p_currentKey).fileName());
			m_name->setToolButtonStyle(Qt::ToolButtonTextOnly);
			m_name->setPopupMode(QToolButton::InstantPopup);
			m_name->setMenu(menu);
			m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			m_name->setMinimumWidth(60);
			m_name->setCursor(Qt::PointingHandCursor);
			m_name->setStyleSheet(QStringLiteral(
				"QToolButton { text-align: left; padding: 4px 8px; border: 1px solid #3c3c3c;"
				" background: #232323; } QToolButton::menu-indicator { image: none; }"));
			m_name->setToolTip(QStringLiteral("Pick the material of this slot"));
			layout->addWidget(m_name, 1);

			// The drops land on whichever child is under the cursor, so the whole row takes them.
			for (QWidget* child : findChildren<QWidget*>())
			{
				child->setAcceptDrops(true);
				child->installEventFilter(this);
			}
		}

		/** Assigned after construction: the handler needs the row to refresh itself. */
		void SetHandlers(AssignHandler p_assign, ClearHandler p_clear)
		{
			m_assign = std::move(p_assign);
			m_clear = std::move(p_clear);
		}

		/** Shows what the slot holds now: the name, the filled/empty bar and the thumbnail. */
		void Refresh(const QString& p_key, const QColor& p_albedo)
		{
			m_name->setText(p_key.isEmpty() ? QStringLiteral("<none>") : QFileInfo(p_key).fileName());
			m_thumbnail->setStyleSheet(QStringLiteral(
				"QToolButton { background: #161616; border: 1px solid #3c3c3c;"
				" border-bottom: 3px solid %1; } QToolButton::menu-indicator { image: none; }")
				.arg(p_key.isEmpty() ? QStringLiteral("#3c3c3c") : QStringLiteral("#5fbf3f")));
			m_thumbnail->setIcon(QIcon());
			if (!p_key.isEmpty())
			{
				SetThumbnail(m_thumbnail, p_albedo, p_key);
			}
		}

	protected:
		void dragEnterEvent(QDragEnterEvent* p_event) override
		{
			if (!MaterialPathFromMime(p_event->mimeData()).isEmpty()) { p_event->acceptProposedAction(); }
		}

		void dragMoveEvent(QDragMoveEvent* p_event) override
		{
			if (!MaterialPathFromMime(p_event->mimeData()).isEmpty()) { p_event->acceptProposedAction(); }
		}

		void dropEvent(QDropEvent* p_event) override
		{
			const QString path = MaterialPathFromMime(p_event->mimeData());
			if (path.isEmpty()) { return; }
			if (m_assign) { m_assign(path); }
			p_event->acceptProposedAction();
		}

		/** The children are drop targets too: a drop on the thumbnail or the name field has to
		 * behave like a drop on the row. */
		bool eventFilter(QObject* p_watched, QEvent* p_event) override
		{
			if (p_event->type() == QEvent::DragEnter || p_event->type() == QEvent::DragMove)
			{
				auto* drag = static_cast<QDragMoveEvent*>(p_event);
				if (!MaterialPathFromMime(drag->mimeData()).isEmpty())
				{
					drag->acceptProposedAction();
					return true;
				}
			}
			else if (p_event->type() == QEvent::Drop)
			{
				auto* drop = static_cast<QDropEvent*>(p_event);
				const QString path = MaterialPathFromMime(drop->mimeData());
				if (!path.isEmpty())
				{
					if (m_assign) { m_assign(path); }
					drop->acceptProposedAction();
					return true;
				}
			}
			return QWidget::eventFilter(p_watched, p_event);
		}

	private:
		/** The browser's rendered picture when it exists, the painted sphere until then. */
		void SetThumbnail(QToolButton* p_button, const QColor& p_albedo, const QString& p_key)
		{
			const QPixmap cached = CachedMaterialThumbnail(MaterialFilePath(p_key));
			if (!cached.isNull())
			{
				p_button->setIcon(QIcon(cached.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				return;
			}

			p_button->setIcon(QIcon(MaterialSphereIcon(p_albedo, 36)));

			auto* poll = new QTimer(this);
			poll->setInterval(400);
			QObject::connect(poll, &QTimer::timeout, poll, [p_button, key = p_key, poll] {
				const QPixmap ready = CachedMaterialThumbnail(MaterialFilePath(key));
				if (ready.isNull()) { return; }
				p_button->setIcon(QIcon(ready.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				poll->stop();
			});
			poll->start();
		}

		QToolButton* m_thumbnail = nullptr;
		QToolButton* m_name = nullptr;
		AssignHandler m_assign;
		ClearHandler m_clear;
	};

	class MaterialPropertyComponent :public ActorPropertyComponent
	{
	public:
		MaterialPropertyComponent(Core::ECS::Components::CMaterialRenderer* comp) :ActorPropertyComponent(comp),
			m_renderer(comp) {
			mat = comp->GetMaterialAtIndex(0);
			BuildForm();
		}
		virtual ~MaterialPropertyComponent() = default;

		QWidget* createCustomWidget(QWidget* p_parent) override {
			// The slots the component draws with, one Unreal-style row each: index, thumbnail,
			// material drop-down, and a material dragged onto the row is taken as well. The
			// parameter form of the first slot follows, since that is what the panel is for.
			auto* container = new QWidget(p_parent);
			auto* layout = new QVBoxLayout(container);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);

			const std::vector<std::string> keys = MaterialAssetKeys();
			const int slotCount = MaterialSlotCount();
			for (int index = 0; index < slotCount; ++index)
			{
				auto* row = new MaterialSlotRow(index, MaterialKeyAt(index), AlbedoAt(index), keys,
					nullptr, nullptr, container);

				// The row has to update itself after the swap: the name and the thumbnail are what
				// shows which material the slot holds now.
				row->SetHandlers(
					[this, index, row](const QString& p_key) {
						if (p_key.isEmpty()) { ClearMaterialAt(index); }
						else { AssignMaterialAt(index, p_key); }
						row->Refresh(MaterialKeyAt(index), AlbedoAt(index));
					},
					[this, index, row] {
						ClearMaterialAt(index);
						row->Refresh(MaterialKeyAt(index), AlbedoAt(index));
					});
				layout->addWidget(row);

				auto* separator = new QFrame(container);
				separator->setFrameShape(QFrame::HLine);
				separator->setStyleSheet(QStringLiteral("color: #2e2e2e;"));
				layout->addWidget(separator);
			}

			if (m_form != nullptr)
			{
				layout->addWidget(m_form);
			}
			return container;
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
		/** How many material slots to show: the number the actor's model splits its sub-ranges
		 * over, and at least two - a CAD face mesh keeps its transparent variant on slot 1 even
		 * when the model reports a single material name. */
		int MaterialSlotCount() const
		{
			int count = 2;
			if (m_renderer != nullptr)
			{
				if (auto* modelRenderer = m_renderer->owner.GetComponent<Core::ECS::Components::CModelRenderer>())
				{
					if (auto* model = modelRenderer->GetModel())
					{
						count = std::max(count, static_cast<int>(model->GetMaterialNames().size()));
					}
				}
			}
			return std::min(count, 8);
		}

		/** The name of the material in a slot, as the user should see it: the asset's file name
		 * when the slot holds an asset, "instance of <asset>" when it holds an instance of one. */
		QString MaterialNameAt(int p_index) const
		{
			if (m_renderer == nullptr)
			{
				return {};
			}
			auto* instance = m_renderer->GetMaterialAtIndex(static_cast<uint8_t>(p_index));
			if (instance == nullptr)
			{
				return QStringLiteral("<none>");
			}
			if (auto* asset = dynamic_cast<::Core::Resources::Material*>(instance))
			{
				return QFileInfo(QString::fromStdString(asset->path)).fileName();
			}
			if (auto* definition = dynamic_cast<::Core::Resources::Material*>(instance->Definition()))
			{
				return QStringLiteral("instance of ") + QFileInfo(QString::fromStdString(definition->path)).fileName();
			}
			return QStringLiteral("<instance>");
		}

		/** Gives the material file to the slot. The component owns an instance of it, so the asset
		 * stays shared: a shape's own values (the CAD domain colour table, the pick colour) land on
		 * the instance and never on the material the browser shows. */
		/** The asset key behind a slot, for the drop-down's selection: the asset's own path when the
		 * slot holds one, the definition's when it holds an instance of it. */
		QString MaterialKeyAt(int p_index) const
		{
			if (m_renderer == nullptr)
			{
				return {};
			}
			auto* instance = m_renderer->GetMaterialAtIndex(static_cast<uint8_t>(p_index));
			if (instance == nullptr)
			{
				return {};
			}
			if (auto* asset = dynamic_cast<::Core::Resources::Material*>(instance))
			{
				return QString::fromStdString(asset->path);
			}
			if (auto* definition = dynamic_cast<::Core::Resources::Material*>(instance->Definition()))
			{
				return QString::fromStdString(definition->path);
			}
			return {};
		}

		/** The slot's base colour, for the painted thumbnail. Falls back to grey when the material
		 * has no u_Albedo (an unlit line material, for instance). */
		QColor AlbedoAt(int p_index) const
		{
			if (m_renderer == nullptr)
			{
				return {};
			}
			auto* instance = m_renderer->GetMaterialAtIndex(static_cast<uint8_t>(p_index));
			if (instance == nullptr)
			{
				return {};
			}
			const auto property = instance->GetProperty("u_Albedo");
			if (!property.has_value() || !std::holds_alternative<Maths::FVector4>(property.value().value))
			{
				return QColor(150, 150, 150);
			}
			const auto& colour = std::get<Maths::FVector4>(property.value().value);
			return QColor::fromRgbF(std::clamp(colour.x, 0.0f, 1.0f), std::clamp(colour.y, 0.0f, 1.0f),
				std::clamp(colour.z, 0.0f, 1.0f));
		}
		/** Empties the slot: nothing is drawn for the sub-ranges that read it. */
		void ClearMaterialAt(int p_index)
		{
			if (m_renderer == nullptr)
			{
				return;
			}
			m_renderer->RemoveMaterialAtIndex(static_cast<uint8_t>(p_index));
			GetService(::Editor::Panels::SceneView).GetRenderer().InvalidateParsedDrawables();
		}
		void AssignMaterialAt(int p_index, const QString& p_path)
		{
			if (m_renderer == nullptr)
			{
				return;
			}

			const std::string key = (":Materials/" + QFileInfo(p_path).fileName()).toStdString();
			auto* material = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
				.GetResource(key, true);
			if (material == nullptr)
			{
				CORE_WARN("[Material] could not load {} for slot {}", key, p_index);
				return;
			}

			m_renderer->SetMaterialInstanceAtIndex(static_cast<uint8_t>(p_index), material->CreateInstance());
			if (p_index == 0)
			{
				// The parameter form below the list edits slot 0.
				mat = m_renderer->GetMaterialAtIndex(0);
			}

			// The drawables were parsed against the old material list.
			GetService(::Editor::Panels::SceneView).GetRenderer().InvalidateParsedDrawables();
			CORE_INFO("[Material] {} assigned to slot {} of {}", key, p_index, m_renderer->owner.GetName());
		}

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
			// Reset only the parameters the panel edits. Clearing the whole property map and
			// rebuilding it from the shader defaults also recreated the engine-owned samplers
			// (_IrradianceCube, _PrefilterCube, _BRDFLut, _ShadowMap, the CAD domain colour
			// buffer and the depth-peel textures) as null textures: the next draw then bound
			// the empty 2D fallback to their units, which crashed on the CAD material.
			for (const auto& parameter : mat->GetShader()->GetParameters()) {
				if (parameter.isUi) {
					mat->ResetPropertyToDefault(parameter.name);
				}
			}
			for (auto* property : mProperties) {
				property->updateWidgetValue(getPropertyValue(property->getPropertyName()));
			}
		}

		::Rendering::Data::MaterialInstance* mat = nullptr;
		Core::ECS::Components::CMaterialRenderer* m_renderer = nullptr;
		QWidget* m_form = nullptr;
		std::map<QString, std::string> m_uniformNames;
		std::map<QString, std::string> m_featureNames;
		std::set<std::string> m_colorNames;
	};
	/** A model renderer's meshes: what the actor draws, how heavy each mesh is, and which material
	 * slot every sub-range reads. Read-only: the geometry comes from the document / the import. */
	class ModelRendererPropertyComponent : public ActorPropertyComponent
	{
	public:
		ModelRendererPropertyComponent(Core::ECS::Components::CModelRenderer* p_component)
			: ActorPropertyComponent(p_component), m_renderer(p_component)
		{
		}

		QWidget* createCustomWidget(QWidget* p_parent) override
		{
			auto* container = new QWidget(p_parent);
			auto* layout = new QVBoxLayout(container);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(2);

			auto* title = new QLabel(QStringLiteral("Meshes"), container);
			title->setMargin(6);
			layout->addWidget(title);

			auto* model = m_renderer != nullptr ? m_renderer->GetModel() : nullptr;
			if (model == nullptr)
			{
				auto* hint = new QLabel(QStringLiteral("This actor has no model."), container);
				hint->setMargin(6);
				layout->addWidget(hint);
				return container;
			}

			const auto& meshes = model->GetMeshes();
			for (size_t i = 0; i < meshes.size(); ++i)
			{
				auto* mesh = meshes[i];
				if (mesh == nullptr) { continue; }

				const auto& materialIndices = mesh->GetMaterialIndex();
				QString materials;
				for (size_t m = 0; m < materialIndices.size(); ++m)
				{
					materials += (m == 0 ? QString() : QStringLiteral(", "))
						+ QString::number(materialIndices[m]);
				}

				auto* row = new QLabel(
					QStringLiteral("%1.  %2 verts   %3 indices   material %4")
						.arg(static_cast<int>(i))
						.arg(mesh->GetVertexCount())
						.arg(mesh->GetIndexCount())
						.arg(materials.isEmpty() ? QStringLiteral("-") : materials),
					container);
				row->setMargin(6);
				row->setTextInteractionFlags(Qt::TextSelectableByMouse);
				layout->addWidget(row);
			}
			return container;
		}

	private:
		Core::ECS::Components::CModelRenderer* m_renderer = nullptr;
	};

	ActorPropertyComponent* transferActorPropertyComponent(Core::ECS::Components::AComponent* comp) {

		if (auto trans = dynamic_cast<Core::ECS::Components::CTransform*>(comp)) {
			return new  TransFormPropertyComponent(trans);
		}
		else if (auto renderer = dynamic_cast<Core::ECS::Components::CModelRenderer*>(comp)) {
			return new ModelRendererPropertyComponent(renderer);
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
