#include "editor/Language.h"
#include "editor/ViewCubeLabels.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QTabBar>
#include <QWidget>
#include <QHash>

#include <cstdlib>

namespace
{
	const char* const kSettingKey = "ui/language";

	MOON::Language::ELanguage gCurrent = MOON::Language::ELanguage::Chinese;

	/** The strings that had no English twin in the code: group captions, menu titles and the two
	 * ribbon page tabs. Everything else is carried by the actions themselves. */
	const QHash<QString, QString>& Dictionary()
	{
		static const QHash<QString, QString> dictionary = {
			// ribbon captions
			{ QStringLiteral("绘制"), QStringLiteral("Draw") },
			{ QStringLiteral("编辑"), QStringLiteral("Edit") },
			{ QStringLiteral("参考"), QStringLiteral("Reference") },
			{ QStringLiteral("几何约束"), QStringLiteral("Geometric") },
			{ QStringLiteral("尺寸约束"), QStringLiteral("Dimensions") },
			{ QStringLiteral("基本体"), QStringLiteral("Primitives") },
			{ QStringLiteral("基于轮廓"), QStringLiteral("Profile") },
			{ QStringLiteral("扫掠"), QStringLiteral("Sweep") },
			{ QStringLiteral("扫描"), QStringLiteral("Sweep") },
			{ QStringLiteral("修饰"), QStringLiteral("Dress-Up") },
			{ QStringLiteral("基准"), QStringLiteral("Datum") },
			{ QStringLiteral("变换"), QStringLiteral("Transform") },
			// ribbon pages
			{ QStringLiteral("草图"), QStringLiteral("Sketch") },
			{ QStringLiteral("模型"), QStringLiteral("Model") },
			// menus
			{ QStringLiteral("文件"), QStringLiteral("File") },
			{ QStringLiteral("显示"), QStringLiteral("Display") },
			{ QStringLiteral("视图"), QStringLiteral("View") },
			{ QStringLiteral("主题"), QStringLiteral("Theme") },
			{ QStringLiteral("深色"), QStringLiteral("Dark") },
			{ QStringLiteral("浅色"), QStringLiteral("Light") },
			{ QStringLiteral("语言"), QStringLiteral("Language") },
			{ QStringLiteral("中文"), QStringLiteral("Chinese") },
			{ QStringLiteral("英文"), QStringLiteral("English") },
		};
		return dictionary;
	}
}

void MOON::Language::Initialize()
{
	if (const char* p_env = std::getenv("MOON_LANG"); p_env != nullptr && *p_env != '\0')
	{
		gCurrent = QByteArray(p_env).toLower().startsWith("en") ? ELanguage::English : ELanguage::Chinese;
		return;
	}
	QSettings settings;
	const QString stored = settings.value(kSettingKey, "zh").toString();
	gCurrent = stored.startsWith("en") ? ELanguage::English : ELanguage::Chinese;
}

MOON::Language::ELanguage MOON::Language::Current()
{
	return gCurrent;
}

void MOON::Language::Set(ELanguage p_language, bool p_persist)
{
	// The view cube face labels are drawn from the language; the renderer rebuilds them on the
	// next frame (see UpdateViewCubeLabels).
	ViewCubeLabels::Invalidate();
	gCurrent = p_language;
	if (p_persist)
	{
		QSettings settings;
		settings.setValue(kSettingKey, p_language == ELanguage::English ? "en" : "zh");
	}
}

