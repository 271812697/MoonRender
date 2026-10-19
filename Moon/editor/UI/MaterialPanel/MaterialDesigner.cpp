#include "MaterialDesigner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Global/ServiceLocator.h"
#include "Core/Resources/Loaders/MaterialLoader.h"
#include "Core/Resources/Material.h"
#include "Core/ResourceManagement/MaterialManager.h"
#include "Core/ResourceManagement/ShaderManager.h"
#include "Core/ResourceManagement/TextureManager.h"
#include "Core/SceneSystem/SceneManager.h"
#include "Rendering/Context/Driver.h"
#include "Rendering/Resources/Shader.h"
#include "Rendering/Resources/Texture.h"
#include "Tools/Utils/PathParser.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/ColorPickerProperty.h"
#include "Widgets/FVec3Property.h"
#include "Widgets/FVec4Property.h"
#include "Widgets/PropertyComponent.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/SliderIntProperty.h"
#include "Widgets/Vec2Property.h"
#include "core/ECS/Actor.h"
#include "core/ECS/Components/CMaterialRenderer.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "renderer/Context.h"
#include "renderer/SceneView.h"

namespace MOON
{
	namespace
	{
		using Core::Resources::Material;

		std::string ToLower(std::string p_text)
		{
			std::ranges::transform(p_text, p_text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return p_text;
		}

		/** "u_TextureTiling" -> "Texture Tiling": the label a designer reads, while the raw
		 * uniform name stays in the tooltip. */
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

		bool IsColorParameter(const std::string& p_name)
		{
			const std::string name = ToLower(p_name);
			return name.find("albedo") != std::string::npos
				|| name.find("color") != std::string::npos
				|| name.find("tint") != std::string::npos;
		}

		/** "PARALLAX_MAPPING" -> "Parallax Mapping", the label of a feature toggle. */
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

		bool IsAlbedoParameter(const std::string& p_name)
		{
			return ToLower(p_name).find("albedo") != std::string::npos;
		}

		/** The range a unit-less material scalar gets.
		 *
		 * The shader DSL carries no slider metadata (unlike filament's material designer, which
		 * has a schema), so the name and the current value are all there is: the usual surface
		 * factors are unit scalars, the intensities and distances are not, and everything else
		 * gets a range around its current value. */
		struct SliderRange
		{
			float min = 0.0f;
			float max = 1.0f;
			float step = 0.005f;
		};

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

		/** Loading a material or a texture compiles/creates GL objects, and the designer runs from
		 * widget signals, where no context is current. The module's context is the one they belong
		 * on, so it is made current for the duration of such a load. */
		class ModuleContextScope
		{
		public:
			ModuleContextScope()
			{
				auto& context = Core::Global::ServiceLocator::Get<Editor::Core::Context>();
				m_host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
				m_ready = m_host != nullptr && m_host->MakeCurrent();
			}

			~ModuleContextScope()
			{
				if (m_ready)
				{
					m_host->DoneCurrent();
				}
			}

			explicit operator bool() const { return m_ready; }

		private:
			Rendering::Context::IGLContextHost* m_host = nullptr;
			bool m_ready = false;
		};

		/** The backend the parameter widgets read from and write to: one material instance.
		 *
		 * The widgets are labelled with the pretty name ("Albedo Map"), while the material is
		 * keyed by the shader's own names ("u_AlbedoMap"); the map keeps the two apart. */
		class DesignerMaterialComponent : public PropertyComponent
		{
		public:
			DesignerMaterialComponent(Material& p_material,
				std::map<QString, std::string> p_uniformNames,
				std::map<QString, std::string> p_featureNames,
				std::map<QString, std::string> p_stateNames,
				std::set<std::string> p_colorNames,
				std::function<void()> p_onChanged)
				: m_material(p_material),
				m_onChanged(std::move(p_onChanged)),
				m_uniformNames(std::move(p_uniformNames)),
				m_featureNames(std::move(p_featureNames)),
				m_stateNames(std::move(p_stateNames)),
				m_colorNames(std::move(p_colorNames))
			{
			}

			QString getComponentName() override
			{
				return QStringLiteral("Material");
			}

			QVariant getPropertyValue(const QString& p_name) override
			{
				using namespace Maths;

				if (const auto it = m_featureNames.find(p_name); it != m_featureNames.end())
				{
					return m_material.HasFeature(it->second);
				}
				if (const auto it = m_stateNames.find(p_name); it != m_stateNames.end())
				{
					return StateValue(it->second);
				}

				const std::string name = UniformName(p_name);
				const auto property = m_material.GetProperty(name);
				if (!property)
				{
					return {};
				}

				const auto& value = property->value;
				if (std::holds_alternative<bool>(value)) { return std::get<bool>(value); }
				if (std::holds_alternative<int>(value)) { return std::get<int>(value); }
				if (std::holds_alternative<float>(value)) { return std::get<float>(value); }
				if (std::holds_alternative<FVector2>(value)) { return QVariant::fromValue(std::get<FVector2>(value)); }
				if (std::holds_alternative<FVector3>(value)) { return QVariant::fromValue(std::get<FVector3>(value)); }
				if (std::holds_alternative<FVector4>(value))
				{
					const FVector4& vector = std::get<FVector4>(value);
					if (m_colorNames.contains(name))
					{
						return QColor::fromRgbF(vector.x, vector.y, vector.z, vector.w);
					}
					return QVariant::fromValue(vector);
				}
				return {};
			}

			void setPropertyValue(const QString& p_name, const QVariant& p_value) override
			{
				using namespace Maths;

				if (const auto it = m_featureNames.find(p_name); it != m_featureNames.end())
				{
					const bool enabled = p_value.toBool();
					if (m_material.HasFeature(it->second) == enabled)
					{
						return; // the checkbox reporting its initial state is not an edit
					}
					if (enabled)
					{
						m_material.AddFeature(it->second);
					}
					else
					{
						m_material.RemoveFeature(it->second);
					}
					if (m_onChanged) { m_onChanged(); }
					return;
				}
				if (const auto it = m_stateNames.find(p_name); it != m_stateNames.end())
				{
					const bool enabled = p_value.toBool();
					if (StateValue(it->second).toBool() == enabled)
					{
						return;
					}
					SetStateValue(it->second, enabled);
					if (m_onChanged) { m_onChanged(); }
					return;
				}

				const std::string name = UniformName(p_name);
				const auto property = m_material.GetProperty(name);
				if (!property)
				{
					return;
				}

				const auto& current = property->value;
				if (SameValue(current, p_value))
				{
					// A widget that pushes its value back while it is being created (ColorPicker
					// does, and the sliders clamp) must not turn "opened a material" into "edited
					// it" - that would also reset state the material file had set.
					return;
				}
				if (p_value.userType() == QMetaType::QColor)
				{
					const QColor color = p_value.value<QColor>();
					m_material.SetProperty(name, FVector4{
						static_cast<float>(color.redF()),
						static_cast<float>(color.greenF()),
						static_cast<float>(color.blueF()),
						static_cast<float>(color.alphaF()) });

					if (IsAlbedoParameter(name))
					{
						// The same rule the property panel applies to u_Albedo: alpha below 1
						// makes the instance transparent, so the preview and the viewport agree.
						// An explicit add/multiply/screen mode is the user's choice, though - the
						// alpha of the colour must not drag it back to the plain alpha blend.
						using Rendering::Settings::EBlendMode;
						if (m_material.GetBlendMode() == EBlendMode::BLEND_OPAQUE
							|| m_material.GetBlendMode() == EBlendMode::BLEND_TRANSPARENT)
						{
							const bool transparent = color.alphaF() < 0.999f;
							m_material.SetTransparent(transparent);
							m_material.SetBlendable(transparent);
							m_material.SetDepthWriting(true);
						}
					}
				}
				else if (std::holds_alternative<bool>(current)) { m_material.SetProperty(name, p_value.toBool()); }
				else if (std::holds_alternative<int>(current)) { m_material.SetProperty(name, p_value.toInt()); }
				else if (std::holds_alternative<float>(current)) { m_material.SetProperty(name, p_value.toFloat()); }
				else if (std::holds_alternative<FVector2>(current)) { m_material.SetProperty(name, p_value.value<FVector2>()); }
				else if (std::holds_alternative<FVector3>(current)) { m_material.SetProperty(name, p_value.value<FVector3>()); }
				else if (std::holds_alternative<FVector4>(current)) { m_material.SetProperty(name, p_value.value<FVector4>()); }
				else
				{
					return;
				}

				if (m_onChanged)
				{
					m_onChanged();
				}
			}

		private:
			/** The material's renderer state, addressed by the ids the form maps labels to. An
			 * instance starts from the shader's material block; these values are its overrides. */
			QVariant StateValue(const std::string& p_state) const
			{
				if (p_state == "blendable") { return m_material.IsBlendable(); }
				if (p_state == "transparent") { return m_material.IsTransparent(); }
				if (p_state == "backfaceCulling") { return m_material.HasBackfaceCulling(); }
				if (p_state == "frontfaceCulling") { return m_material.HasFrontfaceCulling(); }
				if (p_state == "depthTest") { return m_material.HasDepthTest(); }
				if (p_state == "depthWriting") { return m_material.HasDepthWriting(); }
				if (p_state == "castShadows") { return m_material.IsShadowCaster(); }
				if (p_state == "receiveShadows") { return m_material.IsShadowReceiver(); }
				return {};
			}

			void SetStateValue(const std::string& p_state, bool p_value)
			{
				if (p_state == "blendable") { m_material.SetBlendable(p_value); }
				else if (p_state == "transparent") { m_material.SetTransparent(p_value); }
				else if (p_state == "backfaceCulling") { m_material.SetBackfaceCulling(p_value); }
				else if (p_state == "frontfaceCulling") { m_material.SetFrontfaceCulling(p_value); }
				else if (p_state == "depthTest") { m_material.SetDepthTest(p_value); }
				else if (p_state == "depthWriting") { m_material.SetDepthWriting(p_value); }
				else if (p_state == "castShadows") { m_material.SetCastShadows(p_value); }
				else if (p_state == "receiveShadows") { m_material.SetReceiveShadows(p_value); }
			}

			/** Whether p_incoming already is what the material stores, so a widget that reports
			 * its initial value back does not look like an edit. */
			bool SameValue(const Rendering::Data::MaterialPropertyType& p_current, const QVariant& p_incoming) const
			{
				using namespace Maths;

				if (p_incoming.userType() == QMetaType::QColor)
				{
					if (!std::holds_alternative<FVector4>(p_current))
					{
						return false;
					}
					const FVector4& stored = std::get<FVector4>(p_current);
					const QColor color = p_incoming.value<QColor>();
					return std::abs(stored.x - static_cast<float>(color.redF())) < 1e-4f
						&& std::abs(stored.y - static_cast<float>(color.greenF())) < 1e-4f
						&& std::abs(stored.z - static_cast<float>(color.blueF())) < 1e-4f
						&& std::abs(stored.w - static_cast<float>(color.alphaF())) < 1e-4f;
				}
				if (std::holds_alternative<bool>(p_current)) { return std::get<bool>(p_current) == p_incoming.toBool(); }
				if (std::holds_alternative<int>(p_current)) { return std::get<int>(p_current) == p_incoming.toInt(); }
				if (std::holds_alternative<float>(p_current)) { return std::abs(std::get<float>(p_current) - p_incoming.toFloat()) < 1e-6f; }
				if (std::holds_alternative<FVector2>(p_current))
				{
					const FVector2& stored = std::get<FVector2>(p_current);
					const FVector2 incoming = p_incoming.value<FVector2>();
					return stored.x == incoming.x && stored.y == incoming.y;
				}
				if (std::holds_alternative<FVector3>(p_current))
				{
					const FVector3& stored = std::get<FVector3>(p_current);
					const FVector3 incoming = p_incoming.value<FVector3>();
					return stored.x == incoming.x && stored.y == incoming.y && stored.z == incoming.z;
				}
				if (std::holds_alternative<FVector4>(p_current))
				{
					const FVector4& stored = std::get<FVector4>(p_current);
					const FVector4 incoming = p_incoming.value<FVector4>();
					return stored.x == incoming.x && stored.y == incoming.y
						&& stored.z == incoming.z && stored.w == incoming.w;
				}
				return false;
			}

			std::string UniformName(const QString& p_label) const
			{
				const auto it = m_uniformNames.find(p_label);
				return it != m_uniformNames.end() ? it->second : p_label.toStdString();
			}

			Material& m_material;
			std::function<void()> m_onChanged;
			std::map<QString, std::string> m_uniformNames;
			std::map<QString, std::string> m_featureNames;
			std::map<QString, std::string> m_stateNames;
			std::set<std::string> m_colorNames;
		};

		/** A texture slot: the assets the engine ships as a combo box, a thumbnail next to it,
		 * and the material property written straight away (a Texture* is what the upload binds;
		 * its path is what the .ovmat stores). */
		class TextureSlotRow : public QWidget
		{
		public:
			TextureSlotRow(const QString& p_label, const std::string& p_name, Material& p_material,
				const std::vector<MaterialDesigner::TextureAsset>& p_assets,
				const std::function<void()>& p_onChanged, QWidget* p_parent)
				: QWidget(p_parent),
				m_material(p_material),
				m_name(p_name),
				m_onChanged(p_onChanged)
			{
				auto* layout = new QHBoxLayout(this);
				layout->setContentsMargins(0, 0, 0, 0);
				layout->setSpacing(6);

				auto* label = new QLabel(p_label, this);
				label->setObjectName("PropertyLabel");
				label->setMinimumWidth(110);
				label->setMaximumWidth(170);
				label->setToolTip(QString::fromStdString(p_name));
				layout->addWidget(label);

				m_combo = new QComboBox(this);
				m_combo->setMaxVisibleItems(20);
				m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
				m_combo->setMinimumContentsLength(10);
				m_combo->addItem(QStringLiteral("None"), QString());
				for (const auto& asset : p_assets)
				{
					m_combo->addItem(asset.icon, asset.label, QString::fromStdString(asset.key));
				}
				layout->addWidget(m_combo, 1);

				m_thumb = new QLabel(this);
				m_thumb->setObjectName("MaterialTextureThumb");
				m_thumb->setFixedSize(26, 26);
				layout->addWidget(m_thumb);

				QString key;
				if (const auto property = p_material.GetProperty(p_name))
				{
					if (std::holds_alternative<Rendering::Resources::Texture*>(property->value))
					{
						if (auto* texture = std::get<Rendering::Resources::Texture*>(property->value))
						{
							key = QString::fromStdString(texture->path);
						}
					}
				}

				int index = key.isEmpty() ? 0 : m_combo->findData(key);
				if (index < 0)
				{
					m_combo->addItem(key, key);
					index = m_combo->count() - 1;
				}
				m_combo->setCurrentIndex(index);
				UpdateThumbnail();

				QObject::connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
					this, [this](int) { Apply(); });
			}

		private:
			void Apply()
			{
				const QString key = m_combo->currentData().toString();

				Rendering::Resources::Texture* texture = nullptr;
				if (!key.isEmpty())
				{
					// Textures are GL objects too; the combo's signal runs with no context current.
					ModuleContextScope context;
					if (!context)
					{
						return;
					}
					texture = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>()
						.GetResource(key.toStdString(), true);
				}
				m_material.SetProperty(m_name, texture);
				UpdateThumbnail();

				if (m_onChanged)
				{
					m_onChanged();
				}
			}

			void UpdateThumbnail()
			{
				const int index = m_combo->currentIndex();
				if (index <= 0)
				{
					m_thumb->clear();
					return;
				}

				const QIcon icon = m_combo->itemIcon(index);
				if (!icon.isNull())
				{
					m_thumb->setPixmap(icon.pixmap(26, 26));
					return;
				}

				// An asset without a thumbnail (HDR/EXR/...). Nothing to show, but the name is
				// still in the combo; leave the cell empty rather than a wrong picture.
				m_thumb->clear();
			}

			Material& m_material;
			std::string m_name;
			std::function<void()> m_onChanged;
			QComboBox* m_combo = nullptr;
			QLabel* m_thumb = nullptr;
		};

