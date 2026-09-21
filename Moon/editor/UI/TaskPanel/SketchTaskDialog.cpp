#include "SketchTaskDialog.h"
#include "TaskBox.h"
#include "editor/UI/PropertyPanel/Collapsiblegroupboxwidget.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "feature/SketcherFeature.h"
#include "Widgets/BoolProperty.h"
#include "Widgets/ColorPickerProperty.h"
#include "Widgets/SliderFloatProperty.h"
#include "core/ViewTool.h"
#include "Interactive/Widgets/SketchPlane.h"
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <gp_Pln.hxx>
#include <QListWidget>
#include <QStringList>
#include <QTimer>
#include <QIcon>
#include <QToolButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QColor>
#include <Eigen/Core>
#include <cstdio>
#include <vector>
#include <functional>
#include <set>
namespace MOON {
	static QColor abgrToQColor(const Eigen::Vector4<uint8_t>& c)
	{
		return QColor(c[3], c[2], c[1], c[0]);
	}
	static Eigen::Vector4<uint8_t> qColorToAbgr(const QColor& c)
	{
		return Eigen::Vector4<uint8_t>(
			static_cast<uint8_t>(c.alpha()),
			static_cast<uint8_t>(c.blue()),
			static_cast<uint8_t>(c.green()),
			static_cast<uint8_t>(c.red())
		);
	}
	static QString constraintIconPath(Sketcher::ConstraintType type)
	{
		const QString prefix = ":/widgets/icons/constraint/";
		switch (type) {
		case Sketcher::ConstraintType::Coincident:
			return prefix + "Constraint_Coincident.svg";
		case Sketcher::ConstraintType::Horizontal:
			return prefix + "Constraint_Horizontal.svg";
		case Sketcher::ConstraintType::Vertical:
			return prefix + "Constraint_Vertical.svg";
		case Sketcher::ConstraintType::Parallel:
			return prefix + "Constraint_Parallel.svg";
		case Sketcher::ConstraintType::Tangent:
			return prefix + "Constraint_Tangent.svg";
		case Sketcher::ConstraintType::Distance:
			return prefix + "Constraint_Length.svg";
		case Sketcher::ConstraintType::DistanceX:
			return prefix + "Constraint_DistanceX.svg";
		case Sketcher::ConstraintType::DistanceY:
			return prefix + "Constraint_VerticalDistance.svg";
		case Sketcher::ConstraintType::Angle:
			return prefix + "Constraint_InternalAngle.svg";
		case Sketcher::ConstraintType::Perpendicular:
			return prefix + "Constraint_Perpendicular.svg";
		case Sketcher::ConstraintType::Radius:
			return prefix + "Constraint_Radius.svg";
		case Sketcher::ConstraintType::Equal:
			return prefix + "Constraint_EqualLength.svg";
		case Sketcher::ConstraintType::PointOnObject:
			return prefix + "Constraint_PointOnObject.svg";
		case Sketcher::ConstraintType::Symmetric:
			return prefix + "Constraint_Symmetric.svg";
		case Sketcher::ConstraintType::Diameter:
			return prefix + "Constraint_Diameter.svg";
		case Sketcher::ConstraintType::Block:
			return prefix + "Constraint_Block.svg";
		default:
			return QString();
		}
	}
	static QString curveIconPath(const Part::Geometry* g)
	{
		if (!g) {
			return QString();
		}
		const QString prefix = ":/widgets/icons/";
		if (g->is<Part::GeomLineSegment>()) {
			return prefix + "Sketcher_CreateLine.svg";
		}
		if (g->is<Part::GeomArcOfCircle>()) {
			return prefix + "Sketcher_CreateArc.svg";
		}
		if (g->is<Part::GeomCircle>()) {
			return prefix + "Sketcher_CreateCircle.svg";
		}
		if (g->is<Part::GeomEllipse>() || g->is<Part::GeomArcOfEllipse>()) {
			return prefix + "Sketcher_CreateEllipseByCenter.svg";
		}
		if (g->is<Part::GeomBSplineCurve>()) {
			return prefix + "Sketcher_CreateBSpline.svg";
		}
		if (g->is<Part::GeomPoint>()) {
			return prefix + "Sketcher_CreatePoint.svg";
		}
		return QString();
	}