QString MOON::Language::English(const QString& p_chinese)
{
	const auto it = Dictionary().constFind(p_chinese);
	return it == Dictionary().constEnd() ? p_chinese : it.value();
}

	/** English parameter names used by the feature panels, in Chinese. The toolbar's wording wins
	 * when a term appears in both places (Pad is 凸台, Offset is 偏移). */
	const QHash<QString, QString>& Parameters()
	{
		static const QHash<QString, QString> dictionary = {
			{ QStringLiteral("Angle"), QStringLiteral("角度") },
			{ QStringLiteral("Angle 1"), QStringLiteral("角度 1") },
			{ QStringLiteral("Angle 2"), QStringLiteral("角度 2") },
			{ QStringLiteral("Axis"), QStringLiteral("轴") },
			{ QStringLiteral("Axis Line Width"), QStringLiteral("轴线宽度") },
			{ QStringLiteral("Axis X Color"), QStringLiteral("X 轴颜色") },
			{ QStringLiteral("Axis Y Color"), QStringLiteral("Y 轴颜色") },
			{ QStringLiteral("Binormal"), QStringLiteral("副法线") },
			{ QStringLiteral("Closed"), QStringLiteral("闭合") },
			{ QStringLiteral("Constraint Color"), QStringLiteral("约束颜色") },
			{ QStringLiteral("Construction Color"), QStringLiteral("构造几何颜色") },
			{ QStringLiteral("Curve Color"), QStringLiteral("曲线颜色") },
			{ QStringLiteral("Curve Line Width"), QStringLiteral("曲线宽度") },
			{ QStringLiteral("Direction"), QStringLiteral("方向") },
			{ QStringLiteral("External Color"), QStringLiteral("外部几何颜色") },
			{ QStringLiteral("Extrude Direction"), QStringLiteral("拉伸方向") },
			{ QStringLiteral("Extrude Type"), QStringLiteral("拉伸类型") },
			{ QStringLiteral("Flip Direction"), QStringLiteral("反向") },
			{ QStringLiteral("Grid Line Width"), QStringLiteral("网格线宽") },
			{ QStringLiteral("Grid Major Color"), QStringLiteral("网格主线颜色") },
			{ QStringLiteral("Grid Minor Color"), QStringLiteral("网格次线颜色") },
			{ QStringLiteral("Grid Target Px"), QStringLiteral("网格目标像素") },
			{ QStringLiteral("Growth"), QStringLiteral("增长方式") },
			{ QStringLiteral("Height"), QStringLiteral("高度") },
			{ QStringLiteral("Intersection"), QStringLiteral("相交") },
			{ QStringLiteral("Join Type"), QStringLiteral("连接方式") },
			{ QStringLiteral("LeftHanded"), QStringLiteral("左旋") },
			{ QStringLiteral("Length"), QStringLiteral("长度") },
			{ QStringLiteral("Length 1"), QStringLiteral("长度 1") },
			{ QStringLiteral("Length 2"), QStringLiteral("长度 2") },
			{ QStringLiteral("Map Mode"), QStringLiteral("映射模式") },
			{ QStringLiteral("Mirror Plane"), QStringLiteral("镜像面") },
			{ QStringLiteral("Mirror Type"), QStringLiteral("镜像类型") },
			{ QStringLiteral("Mode"), QStringLiteral("模式") },
			{ QStringLiteral("Occurrences"), QStringLiteral("数量") },
			{ QStringLiteral("Offset"), QStringLiteral("偏移") },
			{ QStringLiteral("Origin"), QStringLiteral("原点") },
			{ QStringLiteral("Pattern Type"), QStringLiteral("阵列类型") },
			{ QStringLiteral("Pitch"), QStringLiteral("螺距") },
			{ QStringLiteral("Point Color"), QStringLiteral("点颜色") },
			{ QStringLiteral("Point Size"), QStringLiteral("点大小") },
			{ QStringLiteral("Polar Type"), QStringLiteral("环形类型") },
			{ QStringLiteral("Preselect Color"), QStringLiteral("预选颜色") },
			{ QStringLiteral("Radius"), QStringLiteral("半径") },
			{ QStringLiteral("Reverse"), QStringLiteral("反向") },
			{ QStringLiteral("Reversed"), QStringLiteral("反向") },
			{ QStringLiteral("Rotation"), QStringLiteral("旋转角") },
			{ QStringLiteral("Ruled"), QStringLiteral("直纹") },
			{ QStringLiteral("Select Color"), QStringLiteral("选中颜色") },
			{ QStringLiteral("Size"), QStringLiteral("尺寸") },
			{ QStringLiteral("Size2"), QStringLiteral("尺寸 2") },
			{ QStringLiteral("Thickness"), QStringLiteral("厚度") },
			{ QStringLiteral("Thickness value"), QStringLiteral("厚度值") },
			{ QStringLiteral("Transition"), QStringLiteral("过渡方式") },
			{ QStringLiteral("Turns"), QStringLiteral("圈数") },
			{ QStringLiteral("Type"), QStringLiteral("类型") },
			{ QStringLiteral("Use ALL Edges"), QStringLiteral("使用全部边") },
			{ QStringLiteral("Width"), QStringLiteral("宽度") },
			// property/task group titles
			{ QStringLiteral("PathTrace Mat"), QStringLiteral("路径追踪材质") },
			{ QStringLiteral("Debug Setting"), QStringLiteral("调试设置") },
			{ QStringLiteral("Render Pass Setting"), QStringLiteral("渲染通道设置") },
			{ QStringLiteral("mat"), QStringLiteral("材质") },
			{ QStringLiteral("Extrude"), QStringLiteral("拉伸") },
			{ QStringLiteral("Pad"), QStringLiteral("凸台") },
			{ QStringLiteral("Pocket"), QStringLiteral("凹槽") },
			{ QStringLiteral("Revolution"), QStringLiteral("旋转") },
			{ QStringLiteral("Groove"), QStringLiteral("开槽") },
			{ QStringLiteral("Fillet"), QStringLiteral("圆角") },
			{ QStringLiteral("Chamfer"), QStringLiteral("倒角") },
			{ QStringLiteral("Thickness"), QStringLiteral("抽壳") },
			{ QStringLiteral("Helix"), QStringLiteral("螺旋") },
			{ QStringLiteral("Pipe"), QStringLiteral("管道") },
			{ QStringLiteral("Loft"), QStringLiteral("放样") },
			{ QStringLiteral("Datum Line"), QStringLiteral("基准线") },
			{ QStringLiteral("Datum Plane"), QStringLiteral("基准面") },
			{ QStringLiteral("Polar Pattern"), QStringLiteral("环形阵列") },
			{ QStringLiteral("Linear Pattern"), QStringLiteral("线性阵列") },
			{ QStringLiteral("Mirror"), QStringLiteral("镜像") },
			{ QStringLiteral("Sketch"), QStringLiteral("草图") },
			{ QStringLiteral("Profile"), QStringLiteral("轮廓") },
			{ QStringLiteral("Constraints"), QStringLiteral("约束") },
			{ QStringLiteral("Dimensions"), QStringLiteral("尺寸") },
			// enum values in the feature panels
			{ QStringLiteral("Through All"), QStringLiteral("贯通") },
			{ QStringLiteral("UpToFace"), QStringLiteral("到面") },
			{ QStringLiteral("Forward"), QStringLiteral("正向") },
			{ QStringLiteral("Double"), QStringLiteral("双向") },
			{ QStringLiteral("Sysmetric"), QStringLiteral("对称") },
			{ QStringLiteral("Symmetric"), QStringLiteral("对称") },
			{ QStringLiteral("Two Sides"), QStringLiteral("两侧") },
			{ QStringLiteral("Normal"), QStringLiteral("法向") },
			{ QStringLiteral("Tangent"), QStringLiteral("相切") },
			{ QStringLiteral("One Side"), QStringLiteral("单侧") },
		};
		return dictionary;
	}