		/** The material's blend mode, filament's BlendMode. A combo rather than a checkbox: the
		 * renderer picks different factors for alpha, additive, multiply, screen and fade, and
		 * reads the choice from the state mask. */
		class BlendModeRow : public QWidget
		{
		public:
			BlendModeRow(const QString& p_label, Material& p_material,
				const std::function<void()>& p_onChanged, QWidget* p_parent)
				: QWidget(p_parent),
				m_material(p_material),
				m_onChanged(p_onChanged)
			{
				using Rendering::Settings::EBlendMode;

				auto* layout = new QHBoxLayout(this);
				layout->setContentsMargins(0, 0, 0, 0);
				layout->setSpacing(6);

				auto* label = new QLabel(p_label, this);
				label->setObjectName("PropertyLabel");
				label->setMinimumWidth(110);
				label->setMaximumWidth(170);
				layout->addWidget(label);

				m_combo = new QComboBox(this);
				m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
				m_combo->setMinimumContentsLength(10);
				m_combo->addItem(QStringLiteral("Opaque"), static_cast<int>(EBlendMode::BLEND_OPAQUE));
				m_combo->addItem(QStringLiteral("Transparent"), static_cast<int>(EBlendMode::BLEND_TRANSPARENT));
				m_combo->addItem(QStringLiteral("Add"), static_cast<int>(EBlendMode::BLEND_ADD));
				m_combo->addItem(QStringLiteral("Multiply"), static_cast<int>(EBlendMode::BLEND_MULTIPLY));
				m_combo->addItem(QStringLiteral("Screen"), static_cast<int>(EBlendMode::BLEND_SCREEN));
				m_combo->addItem(QStringLiteral("Fade"), static_cast<int>(EBlendMode::BLEND_FADE));
				m_combo->setCurrentIndex(
					m_combo->findData(static_cast<int>(p_material.GetBlendMode())));
				layout->addWidget(m_combo, 1);

				QObject::connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
					[this](int) {
						m_material.SetBlendMode(static_cast<Rendering::Settings::EBlendMode>(
							m_combo->currentData().toInt()));
						if (m_onChanged)
						{
							m_onChanged();
						}
					});
			}