    class SketchTaskDialog::Internal
    {
    public:
        Internal(SketchTaskDialog* s) :self(s) {
            auto f = self->getFeature();
            if (f) {
                //
				feature = dynamic_cast<SketcherFeature*>(f);
                feature->getSketcherObj()->beginEdit();

            }
            else
            {
                feature = SketcherObjManager::instance().CreateSketcherFeature();
                //feature->getSketcherObj()->setActive(true);
				self->setFeature(feature);
                Feature* baseFeature = nullptr;
                std::vector<std::string>subValues;
                ViewTool::getSelectedBasedFeature(baseFeature, subValues);
                if (baseFeature) {
                    feature->setBaseFeature(baseFeature);
                    feature->setSubValues(subValues);
                    Part::TopoShape face = feature->getBaseTopoFaceShape();
                    gp_Pln pln;
                    face.findPlane(pln);
                    SketcherPlane2D plane;
                    plane.normal = Base::Vector3d{ pln.Axis().Direction().X(),pln.Axis().Direction().Y(),pln.Axis().Direction().Z() };
                    plane.origin = Base::Vector3d{ pln.Location().X(),pln.Location().Y(),pln.Location().Z() };
                    plane.xAxis = Base::Vector3d{ pln.XAxis().Direction().X(),pln.XAxis().Direction().Y(),pln.XAxis().Direction().Z() };
                    plane.yAxis = Base::Vector3d{ pln.YAxis().Direction().X(),pln.YAxis().Direction().Y(),pln.YAxis().Direction().Z() };
                    feature->getSketcherObj()->setPlane(plane);
                }
                else
                {
					behaviour = new SketchPlane("selectPlane");
                    behaviour->AddObserver(SketchPlaneEvent::SelectPlane,self, &SketchTaskDialog::onSelectPlane);
                }
            }
            SketcherObjManager::instance().setCurrentActiveSketcherFeature(feature);
        }
        ~Internal() {
            feature->getSketcherObj()->setActive(false);
            if (behaviour) {
                delete behaviour;
            }
        }
    private:
        friend SketchTaskDialog;
        SketchTaskDialog* self = nullptr;
        SketcherFeature* feature = nullptr;
        SketchPlane* behaviour = nullptr;
    };