QString MOON::Language::Pick(const QString& p_zh, const QString& p_en)
{
	return gCurrent == ELanguage::English ? p_en : p_zh;
}

QString MOON::Language::Localize(const QString& p_english)
{
	if (gCurrent != ELanguage::Chinese)
	{
		return p_english;
	}
	const auto it = Parameters().constFind(p_english);
	return it == Parameters().constEnd() ? p_english : it.value();
}

void MOON::Language::SetLabel(QAction* p_action, const QString& p_zh, const QString& p_en)
{
	if (p_action == nullptr)
	{
		return;
	}
	p_action->setProperty("moonZh", p_zh);
	p_action->setProperty("moonEn", p_en);
	p_action->setText(Pick(p_zh, p_en));
	// Both spellings stay in the tooltip: the icon is what one looks at, the tooltip is where the
	// other language is still available without switching.
	p_action->setToolTip(QStringLiteral("%1 (%2)").arg(p_zh, p_en));
}

void MOON::Language::SetText(QWidget* p_widget, const QString& p_zh, const QString& p_en)
{
	p_widget->setProperty("moonZh", p_zh);
	p_widget->setProperty("moonEn", p_en);
	if (auto* button = qobject_cast<QAbstractButton*>(p_widget))
	{
		button->setText(Pick(p_zh, p_en));
	}
	else if (auto* label = qobject_cast<QLabel*>(p_widget))
	{
		label->setText(Pick(p_zh, p_en));
	}
}