		private:
			Material& m_material;
			std::function<void()> m_onChanged;
			QComboBox* m_combo = nullptr;
		};

		/** ":Shaders\\Standard.mat" for "Standard.mat", the key the shader manager is keyed by. */
		std::string ToShaderKey(const std::string& p_relative)
		{
			std::string key = ":Shaders\\";
			for (const char c : p_relative)
			{
				key += (c == '/' ? '\\' : c);
			}
			return key;
		}

		/** The little square a browser row shows until its material has been previewed: a sphere
		 * outline on the panel's dark background, so the list keeps its rhythm. */
		QIcon PlaceholderThumbnail()
		{
			static const QIcon icon = [] {
				QPixmap pixmap(44, 44);
				pixmap.fill(Qt::transparent);

				QPainter painter(&pixmap);
				painter.setRenderHint(QPainter::Antialiasing, true);
				painter.setPen(QPen(QColor(0x4a, 0x4a, 0x50), 1.0));
				painter.setBrush(QColor(0x2c, 0x2c, 0x30));
				painter.drawRoundedRect(QRectF(0.5, 0.5, 43.0, 43.0), 4.0, 4.0);

				painter.setPen(QPen(QColor(0x6f, 0x75, 0x7e), 1.2));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(QPointF(22.0, 22.0), 11.0, 11.0);
				painter.setPen(QPen(QColor(0x55, 0x5b, 0x63), 1.0));
				painter.drawArc(QRectF(14.0, 18.0, 16.0, 8.0), 0, 180 * 16);
				painter.end();

				return QIcon(pixmap);
			}();
			return icon;
		}
	}

	MaterialDesigner::MaterialDesigner(QWidget* p_parent) : QWidget(p_parent)
	{
		const std::string engineRoot = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/";
		m_materialsRoot = engineRoot + "Materials/";
		m_shadersRoot = engineRoot + "Shaders/";
		m_texturesRoot = engineRoot + "Textures/";

		setObjectName("MaterialDesigner");
		setAttribute(Qt::WA_StyledBackground, true);

		BuildUi();
		ScanTextures();

		// Nothing is loaded here on purpose: loading a material compiles its shader, and this
		// panel can be constructed before the render module's context exists (the editor builds
		// its docks first). Refresh() - on show, and when the Material page is opened - loads.
		RebuildForm();
	}

	MaterialDesigner::~MaterialDesigner() = default;

	Core::Resources::Material* MaterialDesigner::GetMaterial() const
	{
		return m_material;
	}

	void MaterialDesigner::BuildUi()
	{
		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		// --- the actions ----------------------------------------------------------------------
		auto* bar = new QWidget(this);
		bar->setObjectName("MaterialDesignerBar");
		bar->setAttribute(Qt::WA_StyledBackground, true);
		auto* barLayout = new QHBoxLayout(bar);
		barLayout->setContentsMargins(6, 5, 6, 5);
		barLayout->setSpacing(4);

		m_new = new QToolButton(bar);
		m_new->setObjectName("MaterialButton");
		m_new->setText(QStringLiteral("New"));
		m_new->setToolTip(QStringLiteral("Create a material from a shader"));
		m_new->setCursor(Qt::PointingHandCursor);

		m_duplicate = new QToolButton(bar);
		m_duplicate->setObjectName("MaterialButton");
		m_duplicate->setText(QStringLiteral("Duplicate"));
		m_duplicate->setToolTip(QStringLiteral("Save a copy of this material under a new name"));
		m_duplicate->setCursor(Qt::PointingHandCursor);

		m_revert = new QToolButton(bar);
		m_revert->setObjectName("MaterialButton");
		m_revert->setText(QStringLiteral("Revert"));
		m_revert->setToolTip(QStringLiteral("Discard the edits and read the .ovmat from disk again"));
		m_revert->setCursor(Qt::PointingHandCursor);

		m_save = new QToolButton(bar);
		m_save->setObjectName("MaterialPrimaryButton");
		m_save->setText(QStringLiteral("Save"));
		m_save->setToolTip(QStringLiteral("Write the .ovmat back to disk"));
		m_save->setCursor(Qt::PointingHandCursor);

		m_assign = new QToolButton(bar);
		m_assign->setObjectName("MaterialButton");
		m_assign->setText(QStringLiteral("Assign"));
		m_assign->setToolTip(QStringLiteral("Give this material to the selected actor"));
		m_assign->setCursor(Qt::PointingHandCursor);

		barLayout->addStretch(1);
		barLayout->addWidget(m_new);
		barLayout->addWidget(m_duplicate);
		barLayout->addWidget(m_revert);
		barLayout->addWidget(m_save);
		barLayout->addWidget(m_assign);
		layout->addWidget(bar);

		// --- the browser and the generated form -----------------------------------------------
		auto* content = new QSplitter(Qt::Horizontal, this);
		content->setHandleWidth(4);
		content->setChildrenCollapsible(false);

		m_browser = new QListWidget(content);
		m_browser->setObjectName("MaterialBrowser");
		m_browser->setUniformItemSizes(true);
		m_browser->setIconSize(QSize(44, 44));
		m_browser->setSelectionMode(QAbstractItemView::SingleSelection);
		m_browser->setMinimumWidth(120);
		m_browser->setMaximumWidth(280);
		m_browser->setToolTip(QStringLiteral("The material assets under Engine/Materials"));

		m_scroll = new QScrollArea(content);
		m_scroll->setObjectName("MaterialDesignerScroll");
		m_scroll->setWidgetResizable(true);
		m_scroll->setFrameShape(QFrame::NoFrame);
		m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		m_form = new QWidget();
		m_form->setObjectName("MaterialDesignerForm");
		m_form->setAttribute(Qt::WA_StyledBackground, true);
		m_formLayout = new QVBoxLayout(m_form);
		m_formLayout->setContentsMargins(0, 0, 0, 0);
		m_formLayout->setSpacing(0);
		m_scroll->setWidget(m_form);

		content->addWidget(m_browser);
		content->addWidget(m_scroll);
		content->setStretchFactor(0, 0);
		content->setStretchFactor(1, 1);
		content->setSizes({ 150, 420 });
		layout->addWidget(content, 1);

		m_status = new QLabel(this);
		m_status->setObjectName("MaterialDesignerStatus");
		m_status->setText(QStringLiteral("No material selected"));
		m_status->setMinimumHeight(20);
		// Long paths must not widen the panel: the label clips (the full text is the tooltip).
		m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		layout->addWidget(m_status);

		QObject::connect(m_browser, &QListWidget::currentRowChanged,
			this, [this](int p_index) { OnMaterialSelected(p_index); });
		QObject::connect(m_new, &QToolButton::clicked, this, [this] { NewMaterial(); });
		QObject::connect(m_duplicate, &QToolButton::clicked, this, [this] { DuplicateMaterial(); });
		QObject::connect(m_revert, &QToolButton::clicked, this, [this] { RevertMaterial(); });
		QObject::connect(m_save, &QToolButton::clicked, this, [this] { SaveMaterial(); });
		QObject::connect(m_assign, &QToolButton::clicked, this, [this] { AssignToSelection(); });

		// Right-clicking the browser is where the destructive action lives: a material an actor
		// is drawing cannot be deleted, so Delete explains itself instead of being a button that
		// always looks available.
		m_browser->setContextMenuPolicy(Qt::CustomContextMenu);
		QObject::connect(m_browser, &QWidget::customContextMenuRequested, this,
			[this](const QPoint& p_position) {
				if (auto* item = m_browser->itemAt(p_position); item != nullptr)
				{
					m_browser->setCurrentItem(item);
				}
				QMenu menu(this);
				QAction* undo = menu.addAction(QStringLiteral("Undo"));
				undo->setShortcut(QKeySequence::Undo);
				undo->setEnabled(!m_undo.empty());
				QAction* redo = menu.addAction(QStringLiteral("Redo"));
				redo->setShortcut(QKeySequence::Redo);
				redo->setEnabled(!m_redo.empty());
				menu.addSeparator();
				QAction* duplicate = menu.addAction(QStringLiteral("Duplicate"));
				QAction* remove = menu.addAction(QStringLiteral("Delete"));
				QAction* chosen = menu.exec(m_browser->mapToGlobal(p_position));
				if (chosen == undo) { Undo(); }
				else if (chosen == redo) { Redo(); }
				else if (chosen == duplicate) { DuplicateMaterial(); }
				else if (chosen == remove) { DeleteMaterial(); }
			});

		// Undo/redo are scoped to the designer's page, so the shader editor in the other page
		// keeps its own undo stack.
		auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
		undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(undoShortcut, &QShortcut::activated, this, [this] { Undo(); });

		auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
		redoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(redoShortcut, &QShortcut::activated, this, [this] { Redo(); });

		// QKeySequence::Redo is Ctrl+Y on Windows; the other common spelling is Ctrl+Shift+Z.
		auto* redoAlternate = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
		redoAlternate->setContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(redoAlternate, &QShortcut::activated, this, [this] { Redo(); });
	}

	void MaterialDesigner::ScanTextures()
	{
		m_textures.clear();

		static const std::set<std::string> kImageExtensions = {
			".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr", ".dds"
		};

		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_texturesRoot, error))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}
			const std::string extension = ToLower(entry.path().extension().string());
			if (!kImageExtensions.contains(extension))
			{
				continue;
			}

			TextureAsset asset;
			asset.key = ":Textures/" + std::filesystem::relative(entry.path(), m_texturesRoot).generic_string();
			asset.label = QString::fromStdString(asset.key.substr(10));

			// HDR/EXR/DDS have no cheap QImage preview; the name alone is enough for them.
			if (extension != ".hdr" && extension != ".exr" && extension != ".dds")
			{
				const QImage image(QString::fromStdString(entry.path().string()));
				if (!image.isNull())
				{
					asset.icon = QIcon(QPixmap::fromImage(
						image.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				}
			}
			m_textures.push_back(std::move(asset));
		}

		std::ranges::sort(m_textures, {}, &TextureAsset::key);
	}

	void MaterialDesigner::Refresh()
	{
		ScanTextures();
		RefreshMaterialList(m_selectedKey);
		if (m_material != nullptr)
		{
			RebuildForm();
		}
		emit materialShown(m_material);
	}

	void MaterialDesigner::RefreshMaterialList(const std::string& p_select)
	{
		const std::string select = p_select.empty() ? m_selectedKey : p_select;

		m_browser->blockSignals(true);
		m_browser->clear();
		m_materialKeys.clear();

		std::vector<std::string> files;
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(m_materialsRoot, error))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".ovmat")
			{
				files.push_back(entry.path().filename().generic_string());
			}
		}
		std::ranges::sort(files);

		for (const std::string& file : files)
		{
			const std::string key = ":Materials/" + file;
			m_materialKeys.push_back(key);

			auto* item = new QListWidgetItem(QString::fromStdString(std::filesystem::path(file).stem().string()));
			item->setToolTip(QString::fromStdString(key));
			if (const auto thumbnail = m_thumbnails.find(key); thumbnail != m_thumbnails.end())
			{
				item->setIcon(thumbnail->second);
			}
			else
			{
				item->setIcon(PlaceholderThumbnail());
			}
			m_browser->addItem(item);
		}
		m_browser->blockSignals(false);

		int index = -1;
		for (size_t i = 0; i < m_materialKeys.size(); ++i)
		{
			if (m_materialKeys[i] == select)
			{
				index = static_cast<int>(i);
				break;
			}
		}
		if (index < 0 && !m_materialKeys.empty())
		{
			index = 0;
		}

		if (index < 0)
		{
			m_material = nullptr;
			m_selectedKey.clear();
			RebuildForm();
			emit materialShown(nullptr);
			return;
		}

		m_browser->blockSignals(true);
		m_browser->setCurrentRow(index);
		m_browser->blockSignals(false);
		OnMaterialSelected(index);
	}

	void MaterialDesigner::OnMaterialSelected(int p_index)
	{
		if (p_index < 0 || p_index >= static_cast<int>(m_materialKeys.size()))
		{
			m_material = nullptr;
			m_selectedKey.clear();
			RebuildForm();
			emit materialShown(nullptr);
			return;
		}

		const std::string key = m_materialKeys[p_index];
		if (key == m_selectedKey && m_material != nullptr)
		{
			// Same material, but the shader behind it may have been hot reloaded.
			RebuildForm();
			return;
		}

		Core::Resources::Material* material = nullptr;
		{
			ModuleContextScope context;
			if (!context)
			{
				SetStatus(QStringLiteral("The render context is not ready yet - open a project, then try again."), true);
				return;
			}
			material = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
				.GetResource(key, true);
		}
		if (material == nullptr)
		{
			SetStatus(QStringLiteral("Could not load ") + QString::fromStdString(key), true);
			return;
		}

		m_material = material;
		m_selectedKey = key;
		m_dirty = false;
		ResetHistory();
		RebuildForm();
		emit materialShown(m_material);
		SetStatus(QString::fromStdString(key));
	}

	void MaterialDesigner::SetThumbnail(const QImage& p_image)
	{
		if (m_material == nullptr || m_selectedKey.empty() || p_image.isNull())
		{
			return;
		}

		const QImage scaled = p_image.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		m_thumbnails[m_selectedKey] = QIcon(QPixmap::fromImage(scaled));

		// The capture is of what the preview is showing, which is the current item.
		if (auto* item = m_browser->currentItem(); item != nullptr)
		{
			item->setIcon(m_thumbnails[m_selectedKey]);
		}
	}

	void MaterialDesigner::RebuildForm()
	{
		while (auto* item = m_formLayout->takeAt(0))
		{
			delete item->widget();
			delete item;
		}
		m_component.reset();

		auto makeHint = [this](const QString& p_text) {
			auto* hint = new QLabel(p_text, m_form);
			hint->setObjectName("MaterialDesignerHint");
			hint->setWordWrap(true);
			hint->setMargin(10);
			m_formLayout->addWidget(hint);
		};

		if (m_material == nullptr)
		{
			makeHint(QStringLiteral("Pick a material above, or press New to make one from a shader."));
			m_formLayout->addStretch();
			return;
		}

		auto* shader = m_material->GetShader();
		if (shader == nullptr)
		{
			makeHint(QStringLiteral("This material has no shader."));
			m_formLayout->addStretch();
			return;
		}

		const std::vector<Rendering::Resources::Shader::Parameter>& parameters = shader->GetParameters();

		// Labels and the label -> uniform map. The property widgets are keyed by their label, the
		// material by the shader's uniform name, so the two have to stay paired. A label declared
		// twice (or two parameters deriving the same label) falls back to the uniform name.
		std::vector<QString> labels(parameters.size());
		std::map<QString, std::string> labelToUniform;
		std::set<QString> usedLabels;
		std::set<std::string> colorNames;
		for (size_t i = 0; i < parameters.size(); ++i)
		{
			const Rendering::Resources::Shader::Parameter& parameter = parameters[i];
			if (!parameter.isUi)
			{
				continue;
			}

			QString label = parameter.label.empty()
				? QString::fromStdString(PrettyLabel(parameter.name))
				: QString::fromStdString(parameter.label);
			if (usedLabels.contains(label))
			{
				label = QString::fromStdString(parameter.name);
			}
			usedLabels.insert(label);
			labels[i] = label;

			if (!parameter.isSampler)
			{
				labelToUniform.emplace(label, parameter.name);
				if (parameter.type == "vec4" && (parameter.isColor || IsColorParameter(parameter.name)))
				{
					colorNames.insert(parameter.name);
				}
			}
		}

		// Features are the shader's variant bits: toggling one selects another program variant,
		// which is why they sit above the parameters (PARALLAX_MAPPING adds the parallax ones).
		std::map<QString, std::string> featureNames;
		{
			std::vector<std::string> features(shader->GetFeatures().begin(), shader->GetFeatures().end());
			std::ranges::sort(features);
			for (const std::string& feature : features)
			{
				QString label = QString::fromStdString(FeatureLabel(feature));
				if (usedLabels.contains(label))
				{
					label = QString::fromStdString(feature);
				}
				usedLabels.insert(label);
				featureNames.emplace(label, feature);
			}
		}

		// The instance's renderer state: what a .ovmat stores besides the parameters and the
		// features. A material starts from the shader's material block (see Shader::State), and
		// these are the overrides the designer can pin.
		std::map<QString, std::string> stateNames;
		{
			const std::pair<const char*, const char*> states[] = {
				{ "Backface Culling", "backfaceCulling" },
				{ "Cast Shadows", "castShadows" },
				{ "Depth Writing", "depthWriting" },
				{ "Receive Shadows", "receiveShadows" },
			};
			for (const auto& [label, state] : states)
			{
				const QString name = QString::fromLatin1(label);
				if (usedLabels.contains(name))
				{
					continue;
				}
				usedLabels.insert(name);
				stateNames.emplace(name, state);
			}
		}

		m_component = std::make_unique<DesignerMaterialComponent>(
			*m_material, labelToUniform, featureNames, stateNames, colorNames, [this] { MarkDirty(); });

		// Groups: the group the material declared, else Parameters / Textures; created in
		// declaration order so the form reads the way the material file does.
		std::vector<std::pair<std::string, CollapsibleGroupBoxWidget*>> groups;
		std::map<std::string, int> groupCounts;
		const auto groupFor = [&](const std::string& p_name) -> CollapsibleGroupBoxWidget* {
			for (auto& [name, group] : groups)
			{
				if (name == p_name)
				{
					return group;
				}
			}
			auto* group = new CollapsibleGroupBoxWidget(QString::fromStdString(p_name), m_form);
			group->setCollapsed(false);
			groups.push_back({ p_name, group });
			return group;
		};

		int parameterCount = 0;

		// Features first: they say which variants the material compiles, and the parameters that
		// only some variants read are below them.
		if (!featureNames.empty())
		{
			auto* featuresGroup = groupFor("Features");
			for (const auto& [label, feature] : featureNames)
			{
				featuresGroup->addProperty(new BoolProperty(label, m_component.get(), BoolProperty::Style::InviwoRect));
				++groupCounts["Features"];
				++parameterCount;
			}
		}

		for (size_t i = 0; i < parameters.size(); ++i)
		{
			const Rendering::Resources::Shader::Parameter& parameter = parameters[i];
			if (!parameter.isUi)
			{
				continue; // engine-owned (the shadow matrix, the environment map, ...)
			}
			if (parameter.count > 0)
			{
				continue; // arrays (the SSAO kernel) are filled by the renderer, not by hand
			}

			const std::string groupName = parameter.group.empty()
				? (parameter.isSampler ? "Textures" : "Parameters")
				: parameter.group;
			auto* group = groupFor(groupName);
			const QString& qname = labels[i];

			if (parameter.isSampler)
			{
				auto* row = new TextureSlotRow(qname, parameter.name, *m_material, m_textures,
					[this] { MarkDirty(); }, m_form);
				group->addSubWidget(row);
				++groupCounts[groupName];
				++parameterCount;
				continue;
			}

			const std::string& type = parameter.type;
			if (type == "bool")
			{
				group->addProperty(new BoolProperty(qname, m_component.get(), BoolProperty::Style::InviwoRect));
			}
			else if (type == "int")
			{
				auto* property = new SliderIntProperty(qname, m_component.get());
				const int value = m_component->getPropertyValue(qname).toInt();
				const int lo = parameter.hasRange ? static_cast<int>(std::lround(parameter.min)) : std::min(0, value);
				const int hi = parameter.hasRange ? static_cast<int>(std::lround(parameter.max)) : std::max(16, std::abs(value) * 2);
				property->setMinMax(lo, hi);
				property->setIncrement(parameter.hasStep
					? std::max(1, static_cast<int>(std::lround(parameter.step)))
					: 1);
				group->addProperty(property);
			}
			else if (type == "float")
			{
				const float value = m_component->getPropertyValue(qname).toFloat();

				SliderRange range;
				if (parameter.hasRange)
				{
					range.min = parameter.min;
					range.max = parameter.max;
					range.step = parameter.hasStep
						? parameter.step
						: std::max(0.0005f, (parameter.max - parameter.min) / 500.0f);
				}
				else
				{
					range = RangeFor(value, parameter.name);
				}

				auto* property = new SliderFloatProperty(qname, m_component.get(), range.min, range.max);
				group->addProperty(property);
				property->setStep(range.step);
			}
			else if (type == "vec2")
			{
				group->addProperty(new Vec2Property(qname, m_component.get()));
			}
			else if (type == "vec3")
			{
				group->addProperty(new FVec3Property(qname, m_component.get()));
			}
			else if (type == "vec4")
			{
				if (parameter.isColor || IsColorParameter(parameter.name))
				{
					group->addProperty(new ColorPickerProperty(qname, m_component.get()));
				}
				else
				{
					group->addProperty(new FVec4Property(qname, m_component.get()));
				}
			}
			else
			{
				continue; // mat3/mat4/uint/... are not authored from here
			}

			++groupCounts[groupName];
			++parameterCount;
		}

		// The state overrides last: they are instance-level, not part of the surface description.
		{
			auto* stateGroup = groupFor("Renderer State");

			// The blend mode replaces the old "blendable" checkbox: it says both whether the
			// material blends and which factors it uses.
			auto* blendRow = new BlendModeRow(QStringLiteral("Blending"), *m_material,
				[this] { MarkDirty(); }, stateGroup);
			stateGroup->addSubWidget(blendRow);
			++groupCounts["Renderer State"];
			++parameterCount;

			for (const auto& [label, state] : stateNames)
			{
				stateGroup->addProperty(new BoolProperty(label, m_component.get(), BoolProperty::Style::InviwoRect));
				++groupCounts["Renderer State"];
				++parameterCount;
			}
		}

		for (auto& [name, group] : groups)
		{
			if (groupCounts[name] > 0)
			{
				m_formLayout->addWidget(group);
			}
			else
			{
				delete group;
			}
		}

		if (parameterCount == 0)
		{
			makeHint(QStringLiteral("This shader declares no parameters that can be edited here."));
		}

		m_formLayout->addStretch();
	}

	void MaterialDesigner::NewMaterial()
	{
		QDialog dialog(this);
		dialog.setWindowTitle(QStringLiteral("New Material"));
		auto* form = new QFormLayout(&dialog);

		auto* nameEdit = new QLineEdit(&dialog);
		nameEdit->setPlaceholderText(QStringLiteral("MyMaterial"));
		form->addRow(QStringLiteral("Name"), nameEdit);

		auto* shaderPicker = new QComboBox(&dialog);
		shaderPicker->setMinimumContentsLength(30);
		shaderPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);

		std::vector<std::string> shaderKeys;
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_shadersRoot, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".mat")
			{
				continue;
			}
			shaderKeys.push_back(ToShaderKey(
				std::filesystem::relative(entry.path(), m_shadersRoot).generic_string()));
		}
		std::ranges::sort(shaderKeys);
		for (const std::string& key : shaderKeys)
		{
			shaderPicker->addItem(QString::fromStdString(key));
		}
		form->addRow(QStringLiteral("Shader"), shaderPicker);

		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		form->addRow(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

		if (dialog.exec() != QDialog::Accepted)
		{
			return;
		}

		QString name = nameEdit->text().trimmed();
		if (name.isEmpty())
		{
			SetStatus(QStringLiteral("A material needs a name."), true);
			return;
		}
		if (name.contains('/') || name.contains('\\') || name.contains(QStringLiteral("..")))
		{
			SetStatus(QStringLiteral("The name cannot contain a path."), true);
			return;
		}
		if (!name.endsWith(QStringLiteral(".ovmat")))
		{
			name += QStringLiteral(".ovmat");
		}

		const std::string key = ":Materials/" + name.toStdString();
		const std::filesystem::path realPath = std::filesystem::path(m_materialsRoot) / name.toStdString();
		if (std::filesystem::exists(realPath))
		{
			SetStatus(QStringLiteral("That material already exists."), true);
			return;
		}

		const auto shaderKey = shaderPicker->currentText().toStdString();

		Core::Resources::Material* created = nullptr;
		std::string failure;
		{
			ModuleContextScope context;
			if (!context)
			{
				failure = "The render context is not ready yet.";
			}
			else
			{
				auto* shader = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ShaderManager>()
					.GetResource(shaderKey, true);
				if (shader == nullptr)
				{
					failure = "The shader could not be loaded.";
				}
				else
				{
					// Scaffold it, write it once, then load it through the manager: CreateResource
					// is what gives the material its (const) resource path, so the designer never
					// fabricates one.
					Material scaffold;
					scaffold.SetShader(shader);
					Core::Resources::Loaders::MaterialLoader::Save(scaffold, realPath.string());
					created = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
						.GetResource(key, true);
					if (created == nullptr)
					{
						failure = "The new material could not be loaded back.";
					}
				}
			}
		}

		if (created == nullptr)
		{
			SetStatus(QString::fromStdString(failure), true);
			return;
		}

		m_selectedKey = key;
		m_material = created;
		m_dirty = false;
		ResetHistory();
		RefreshMaterialList(key);
		SetStatus(QStringLiteral("Created ") + QString::fromStdString(realPath.filename().string()));
	}

	/** Saves the current (possibly edited) instance under a new name and opens the copy. What is
	 * duplicated is what the designer shows, not what the file on disk happens to hold - that is
	 * the usual "duplicate this and tweak it" workflow. */
	void MaterialDesigner::DuplicateMaterial()
	{
		if (m_material == nullptr || m_selectedKey.empty())
		{
			SetStatus(QStringLiteral("Pick a material first."), true);
			return;
		}

		const QString stem = QString::fromStdString(std::filesystem::path(m_selectedKey).stem().string());
		bool accepted = false;
		QString name = QInputDialog::getText(
			this,
			QStringLiteral("Duplicate Material"),
			QStringLiteral("Name of the copy"),
			QLineEdit::Normal,
			stem + QStringLiteral(" Copy"),
			&accepted).trimmed();

		if (!accepted || name.isEmpty())
		{
			return;
		}
		if (!name.endsWith(QStringLiteral(".ovmat")))
		{
			name += QStringLiteral(".ovmat");
		}
		if (name.contains('/') || name.contains('\\') || name.contains(QStringLiteral("..")))
		{
			SetStatus(QStringLiteral("The name cannot contain a path."), true);
			return;
		}

		const std::filesystem::path realPath = std::filesystem::path(m_materialsRoot) / name.toStdString();
		if (std::filesystem::exists(realPath))
		{
			SetStatus(QStringLiteral("That material already exists."), true);
			return;
		}

		Core::Resources::Loaders::MaterialLoader::Save(*m_material, realPath.string());

		const std::string key = ":Materials/" + name.toStdString();
		Core::Resources::Material* copy = nullptr;
		{
			ModuleContextScope context;
			if (!context)
			{
				SetStatus(QStringLiteral("The render context is not ready yet."), true);
				return;
			}
			copy = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
				.GetResource(key, true);
		}
		if (copy == nullptr)
		{
			SetStatus(QStringLiteral("The copy could not be loaded back."), true);
			return;
		}

		m_selectedKey = key;
		m_material = copy;
		m_dirty = false;
		ResetHistory();
		RefreshMaterialList(key);
		emit materialShown(m_material);
		SetStatus(QStringLiteral("Duplicated to ") + name);
	}

	/** Reads the .ovmat from disk again, discarding whatever the designer changed in memory. The
	 * material instance is reloaded in place, so actors that already use it update too. */
	void MaterialDesigner::RevertMaterial()
	{
		if (m_material == nullptr || m_selectedKey.empty())
		{
			SetStatus(QStringLiteral("Pick a material first."), true);
			return;
		}

		if (m_dirty
			&& QMessageBox::question(this, QStringLiteral("Revert Material"),
				QStringLiteral("Discard the unsaved changes to this material?"),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		{
			return;
		}

		const std::filesystem::path realPath =
			std::filesystem::path(m_materialsRoot) / std::filesystem::path(m_selectedKey).filename();

		{
			ModuleContextScope context;
			if (!context)
			{
				SetStatus(QStringLiteral("The render context is not ready yet."), true);
				return;
			}
			Core::Resources::Loaders::MaterialLoader::Reload(*m_material, realPath.string());
		}

		m_dirty = false;
		ResetHistory();
		RebuildForm();
		emit materialShown(m_material);
		emit materialEdited();
		SetStatus(QStringLiteral("Reverted ") + QString::fromStdString(realPath.filename().string()));
	}

	/** Removes a material asset. Only when nothing in the current scene draws with it: an actor
	 * keeps a raw pointer in its material renderer, and deleting the instance under it would
	 * leave a dangling one. */
	void MaterialDesigner::DeleteMaterial()
	{
		if (m_material == nullptr || m_selectedKey.empty())
		{
			SetStatus(QStringLiteral("Pick a material first."), true);
			return;
		}

		Core::ECS::Actor* user = nullptr;
		if (auto* scene = Core::Global::ServiceLocator::Get<Core::SceneSystem::SceneManager>().GetCurrentScene())
		{
			std::function<Core::ECS::Actor*(Core::ECS::Actor&)> findUser = [&](Core::ECS::Actor& p_actor)
				-> Core::ECS::Actor* {
				if (auto* renderer = p_actor.GetComponent<Core::ECS::Components::CMaterialRenderer>())
				{
					for (Core::Resources::Material* material : renderer->GetMaterials())
					{
						if (material == m_material)
						{
							return &p_actor;
						}
					}
				}
				for (Core::ECS::Actor* child : p_actor.GetChildren())
				{
					if (Core::ECS::Actor* found = findUser(*child))
					{
						return found;
					}
				}
				return nullptr;
			};

			for (Core::ECS::Actor* actor : scene->GetActors())
			{
				if (Core::ECS::Actor* found = findUser(*actor))
				{
					user = found;
					break;
				}
			}
		}

		if (user != nullptr)
		{
			SetStatus(QStringLiteral("In use by '%1' - assign another material first.")
				.arg(QString::fromStdString(user->GetName())), true);
			return;
		}

		const std::filesystem::path realPath =
			std::filesystem::path(m_materialsRoot) / std::filesystem::path(m_selectedKey).filename();
		const QString name = QString::fromStdString(realPath.filename().string());
		if (QMessageBox::question(this, QStringLiteral("Delete Material"),
			QStringLiteral("Delete %1 from disk?").arg(name),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		{
			return;
		}

		// The preview holds a pointer as well: hand it the built-in material before this one dies.
		emit materialShown(nullptr);

		Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
			.UnloadResource(m_selectedKey);
		std::error_code error;
		std::filesystem::remove(realPath, error);

		m_thumbnails.erase(m_selectedKey);
		m_material = nullptr;
		m_selectedKey.clear();
		m_dirty = false;
		ResetHistory();
		RefreshMaterialList();
		SetStatus(QStringLiteral("Deleted ") + name);
	}

	void MaterialDesigner::SaveMaterial()
	{
		if (m_material == nullptr || m_selectedKey.empty())
		{
			SetStatus(QStringLiteral("Pick a material first."), true);
			return;
		}

		const std::filesystem::path realPath =
			std::filesystem::path(m_materialsRoot) / std::filesystem::path(m_selectedKey).filename();
		Core::Resources::Loaders::MaterialLoader::Save(*m_material, realPath.string());
		m_dirty = false;
		SetStatus(QStringLiteral("Saved ") + QString::fromStdString(realPath.generic_string()));
	}

	void MaterialDesigner::AssignToSelection()
	{
		if (m_material == nullptr)
		{
			SetStatus(QStringLiteral("Pick a material first."), true);
			return;
		}

		auto& sceneView = Core::Global::ServiceLocator::Get<Editor::Panels::SceneView>();
		auto* actor = sceneView.GetSelectedActor();
		if (actor == nullptr)
		{
			SetStatus(QStringLiteral("Select an actor first."), true);
			return;
		}

		auto* renderer = actor->GetComponent<Core::ECS::Components::CMaterialRenderer>();
		if (renderer == nullptr)
		{
			// A topology shape keeps its face material on the AllFaces anchor (its edges and
			// vertices keep theirs, so they are deliberately left alone).
			if (auto* faces = actor->GetChild("AllFaces"); faces != nullptr)
			{
				renderer = faces->GetComponent<Core::ECS::Components::CMaterialRenderer>();
			}
		}
		if (renderer == nullptr)
		{
			SetStatus(QStringLiteral("The selected actor has no material renderer."), true);
			return;
		}

		renderer->FillWithMaterial(*m_material);
		SetStatus(QStringLiteral("Assigned to ") + QString::fromStdString(actor->GetName()));
		emit materialEdited();
	}

	MaterialDesigner::Snapshot MaterialDesigner::Capture() const
	{
		Snapshot snapshot;
		if (m_material != nullptr)
		{
			snapshot.properties = m_material->GetProperties();
			snapshot.features = m_material->GetFeatures();
			snapshot.blendable = m_material->IsBlendable();
			snapshot.blendMode = m_material->GetBlendMode();
			snapshot.transparent = m_material->IsTransparent();
			snapshot.backfaceCulling = m_material->HasBackfaceCulling();
			snapshot.frontfaceCulling = m_material->HasFrontfaceCulling();
			snapshot.depthTest = m_material->HasDepthTest();
			snapshot.depthWriting = m_material->HasDepthWriting();
			snapshot.castShadows = m_material->IsShadowCaster();
			snapshot.receiveShadows = m_material->IsShadowReceiver();
		}
		return snapshot;
	}

	void MaterialDesigner::ApplySnapshot(const Snapshot& p_snapshot)
	{
		if (m_material == nullptr)
		{
			return;
		}

		// SetProperty per entry so the property signature versions move and the renderer uploads
		// the restored values on the next frame.
		for (const auto& [name, property] : p_snapshot.properties)
		{
			m_material->SetProperty(name, property.value, property.singleUse);
		}
		m_material->SetFeatures(p_snapshot.features);
		m_material->SetBlendable(p_snapshot.blendable);
		m_material->SetBlendMode(p_snapshot.blendMode);
		m_material->SetTransparent(p_snapshot.transparent);
		m_material->SetBackfaceCulling(p_snapshot.backfaceCulling);
		m_material->SetFrontfaceCulling(p_snapshot.frontfaceCulling);
		m_material->SetDepthTest(p_snapshot.depthTest);
		m_material->SetDepthWriting(p_snapshot.depthWriting);
		m_material->SetCastShadows(p_snapshot.castShadows);
		m_material->SetReceiveShadows(p_snapshot.receiveShadows);
	}

	void MaterialDesigner::ResetHistory()
	{
		m_undo.clear();
		m_redo.clear();
		m_baseline = Capture();
		m_hasBaseline = (m_material != nullptr);
		m_lastEditMs = 0;
	}

	void MaterialDesigner::Undo()
	{
		if (m_material == nullptr || m_undo.empty())
		{
			SetStatus(QStringLiteral("Nothing to undo."));
			return;
		}

		m_redo.push_back(Capture());
		ApplySnapshot(m_undo.back());
		m_undo.pop_back();

		m_baseline = Capture();
		m_hasBaseline = true;
		m_lastEditMs = 0;
		m_dirty = true;
		RebuildForm();
		emit materialEdited();
		SetStatus(QStringLiteral("Undo"));
	}

	void MaterialDesigner::Redo()
	{
		if (m_material == nullptr || m_redo.empty())
		{
			SetStatus(QStringLiteral("Nothing to redo."));
			return;
		}

		m_undo.push_back(Capture());
		ApplySnapshot(m_redo.back());
		m_redo.pop_back();

		m_baseline = Capture();
		m_hasBaseline = true;
		m_lastEditMs = 0;
		m_dirty = true;
		RebuildForm();
		emit materialEdited();
		SetStatus(QStringLiteral("Redo"));
	}

	void MaterialDesigner::MarkDirty()
	{
		if (m_material != nullptr && m_hasBaseline)
		{
			const qint64 now = QDateTime::currentMSecsSinceEpoch();

			// A slider drag is one undo step, not one per pixel: the state before the first edit
			// of a burst is what gets pushed; the baseline then tracks the burst's latest state.
			if (m_lastEditMs == 0 || now - m_lastEditMs > 600)
			{
				m_undo.push_back(m_baseline);
				if (m_undo.size() > 128)
				{
					m_undo.erase(m_undo.begin());
				}
				m_redo.clear();
			}

			m_lastEditMs = now;
			m_baseline = Capture();
		}

		m_dirty = true;
		SetStatus(QStringLiteral("Modified - press Save to write it to disk."));
		emit materialEdited();
	}

	void MaterialDesigner::SetStatus(const QString& p_text, bool p_error)
	{
		m_status->setText(p_text);
		m_status->setToolTip(p_text);
		m_status->setStyleSheet(p_error ? QStringLiteral("color:#e06c75;") : QStringLiteral("color:#7d8288;"));
		emit statusMessage(p_text, p_error);
	}
}