    SketchTaskDialog::SketchTaskDialog(QWidget* parent, Feature* feature)
        : ParamTaskDialog(parent),mInternal(new Internal(this)),ShapeHelper(feature)
    {
        PropertyComponent* sketchGroup = addGroupParam("Sketch");
        addParam(
            new BoolProperty(
                "Draw Grid",
                sketchGroup,
                BoolProperty::Style::InviwoRect
            )
        );
        addParam(
            new BoolProperty(
                "Snap To Grid",
                sketchGroup,
                BoolProperty::Style::InviwoRect
            )
        );
        addParam(new ColorPickerProperty("Point Color", sketchGroup));
        addParam(new ColorPickerProperty("Preselect Color", sketchGroup));
        addParam(new ColorPickerProperty("Select Color", sketchGroup));
        addParam(new ColorPickerProperty("Curve Color", sketchGroup));
        addParam(new ColorPickerProperty("Construction Color", sketchGroup));
        addParam(new ColorPickerProperty("External Color", sketchGroup));
        addParam(new ColorPickerProperty("Constraint Color", sketchGroup));
        auto* curveWidth = new SliderFloatProperty("Curve Line Width", sketchGroup, 0.5f, 10.0f);
        curveWidth->setStep(0.1f);
        addParam(curveWidth);
        auto* pointSize = new SliderFloatProperty("Point Size", sketchGroup, 2.0f, 40.0f);
        pointSize->setStep(0.5f);
        addParam(pointSize);
        addGroupParam("Constraints");
        addGroupParam("Curves");
        addGroupParam("External Geometry");
        buildUi();

        mConstraintList = new QListWidget(this);
        mCurveList = new QListWidget(this);
        mExternalList = new QListWidget(this);
        mSolverStatus = new QLabel(this);
        mSolverStatus->setWordWrap(true);
        mSolverStatus->setTextFormat(Qt::RichText);
        mConstraintList->setMinimumHeight(110);
        mCurveList->setMinimumHeight(110);
        mExternalList->setMinimumHeight(80);
        auto attachList = [this](const QString& groupName, QListWidget* list) {
            const auto it = groupToIndex.find(groupName);
            if (it != groupToIndex.end()) {
                auto* group = m_comps[it->second].first;
                group->setCollapsed(false);
                group->addSubWidget(list);
            }
        };
        // The solver's verdict is attached before the constraint list, so it reads as
        // the heading of that list: the degree of freedom count and what is wrong with
        // the constraints below it.
        {
            const auto it = groupToIndex.find("Constraints");
            if (it != groupToIndex.end()) {
                auto* group = m_comps[it->second].first;
                group->addSubWidget(mSolverStatus);
            }
        }
        attachList("Constraints", mConstraintList);
        attachList("Curves", mCurveList);
        attachList("External Geometry", mExternalList);

        mRefreshTimer = new QTimer(this);
        mRefreshTimer->setInterval(300);
        connect(mRefreshTimer, &QTimer::timeout, this, [this]() { refreshLists(); });
        mRefreshTimer->start();
        mCurveList->setMouseTracking(true);
        mCurveList->viewport()->setMouseTracking(true);
        mCurveList->setStyleSheet(
            "QListWidget { background: transparent; border: none; outline: 0; }"
            "QListWidget::item { height: 20px; padding-left: 2px; }"
            "QListWidget::item:hover { background-color: #cfe2f5; color: #202020; }"
            "QListWidget::item:selected { background-color: #7ab2e8; color: white; }"
        );
        mExternalList->setStyleSheet(
            "QListWidget { background: transparent; border: none; outline: 0; }"
            "QListWidget::item { height: 20px; padding-left: 2px; }"
        );
        connect(mCurveList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            if (mInternal && mInternal->feature) {
                const int geoId = item->data(Qt::UserRole).toInt();
                mInternal->feature->getSketcherObj()->selectGeo(geoId);
            }
        });
        connect(mCurveList, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
            if (mInternal && mInternal->feature) {
                const int geoId = item->data(Qt::UserRole).toInt();
                mInternal->feature->getSketcherObj()->setPreselect(geoId);
            }
        });
        refreshLists();
    }

    SketchTaskDialog::~SketchTaskDialog()
    {
        delete mInternal;
    }

    QVariant SketchTaskDialog::getParamValue(const QString& propertyName)
    {
        if (propertyName == "Sketch:Draw Grid" && mInternal->feature) {
            return QVariant::fromValue(mInternal->feature->getSketcherObj()->isDrawGrid());
        }
        if (propertyName == "Sketch:Snap To Grid" && mInternal->feature) {
            return QVariant::fromValue(mInternal->feature->getSketcherObj()->isSnapToGrid());
        }
        if (mInternal && mInternal->feature) {
            const auto& opt = mInternal->feature->getSketcherObj()->drawOption();
            if (propertyName == "Sketch:Point Color") {
                return QVariant::fromValue(abgrToQColor(opt.pointColor));
            }
            if (propertyName == "Sketch:Preselect Color") {
                return QVariant::fromValue(abgrToQColor(opt.preselectColor));
            }
            if (propertyName == "Sketch:Select Color") {
                return QVariant::fromValue(abgrToQColor(opt.selectColor));
            }
            if (propertyName == "Sketch:Curve Color") {
                return QVariant::fromValue(abgrToQColor(opt.curveColor));
            }
            if (propertyName == "Sketch:Construction Color") {
                return QVariant::fromValue(abgrToQColor(opt.constructionColor));
            }
            if (propertyName == "Sketch:External Color") {
                return QVariant::fromValue(abgrToQColor(opt.externalColor));
            }
            if (propertyName == "Sketch:Constraint Color") {
                return QVariant::fromValue(abgrToQColor(opt.constraintColor));
            }
            if (propertyName == "Sketch:Curve Line Width") {
                return QVariant::fromValue(opt.curveLineWidth);
            }
            if (propertyName == "Sketch:Point Size") {
                return QVariant::fromValue(opt.pointSize);
            }
        }
        return QVariant();
    }

    void SketchTaskDialog::setParamValue(const QString& propertyName, const QVariant& value)
    {
        if (propertyName == "Sketch:Draw Grid" && mInternal->feature) {
            mInternal->feature->getSketcherObj()->setDrawGrid(value.value<bool>());
        }
        if (propertyName == "Sketch:Snap To Grid" && mInternal->feature) {
            mInternal->feature->getSketcherObj()->setSnapToGrid(value.value<bool>());
        }
        if (mInternal && mInternal->feature) {
            auto& opt = mInternal->feature->getSketcherObj()->drawOption();
            if (propertyName == "Sketch:Point Color") {
                opt.pointColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Preselect Color") {
                opt.preselectColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Select Color") {
                opt.selectColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Curve Color") {
                opt.curveColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Construction Color") {
                opt.constructionColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:External Color") {
                opt.externalColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Constraint Color") {
                opt.constraintColor = qColorToAbgr(value.value<QColor>());
            }
            else if (propertyName == "Sketch:Curve Line Width") {
                opt.curveLineWidth = value.toFloat();
            }
            else if (propertyName == "Sketch:Point Size") {
                opt.pointSize = value.toFloat();
            }
        }
    }

    
    void SketchTaskDialog::clickOk()
    {
        mInternal->feature->getSketcherObj()->makeDone();
        mInternal->feature->execute();
        mInternal->feature->makeDone();
    }
    void SketchTaskDialog::clickApply()
    {
    }
    void SketchTaskDialog::clickCancel()
    {
    }
    void SketchTaskDialog::onSelectPlane()
    {
        if (mInternal->behaviour) {
            mInternal->feature->getSketcherObj()->setPlane(mInternal->behaviour->getSelectPlane());
        }
    }
    void SketchTaskDialog::refreshLists()
    {
        if (!mInternal || !mInternal->feature || !mConstraintList || !mCurveList
            || !mExternalList) {
            return;
        }
        SketcherObj* obj = mInternal->feature->getSketcherObj();
        if (!obj) {
            return;
        }
        updateSolverStatus(obj);

     	QStringList constraintLines;
        QStringList constraintIconPaths;
        std::vector<bool> constraintVisible;
        std::vector<SketcherObj::ConstraintStatus> constraintStatus;
        for (int i = 0; i < obj->getConstraintCount(); ++i) {
            const Sketcher::Constraint* c = obj->getConstraint(i);
            if (!c) {
                continue;
            }
            constraintIconPaths << constraintIconPath(c->Type);
            constraintVisible.push_back(c->isVisible);
            // The solver's diagnosis of this row, so a sketch that cannot be solved
            // says which constraint is to blame instead of only going red in the
            // viewport.
            constraintStatus.push_back(obj->getConstraintStatus(i));
            QString line = QString("%1  %2").arg(i).arg(c->typeToString().c_str());
            if (c->First != Sketcher::GeoEnum::GeoUndef) {
                line += QString("  F%1/%2").arg(c->First).arg(static_cast<int>(c->FirstPos));
            }
            if (c->Second != Sketcher::GeoEnum::GeoUndef) {
                line += QString("  S%1/%2").arg(c->Second).arg(static_cast<int>(c->SecondPos));
            }
            if (c->Third != Sketcher::GeoEnum::GeoUndef) {
                line += QString("  T%1/%2").arg(c->Third).arg(static_cast<int>(c->ThirdPos));
            }
            if (c->isDimensional()) {
                char buf[48] = { 0 };
                std::snprintf(buf, sizeof(buf), " = %.2f", c->getValue());
                line += buf;
            }
            switch (constraintStatus.back()) {
            case SketcherObj::ConstraintStatus::Conflicting:
                line += "   [conflict]";
                break;
            case SketcherObj::ConstraintStatus::Malformed:
                line += "   [malformed]";
                break;
            case SketcherObj::ConstraintStatus::Redundant:
                line += "   [redundant]";
                break;
            case SketcherObj::ConstraintStatus::PartiallyRedundant:
                line += "   [partly redundant]";
                break;
            case SketcherObj::ConstraintStatus::Ok:
                break;
            }
            constraintLines << line;
        }

        QStringList curveLines;
        QStringList curveIconPaths;
        std::vector<bool> curveVisible;
        for (int i = 0; i <= obj->getHighestCurveIndex(); ++i) {
            const Part::Geometry* g = obj->getGeometry(i);
            if (!g) {
                continue;
            }
            curveIconPaths << curveIconPath(g);
            curveVisible.push_back(obj->isGeometryVisible(i));
            QString typeName;
            if (g->is<Part::GeomLineSegment>()) {
                typeName = "Line";
            }
            else if (g->is<Part::GeomArcOfCircle>()) {
                typeName = "Arc";
            }
            else if (g->is<Part::GeomCircle>()) {
                typeName = "Circle";
            }
            else if (g->is<Part::GeomEllipse>()) {
                typeName = "Ellipse";
            }
            else if (g->is<Part::GeomBSplineCurve>()) {
                typeName = "BSpline";
            }
            else if (g->is<Part::GeomPoint>()) {
                typeName = "Point";
            }
            else {
                typeName = "Curve";
            }
            curveLines << QString("%1  %2").arg(i).arg(typeName);
        }

        // External geometry: what is referenced, where it comes from and whether the
        // reference still resolves. It is listed by entry (not by projected curve),
        // because that is the thing the user can drop again.
        QStringList externalLines;
        std::vector<bool> externalMissing;
        for (int i = 0; i < obj->getExternalGeometryCount(); ++i) {
            const SketcherObj::ExternalGeometry* external = obj->getExternalGeometry(i);
            if (external == nullptr) {
                continue;
            }
            externalMissing.push_back(external->missing);
            QString line = external->source
                ? QString("%1.%2").arg(external->source->GetName().c_str())
                    .arg(external->reference.c_str())
                : QString("?.%1").arg(external->reference.c_str());
            // A section and a projection of the same sub-shape are different
            // references, so the list has to say which one a row is.
            if (external->intersection) {
                line += "  [section]";
            }
            if (external->geos.size() != 1) {
                line += QString("  (%1 curves)").arg(external->geos.size());
            }
            externalLines << line;
        }

        // The solver's verdict is part of what is shown, so it is part of the cache
        // key: a solve that turns a constraint from fine into redundant has to rebuild
        // the rows even though their text did not change.
        QString statusKey;
        for (const SketcherObj::ConstraintStatus status : constraintStatus) {
            statusKey += QString::number(static_cast<int>(status));
        }
        const QString cache = constraintLines.join(QStringLiteral("\n"))
            + QStringLiteral("|") + statusKey
            + QStringLiteral("|")
            + curveLines.join(QStringLiteral("\n")) + QStringLiteral("|")
            + externalLines.join(QStringLiteral("\n"));
        if (cache == mListCache && mCurveList->count() == curveLines.size()) {
            syncCurveListSelection();
            return;
        }
        mListCache = cache;
        const QIcon eyeOpen(":/entityTree/icons/pqEyeball.svg");
        const QIcon eyeClosed(":/entityTree/icons/pqEyeballClosed.svg");
        const auto addEyeRow = [&](
            QListWidget* list,
            const QIcon& typeIcon,
            const QString& text,
            bool visible,
            int userData,
            const std::function<void(bool)>& onToggled,
            const QString& textColor = QString(),
            const QString& tooltip = QString()
        ) {
            auto* item = new QListWidgetItem();
            item->setData(Qt::UserRole, userData);
            auto* row = new QWidget(list);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(2, 2, 2, 2);
            rowLayout->setSpacing(6);
            auto* eye = new QToolButton(row);
            eye->setCheckable(true);
            eye->setAutoRaise(true);
            eye->setChecked(visible);
            eye->setIcon(visible ? eyeOpen : eyeClosed);
            eye->setToolTip(visible ? "Hide" : "Show");
            eye->setIconSize(QSize(20, 20));
            eye->setStyleSheet(
                "QToolButton { border: none; background: transparent; padding: 0px; }"
            );
            QLabel* typeLabel = nullptr;
            if (!typeIcon.isNull()) {
                typeLabel = new QLabel(row);
                typeLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                typeLabel->setPixmap(typeIcon.pixmap(24, 24));
            }
            auto* label = new QLabel(text, row);
            label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            if (!textColor.isEmpty()) {
                label->setStyleSheet(QString("color: %1;").arg(textColor));
            }
            if (!tooltip.isEmpty()) {
                label->setToolTip(tooltip);
            }
            rowLayout->addWidget(eye);
            if (typeLabel) {
                rowLayout->addWidget(typeLabel);
            }
            rowLayout->addWidget(label, 1);
            if (list == mCurveList) {
                const QVariant rowRef = QVariant::fromValue<QWidget*>(row);
                const auto installHoverFilter = [this, &rowRef](QWidget* w) {
                    w->setProperty("_curveRowWidget", rowRef);
                    w->installEventFilter(this);
                };
                row->setProperty("_curveRowId", userData);
                installHoverFilter(row);
                installHoverFilter(eye);
                if (typeLabel) {
                    installHoverFilter(typeLabel);
                }
                installHoverFilter(label);
            }
            connect(eye, &QToolButton::toggled, this, [=](bool on) {
                eye->setIcon(on ? eyeOpen : eyeClosed);
                onToggled(on);
            });
            item->setFlags(item->flags() | Qt::ItemIsSelectable);
            item->setSizeHint(row->sizeHint());
            list->addItem(item);
            list->setItemWidget(item, row);
        };

        mConstraintList->clear();
        for (int i = 0; i < constraintLines.size(); ++i) {
            QString colour;
            QString tooltip;
            switch (constraintStatus[i]) {
            case SketcherObj::ConstraintStatus::Conflicting:
                colour = "#ff6b6b";
                tooltip = "The solver cannot satisfy this together with the other "
                    "constraints of the sketch.";
                break;
            case SketcherObj::ConstraintStatus::Malformed:
                colour = "#ff6b6b";
                tooltip = "The solver cannot make sense of this constraint "
                    "(missing element or value).";
                break;
            case SketcherObj::ConstraintStatus::Redundant:
                colour = "#e8c46b";
                tooltip = "Something else in the sketch already enforces this, so it "
                    "adds no degree of freedom.";
                break;
            case SketcherObj::ConstraintStatus::PartiallyRedundant:
                colour = "#e8c46b";
                tooltip = "Part of this is already enforced by the other constraints.";
                break;
            case SketcherObj::ConstraintStatus::Ok:
                break;
            }
            addEyeRow(
                mConstraintList,
                constraintIconPaths[i].isEmpty() ? QIcon() : QIcon(constraintIconPaths[i]),
                constraintLines[i],
                constraintVisible[i],
                i,
                [obj, i](bool on) { obj->setConstraintVisible(i, on); },
                colour,
                tooltip
            );
        }
        mCurveList->clear();
        for (int i = 0; i < curveLines.size(); ++i) {
            addEyeRow(
                mCurveList,
                curveIconPaths[i].isEmpty() ? QIcon() : QIcon(curveIconPaths[i]),
                curveLines[i],
                curveVisible[i],
                i,
                [obj, i](bool on) { obj->setGeometryVisible(i, on); }
            );
        }
        // External geometry: the row shows the sub-shape the reference points at and
        // a button that drops it again (there is no per-entry visibility to toggle -
        // the entry either exists or it does not).
        mExternalList->clear();
        for (int i = 0; i < externalLines.size(); ++i) {
            auto* item = new QListWidgetItem();
            item->setData(Qt::UserRole, i);
            auto* row = new QWidget(mExternalList);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(2, 2, 2, 2);
            rowLayout->setSpacing(6);
            auto* remove = new QToolButton(row);
            remove->setAutoRaise(true);
            remove->setText(QString::fromUtf8("\u00d7"));
            remove->setToolTip("Remove the reference");
            remove->setStyleSheet(
                "QToolButton { border: none; background: transparent; padding: 0px 4px; "
                "font-size: 16px; }"
            );
            auto* label = new QLabel(externalLines[i], row);
            label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            if (externalMissing[i]) {
                label->setStyleSheet("color: #ff6b6b;");
                label->setToolTip(
                    "The source sub-shape cannot be resolved any more; the reference "
                    "draws nothing");
            }
            rowLayout->addWidget(remove);
            rowLayout->addWidget(label, 1);
            connect(remove, &QToolButton::clicked, this, [this, obj, i]() {
                obj->removeExternalGeometry(i);
                mListCache.clear();
                refreshLists();
            });
            item->setSizeHint(row->sizeHint());
            mExternalList->addItem(item);
            mExternalList->setItemWidget(item, row);
        }
        syncCurveListSelection();
    }
    void SketchTaskDialog::updateSolverStatus(SketcherObj* p_sketch)
    {
        if (mSolverStatus == nullptr || p_sketch == nullptr) {
            return;
        }
        const int dof = p_sketch->getDegreesOfFreedom();
        QString head;
        QString colour;
        if (dof < 0) {
            head = QString("Over-constrained (%1)").arg(dof);
            colour = "#ff6b6b";
        }
        else if (dof == 0) {
            head = "Fully constrained";
            colour = "#8fdc8f";
        }
        else {
            head = QString("Under-constrained: %1 degree%2 of freedom")
                .arg(dof)
                .arg(dof == 1 ? "" : "s");
            colour = "#e8c46b";
        }

        QStringList problems;
        if (p_sketch->hasConflictingConstraints()) {
            problems << "<span style='color:#ff6b6b'>conflicting</span>";
        }
        if (p_sketch->hasMalformedConstraints()) {
            problems << "<span style='color:#ff6b6b'>malformed</span>";
        }
        if (p_sketch->hasRedundantConstraints()) {
            problems << "<span style='color:#e8c46b'>redundant</span>";
        }
        if (p_sketch->hasPartiallyRedundantConstraints()) {
            problems << "<span style='color:#e8c46b'>partly redundant</span>";
        }
        QString html = QString("<span style='color:%1'>%2</span>").arg(colour, head);
        if (!problems.isEmpty()) {
            html += QString(" &mdash; ") + problems.join(QStringLiteral(", "));
        }
        mSolverStatus->setText(html);
    }
    void SketchTaskDialog::syncCurveListSelection()
    {
        if (!mInternal || !mInternal->feature || !mCurveList) {
            return;
        }
        SketcherObj* obj = mInternal->feature->getSketcherObj();
        if (!obj) {
            return;
        }
        std::set<int> selected;
        for (int geoId : obj->getSelectIds()) {
            selected.insert(geoId);
        }
        for (int row = 0; row < mCurveList->count(); ++row) {
            QListWidgetItem* item = mCurveList->item(row);
            if (!item) {
                continue;
            }
            const int geoId = item->data(Qt::UserRole).toInt();
            item->setSelected(selected.count(geoId) != 0);
        }
    }
    bool SketchTaskDialog::eventFilter(QObject* watched, QEvent* event)
    {
        if (!watched || (event->type() != QEvent::Enter && event->type() != QEvent::Leave)) {
            return QWidget::eventFilter(watched, event);
        }
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) {
            return QWidget::eventFilter(watched, event);
        }
        auto* row = widget->property("_curveRowWidget").value<QWidget*>();
        if (!row) {
            return QWidget::eventFilter(watched, event);
        }
        const int geoId = row->property("_curveRowId").toInt();
        if (event->type() == QEvent::Enter) {
            if (mCurveHoverRow && mCurveHoverRow != row) {
                mCurveHoverRow->setStyleSheet(QString());
            }
            mCurveHoverRow = row;
            row->setStyleSheet("background-color: #cfe2f5;");
            if (mInternal && mInternal->feature) {
                mInternal->feature->getSketcherObj()->setPreselect(geoId);
            }
        }
        else if (mCurveHoverRow == row) {
            row->setStyleSheet(QString());
            mCurveHoverRow = nullptr;
        }
        return QWidget::eventFilter(watched, event);
    }
}