void MOON::Language::SetWindowTitle(QWidget* p_widget, const QString& p_zh, const QString& p_en)
{
	if (p_widget == nullptr)
	{
		return;
	}
	p_widget->setProperty("moonTitleZh", p_zh);
	p_widget->setProperty("moonTitleEn", p_en);
	p_widget->setWindowTitle(Pick(p_zh, p_en));
}

void MOON::Language::Apply(QApplication* p_app)
{
	if (p_app == nullptr)
	{
		return;
	}
	for (QWidget* widget : QApplication::allWidgets())
	{
		for (QAction* action : widget->findChildren<QAction*>())
		{
			const QVariant zh = action->property("moonZh");
			const QVariant en = action->property("moonEn");
			if (zh.isValid())
			{
				const QString english = en.isValid() ? en.toString() : English(zh.toString());
				action->setText(Pick(zh.toString(), english));
				action->setToolTip(QStringLiteral("%1 (%2)").arg(zh.toString(), english));
			}
		}
		const QVariant titleZh = widget->property("moonTitleZh");
		if (titleZh.isValid())
		{
			widget->setWindowTitle(Pick(titleZh.toString(), widget->property("moonTitleEn").toString()));
		}
		if (auto* button = qobject_cast<QAbstractButton*>(widget))
		{
			const QVariant zh = button->property("moonZh");
			const QVariant en = button->property("moonEn");
			if (zh.isValid() && en.isValid())
			{
				button->setText(Pick(zh.toString(), en.toString()));
			}
		}
		if (auto* label = qobject_cast<QLabel*>(widget))
		{
			const QVariant zh = label->property("moonZh");
			if (zh.isValid())
			{
				const QVariant en = label->property("moonEn");
				const QString english = en.isValid() ? en.toString() : English(zh.toString());
				label->setText(Pick(zh.toString(), english));
			}
		}
		if (auto* combo = qobject_cast<QComboBox*>(widget))
		{
			const QStringList pairs = combo->property("moonItems").toStringList();
			for (int index = 0; index < pairs.size() && index < combo->count(); ++index)
			{
				const QStringList pair = pairs.at(index).split('|');
				if (pair.size() == 2)
				{
					combo->setItemText(index, Pick(pair.at(0), pair.at(1)));
				}
			}
		}
		if (auto* tabs = qobject_cast<QTabBar*>(widget))
		{
			// Each tab remembers its two spellings as "zh|en".
			const QVariantList pairs = tabs->property("moonTabs").toList();
			for (int index = 0; index < pairs.size() && index < tabs->count(); ++index)
			{
				const QStringList pair = pairs.at(index).toString().split('|');
				if (pair.size() == 2)
				{
					tabs->setTabText(index, Pick(pair.at(0), pair.at(1)));
				}
			}
		}
	}
}
