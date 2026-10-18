#include "Interactive/Widgets/DrawSketchHandlerRectangle.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "base/Tools.h"
#include "base/Exception.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"

namespace MOON {
    static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
    {
        return Base::Vector3d(vector2d.x, vector2d.y, 0.);
    }
	DrawSketchHandlerRectangle::DrawSketchHandlerRectangle(const std::string& name):
		DrawSketchDefaultHandler<DrawSketchHandlerRectangle, StateMachines::FiveSeekEnd, 3, RectangleConstructionMethod>(name)
	{
        makeFrame = false;
	}
	DrawSketchHandlerRectangle::~DrawSketchHandlerRectangle()
	{
	}
	void DrawSketchHandlerRectangle::onUpdate()
	{
        DrawSketchHandler::onUpdate();
	}
	void DrawSketchHandlerRectangle::onSetActive(bool flag)
	{
        if (flag) {
            // Widgets are long-lived singletons reused by the toolbar, so reset
            // all per-run state when the tool is (re)enabled.
            reset();
        }
        SupperClass::onSetActive(flag);
	}
	void DrawSketchHandlerRectangle::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        //using std::numbers::pi;
        double pi = 3.141592653;
        switch (state()) {
        case SelectMode::SeekFirst: {
           // toolWidgetManager.drawPositionAtCursor(onSketchPos);

            if (constructionMethod() == ConstructionMethod::Diagonal
                || constructionMethod() == ConstructionMethod::ThreePoints) {
                corner1 = onSketchPos;
            }
            else {  //(constructionMethod == ConstructionMethod::CenterAndCorner)
                center = onSketchPos;
            }

            //seekAndRenderAutoConstraint(sugConstraints[0], onSketchPos, Base::Vector2d(0.f, 0.f));
        } break;
        case SelectMode::SeekSecond: {
            if (constructionMethod() == ConstructionMethod::Diagonal) {
                //toolWidgetManager.drawDirectionAtCursor(onSketchPos, corner1);

                // Note : we swap corner2 and 4 to make sure the corners are CCW.
                // making things easier down the line.
                corner3 = onSketchPos;
                if ((corner3.x - corner1.x) * (corner3.y - corner1.y) > 0) {
                    corner2 = Base::Vector2d(onSketchPos.x, corner1.y);
                    corner4 = Base::Vector2d(corner1.x, onSketchPos.y);
                    cornersReversed = false;
                }
                else {
                    corner4 = Base::Vector2d(onSketchPos.x, corner1.y);
                    corner2 = Base::Vector2d(corner1.x, onSketchPos.y);
                    cornersReversed = true;
                }
                angle123 = pi / 2;
                angle412 = pi / 2;
            }
            else if (constructionMethod() == ConstructionMethod::CenterAndCorner) {
                //toolWidgetManager.drawDirectionAtCursor(onSketchPos, center);

                corner1 = center - (onSketchPos - center);
                corner3 = onSketchPos;
                if (Base::sgn(corner3.x - corner1.x) * Base::sgn(corner3.y - corner1.y) > 0) {
                    corner2 = Base::Vector2d(onSketchPos.x, corner1.y);
                    corner4 = Base::Vector2d(corner1.x, onSketchPos.y);
                    cornersReversed = false;
                }
                else {
                    corner4 = Base::Vector2d(onSketchPos.x, corner1.y);
                    corner2 = Base::Vector2d(corner1.x, onSketchPos.y);
                    cornersReversed = true;
                }
                angle123 = pi / 2;
                angle412 = pi / 2;
            }
            else if (constructionMethod() == ConstructionMethod::ThreePoints) {
                //toolWidgetManager.drawDirectionAtCursor(onSketchPos, corner1);

                corner2 = onSketchPos;
                Base::Vector2d perpendicular;
                perpendicular.x = -(corner2 - corner1).y;
                perpendicular.y = (corner2 - corner1).x;
                corner3 = corner2 + perpendicular;
                corner4 = corner1 + perpendicular;
                angle123 = pi / 2;
                angle412 = pi / 2;
                corner2Initial = corner2;
                side = getPointSideOfVector(corner3, corner2 - corner1, corner1);
            }
            else {
                //toolWidgetManager.drawDirectionAtCursor(onSketchPos, center);

                corner1 = onSketchPos;
                corner3 = center - (onSketchPos - center);
                Base::Vector2d perpendicular;
                perpendicular.x = -(onSketchPos - center).y;
                perpendicular.y = (onSketchPos - center).x;
                corner2 = center + perpendicular;
                corner4 = center - perpendicular;
                angle123 = pi / 2;
                angle412 = pi / 2;
                side = getPointSideOfVector(corner2, corner3 - corner1, corner1);
            }

            if (roundCorners) {
                length = (corner2 - corner1).Length();
                width = (corner4 - corner1).Length();
                radius = std::min(length, width) / 6;  // NOLINT
            }
            else {
                radius = 0.;
            }

            try {
                CreateAndDrawShapeGeometry();

                //toolWidgetManager.drawWidthHeightAtCursor(onSketchPos, length, width);
            }
            catch (const Base::ValueError&) {
            }  // equal points while hovering raise an objection that can be safely ignored

            //seekAndRenderAutoConstraint(sugConstraints[1], onSketchPos, Base::Vector2d(0.0, 0.0));
        } break;
        case SelectMode::SeekThird: {
            if (constructionMethod() == ConstructionMethod::Diagonal
                || constructionMethod() == ConstructionMethod::CenterAndCorner) {
                if (roundCorners) {
                    calculateRadius(onSketchPos);
                    //toolWidgetManager.drawDoubleAtCursor(onSketchPos, radius);
                }
                else {  // Normal rectangle with frame.
                    calculateThickness(onSketchPos);
                    //toolWidgetManager.drawDoubleAtCursor(onSketchPos, thickness);
                }
            }
            else if (constructionMethod() == ConstructionMethod::ThreePoints) {
                corner2 = corner2Initial;
                corner3 = onSketchPos;
                if (side == getPointSideOfVector(corner3, corner2 - corner1, corner1)) {
                    corner4 = corner1 + (corner3 - corner2);
                    cornersReversed = false;
                }
                else {
                    corner4 = corner2;
                    corner2 = corner1 + (corner3 - corner4);
                    cornersReversed = true;
                }
                Base::Vector2d a = corner1 - corner2;
                Base::Vector2d b = corner3 - corner2;
                if (fabs((sqrt(a.x * a.x + a.y * a.y) * sqrt(b.x * b.x + b.y * b.y)))
                        > Precision::Confusion()) {
                    angle123 = acos(
                        (a.x * b.x + a.y * b.y)
                        / (sqrt(a.x * a.x + a.y * a.y) * sqrt(b.x * b.x + b.y * b.y))
                    );
                }
                angle412 = pi - angle123;
                if (roundCorners) {
                    radius = std::min(length, width) / 6  // NOLINT
                        * std::min(
                            sqrt(1 - cos(angle412) * cos(angle412)),
                            sqrt(1 - cos(angle123) * cos(angle123))
                        );
                }
                else {
                    radius = 0.;
                }

                //toolWidgetManager.drawWidthHeightAtCursor(onSketchPos, length, width);
            }
            else {
                corner2 = onSketchPos;
                corner4 = center - (onSketchPos - center);
                cornersReversed = false;
                if (side != getPointSideOfVector(corner2, corner3 - corner1, corner1)) {
                    corner4 = onSketchPos;
                    corner2 = center - (onSketchPos - center);
                    cornersReversed = true;
                }
                Base::Vector2d a = corner4 - corner1;
                Base::Vector2d b = corner2 - corner1;
                if (fabs((sqrt(a.x * a.x + a.y * a.y) * sqrt(b.x * b.x + b.y * b.y)))
                        > Precision::Confusion()) {
                    angle412 = acos(
                        (a.x * b.x + a.y * b.y)
                        / (sqrt(a.x * a.x + a.y * a.y) * sqrt(b.x * b.x + b.y * b.y))
                    );
                }
                angle123 = pi - angle412;
                if (roundCorners) {
                    radius = std::min(length, width) / 6  // NOLINT
                        * std::min(
                            sqrt(1 - cos(angle412) * cos(angle412)),
                            sqrt(1 - cos(angle123) * cos(angle123))
                        );
                }
                else {
                    radius = 0.;
                }

                //toolWidgetManager.drawWidthHeightAtCursor(onSketchPos, length, width);
            }

            try {
                CreateAndDrawShapeGeometry();
            }
            catch (const Base::ValueError&) {
            }  // equal points while hovering raise an objection that can be safely ignored

            if ((constructionMethod() == ConstructionMethod::ThreePoints
                || constructionMethod() == ConstructionMethod::CenterAnd3Points)) {
                //seekAndRenderAutoConstraint(sugConstraints[2], onSketchPos, Base::Vector2d(0.0, 0.0));
            }
        } break;
        case SelectMode::SeekFourth: {
            if (constructionMethod() == ConstructionMethod::Diagonal
                || constructionMethod() == ConstructionMethod::CenterAndCorner) {
                calculateThickness(onSketchPos);
                //toolWidgetManager.drawDoubleAtCursor(onSketchPos, thickness);
            }
            else {
                if (roundCorners) {
                    calculateRadius(onSketchPos);
                    //toolWidgetManager.drawDoubleAtCursor(onSketchPos, radius);
                }
                else {
                    calculateThickness(onSketchPos);
                    //toolWidgetManager.drawDoubleAtCursor(onSketchPos, thickness);
                }
            }

            CreateAndDrawShapeGeometry();
        } break;
        case SelectMode::SeekFifth: {
            calculateThickness(onSketchPos);
            //toolWidgetManager.drawDoubleAtCursor(onSketchPos, thickness);

            CreateAndDrawShapeGeometry();
        } break;
        default:
            break;
        }
	}
	void DrawSketchHandlerRectangle::onButtonPressed(Base::Vector2d onSketchPos)
	{
        this->updateDataAndDrawToPosition(onSketchPos);

        if (canGoToNextMode()) {
            if (constructionMethod() == ConstructionMethod::Diagonal
                || constructionMethod() == ConstructionMethod::CenterAndCorner) {
                if (state() == SelectMode::SeekSecond && !roundCorners && !makeFrame) {
                    setState(SelectMode::End);
                }
                else if (
                    (state() == SelectMode::SeekThird && roundCorners && !makeFrame)
                    || (state() == SelectMode::SeekThird && !roundCorners && makeFrame)
                    ) {
                    setState(SelectMode::End);
                }
                else if (state() == SelectMode::SeekFourth) {
                    setState(SelectMode::End);
                }
                else {
                    this->moveToNextMode();
                }
            }
            else {
                if (state() == SelectMode::SeekThird && !roundCorners && !makeFrame) {
                    setState(SelectMode::End);
                }
                else if (
                    (state() == SelectMode::SeekFourth && roundCorners && !makeFrame)
                    || (state() == SelectMode::SeekFourth && !roundCorners && makeFrame)
                    ) {
                    setState(SelectMode::End);
                }
                else {
                    this->moveToNextMode();
                }
            }
        }
	}
	bool DrawSketchHandlerRectangle::canGoToNextMode()
	{
        if (state() == SelectMode::SeekSecond
            && (length < Precision::Confusion() || width < Precision::Confusion())) {
            return false;
        }

        return true;
	}
	void DrawSketchHandlerRectangle::createShape(bool onlyeditoutline)
	{
        ShapeGeometry.clear();

        Base::Vector2d vecL = corner2 - corner1;
        Base::Vector2d vecW = corner4 - corner1;
        length = vecL.Length();
        width = vecW.Length();
        angle = vecL.Angle();
        if (length < Precision::Confusion() || width < Precision::Confusion()
            || fmod(fabs(angle123), std::numbers::pi) < Precision::Confusion()) {
            return;
        }

        vecL = vecL / length;
        vecW = vecW / width;
        double L1 = radius;
        double L2 = radius;
        if (cos(angle123 / 2) != 1 && cos(angle412 / 2) != 1) {
            L1 = radius / sqrt(1 - cos(angle123 / 2) * cos(angle123 / 2));
            L2 = radius / sqrt(1 - cos(angle412 / 2) * cos(angle412 / 2));
        }

        createFirstRectangleGeometries(vecL, vecW, L1, L2);

        bool thicknessNotZero = fabs(thickness) > Precision::Confusion();
        bool negThicknessEqualRadius = fabs(radius + thickness) < Precision::Confusion();
        if (makeFrame && state() != SelectMode::SeekSecond && thicknessNotZero) {
            //createSecondRectangleGeometries(vecL, vecW, L1, L2);
        }

        if (!onlyeditoutline) {
            //ShapeConstraints.clear();

            if (radius > Precision::Confusion()) {
                finishOblongCreation(thicknessNotZero, negThicknessEqualRadius);
            }
            else {  // cases of normal rectangles and normal frames
                finishRectangleCreation(thicknessNotZero);
            }
        }
	}
    void DrawSketchHandlerRectangle::onReset()
    {
        thickness = 0.;
        lengthSign = 0;
        widthSign = 0;
        //toolWidgetManager.resetControls();
    }
    void DrawSketchHandlerRectangle::executeCommands()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        // The new geometry starts right after the current sketch content.
        firstCurve = Obj->getHighestCurveIndex() + 1;

        // Rebuild the final geometry, including any construction elements that
        // are only appended on the real (non-preview) build.
        createShape(false);

        if (ShapeGeometry.empty()) {
            return;
        }

        // Commit geometries first; they get the ids firstCurve, firstCurve+1, ...
        // in ShapeGeometry order.
        SupperClass::executeCommands();

        // Frame rectangles are not wired in this port; plain and rounded
        // rectangles get their own constraint sets.
        if (!makeFrame) {
            if (radius <= Precision::Confusion()) {
                addRectangleAutoConstraints();
            }
            else {
                addRoundedRectangleAutoConstraints();
            }
            Obj->solve();
            // Corner/centre anchor points are only construction aids for the
            // auto constraints; keep them in the solver but hide their dots.
            if (constructionPointOneId >= 0) {
                Obj->setConstruction(constructionPointOneId, true);
            }
            if (constructionPointTwoId >= 0) {
                Obj->setConstruction(constructionPointTwoId, true);
            }
            if (constructionPointThreeId >= 0) {
                Obj->setConstruction(constructionPointThreeId, true);
            }
            if (centerPointId >= 0) {
                Obj->setConstruction(centerPointId, true);
            }
        }

        thickness = 0.;
    }
    void DrawSketchHandlerRectangle::addRectangleAutoConstraints()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        addCornerCoincidences(firstCurve);

        addAlignmentConstraints();

        // Centered variants keep the two opposite corners symmetric about the
        // center construction point.
        if (constructionMethod() == ConstructionMethod::CenterAndCorner
            || constructionMethod() == ConstructionMethod::CenterAnd3Points) {
            Obj->addConstraint(
                Sketcher::ConstraintType::Symmetric,
                firstCurve + 2, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::start,
                centerPointId, Sketcher::PointPos::start
            );
        }
    }
    void DrawSketchHandlerRectangle::addCornerCoincidences(int geoId)
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        // Corner coincidences: the end of each side is the start of the next one.
        Obj->addConstraint(
            Sketcher::ConstraintType::Coincident,
            geoId, Sketcher::PointPos::end,
            geoId + 1, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Coincident,
            geoId + 1, Sketcher::PointPos::end,
            geoId + 2, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Coincident,
            geoId + 2, Sketcher::PointPos::end,
            geoId + 3, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Coincident,
            geoId + 3, Sketcher::PointPos::end,
            geoId, Sketcher::PointPos::start
        );
    }
    void DrawSketchHandlerRectangle::addAlignmentConstraints()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        // Opposite sides are aligned with the sketch axes (or kept parallel for
        // the oblique 3-point construction methods).
        Sketcher::ConstraintType typeA = Sketcher::ConstraintType::Horizontal;
        Sketcher::ConstraintType typeB = Sketcher::ConstraintType::Vertical;
        if (Base::sgn(corner3.x - corner1.x) * Base::sgn(corner3.y - corner1.y) < 0) {
            typeA = Sketcher::ConstraintType::Vertical;
            typeB = Sketcher::ConstraintType::Horizontal;
        }

        if (fabs(angle) < Precision::Confusion()
            || constructionMethod() == ConstructionMethod::Diagonal
            || constructionMethod() == ConstructionMethod::CenterAndCorner) {
            Obj->addConstraint(typeA, firstCurve, Sketcher::PointPos::none);
            Obj->addConstraint(typeA, firstCurve + 2, Sketcher::PointPos::none);
            Obj->addConstraint(typeB, firstCurve + 1, Sketcher::PointPos::none);
            Obj->addConstraint(typeB, firstCurve + 3, Sketcher::PointPos::none);
        }
        else {
            Obj->addConstraint(
                Sketcher::ConstraintType::Parallel,
                firstCurve, Sketcher::PointPos::none,
                firstCurve + 2, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::Parallel,
                firstCurve + 1, Sketcher::PointPos::none,
                firstCurve + 3, Sketcher::PointPos::none
            );
            if (fabs(angle123 - std::numbers::pi / 2) < Precision::Confusion()) {
                Obj->addConstraint(
                    Sketcher::ConstraintType::Perpendicular,
                    firstCurve, Sketcher::PointPos::none,
                    firstCurve + 1, Sketcher::PointPos::none
                );
            }
        }
    }
    void DrawSketchHandlerRectangle::addTangentCoincidences(int geoId)
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        // Tangency at every line/arc joint. Geometry layout: lines 0..3,
        // arcs 4..7 (arc i starts at the end of line i and runs to the start
        // of line (i+1)%4).
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId, Sketcher::PointPos::start,
            geoId + 4, Sketcher::PointPos::end
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId, Sketcher::PointPos::end,
            geoId + 5, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 1, Sketcher::PointPos::start,
            geoId + 5, Sketcher::PointPos::end
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 1, Sketcher::PointPos::end,
            geoId + 6, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 2, Sketcher::PointPos::start,
            geoId + 6, Sketcher::PointPos::end
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 2, Sketcher::PointPos::end,
            geoId + 7, Sketcher::PointPos::start
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 3, Sketcher::PointPos::start,
            geoId + 7, Sketcher::PointPos::end
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Tangent,
            geoId + 3, Sketcher::PointPos::end,
            geoId + 4, Sketcher::PointPos::start
        );
    }
    void DrawSketchHandlerRectangle::addArcEqualities()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        // All four corner arcs share one radius.
        Obj->addConstraint(
            Sketcher::ConstraintType::Equal,
            firstCurve + 4, Sketcher::PointPos::none,
            firstCurve + 5, Sketcher::PointPos::none
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Equal,
            firstCurve + 5, Sketcher::PointPos::none,
            firstCurve + 6, Sketcher::PointPos::none
        );
        Obj->addConstraint(
            Sketcher::ConstraintType::Equal,
            firstCurve + 6, Sketcher::PointPos::none,
            firstCurve + 7, Sketcher::PointPos::none
        );
    }
    void DrawSketchHandlerRectangle::addRoundedRectangleAutoConstraints()
    {
        SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (!Obj) return;

        addTangentCoincidences(firstCurve);

        addAlignmentConstraints();

        addArcEqualities();

        // Construction corner points (ids set by the finishOblong* helpers
        // during createShape(false)) tie the outer corners to the sides; the
        // centered variants also mirror opposite corners about the center.
        switch (constructionMethod()) {
        case ConstructionMethod::Diagonal:
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve + 3, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointTwoId, Sketcher::PointPos::start,
                firstCurve + 1, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointTwoId, Sketcher::PointPos::start,
                firstCurve + 2, Sketcher::PointPos::none
            );
            break;
        case ConstructionMethod::ThreePoints:
            if (!cornersReversed) {
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve, Sketcher::PointPos::none
                );
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 1, Sketcher::PointPos::none
                );
            }
            else {
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 2, Sketcher::PointPos::none
                );
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 3, Sketcher::PointPos::none
                );
            }
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve + 3, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointThreeId, Sketcher::PointPos::start,
                firstCurve + 1, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointThreeId, Sketcher::PointPos::start,
                firstCurve + 2, Sketcher::PointPos::none
            );
            break;
        case ConstructionMethod::CenterAnd3Points:
            if (!cornersReversed) {
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve, Sketcher::PointPos::none
                );
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 1, Sketcher::PointPos::none
                );
            }
            else {
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 2, Sketcher::PointPos::none
                );
                Obj->addConstraint(
                    Sketcher::ConstraintType::PointOnObject,
                    constructionPointTwoId, Sketcher::PointPos::start,
                    firstCurve + 3, Sketcher::PointPos::none
                );
            }
            Obj->addConstraint(
                Sketcher::ConstraintType::Symmetric,
                firstCurve + 2, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::start,
                centerPointId, Sketcher::PointPos::start
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve + 3, Sketcher::PointPos::none
            );
            break;
        default:  // CenterAndCorner
            Obj->addConstraint(
                Sketcher::ConstraintType::Symmetric,
                firstCurve + 2, Sketcher::PointPos::start,
                firstCurve, Sketcher::PointPos::start,
                centerPointId, Sketcher::PointPos::start
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve + 1, Sketcher::PointPos::none
            );
            Obj->addConstraint(
                Sketcher::ConstraintType::PointOnObject,
                constructionPointOneId, Sketcher::PointPos::start,
                firstCurve + 2, Sketcher::PointPos::none
            );
            break;
        }
    }
    int DrawSketchHandlerRectangle::getPointSideOfVector(Base::Vector2d pointToCheck, Base::Vector2d separatingVector, Base::Vector2d pointOnVector)
    {
        Base::Vector2d secondPointOnVec = pointOnVector + separatingVector;
        double d = (pointToCheck.x - pointOnVector.x) * (secondPointOnVec.y - pointOnVector.y)
            - (pointToCheck.y - pointOnVector.y) * (secondPointOnVec.x - pointOnVector.x);
        if (abs(d) < Precision::Confusion()) {
            return 0;
        }
        else if (d < 0) {
            return -1;
        }
        else {
            return 1;
        }
    }
    void DrawSketchHandlerRectangle::calculateRadius(Base::Vector2d onSketchPos)
    {
        Base::Vector2d u = (corner2 - corner1) / (corner2 - corner1).Length();
        Base::Vector2d v = (corner4 - corner1) / (corner4 - corner1).Length();
        Base::Vector2d e = onSketchPos - corner1;
        double du = (v.y * e.x - v.x * e.y) / (u.x * v.y - u.y * v.x);
        double dv = (-u.y * e.x + u.x * e.y) / (u.x * v.y - u.y * v.x);

        if (-Precision::Confusion() < du && du < 0) {
            du = 0.0;
        }
        if (-Precision::Confusion() < dv && dv < 0) {
            dv = 0.0;
        }

        if (du < 0.0 || du > length || dv < 0.0 || dv > width) {
            radius = 0.;
        }
        else {
            if (du < length - du && dv < width - dv) {
                radius = (du + dv
                    + std::max(
                        2 * sqrt(du * dv) * sin(angle412 / 2),
                        -2 * sqrt(du * dv) * sin(angle412 / 2)
                    ))
                    * tan(angle412 / 2);
            }
            else if (du > length - du && dv < width - dv) {
                du = length - du;
                radius = (du + dv
                    + std::max(
                        2 * sqrt(du * dv) * sin(angle123 / 2),
                        -2 * sqrt(du * dv) * sin(angle123 / 2)
                    ))
                    * tan(angle123 / 2);
            }
            else if (du < length - du && dv > width - dv) {
                dv = width - dv;
                radius = (du + dv
                    + std::max(
                        2 * sqrt(du * dv) * sin(angle123 / 2),
                        -2 * sqrt(du * dv) * sin(angle123 / 2)
                    ))
                    * tan(angle123 / 2);
            }
            else {
                du = length - du;
                dv = width - dv;
                radius = (du + dv
                    + std::max(
                        2 * sqrt(du * dv) * sin(angle412 / 2),
                        -2 * sqrt(du * dv) * sin(angle412 / 2)
                    ))
                    * tan(angle412 / 2);
            }
            radius = std::min(
                radius,
                std::min(length * 0.999, width * 0.999)  // NOLINT
                / (cos(angle412 / 2) / sqrt(1 - cos(angle412 / 2) * cos(angle412 / 2))
                    + cos(angle123 / 2) / sqrt(1 - cos(angle123 / 2) * cos(angle123 / 2)))
            );
        }
    }
    void DrawSketchHandlerRectangle::calculateThickness(Base::Vector2d onSketchPos)
    {
        Base::Vector2d u = (corner2 - corner1) / (corner2 - corner1).Length();
        Base::Vector2d v = (corner4 - corner1) / (corner4 - corner1).Length();
        Base::Vector2d e = onSketchPos - corner1;
        double obliqueThickness = 0.;
        double du = (v.y * e.x - v.x * e.y) / (u.x * v.y - u.y * v.x);
        double dv = (-u.y * e.x + u.x * e.y) / (u.x * v.y - u.y * v.x);
        if (du > 0 && du < length && !(dv > 0 && dv < width)) {
            obliqueThickness = std::min(fabs(dv), fabs(width - dv));
        }
        else if (dv > 0 && dv < width && !(du > 0 && du < length)) {
            obliqueThickness = std::min(fabs(du), fabs(length - du));
        }
        else if (du > 0 && du < length && dv > 0 && dv < width) {
            obliqueThickness = -std::min(
                std::min(fabs(du), fabs(length - du)),
                std::min(fabs(dv), fabs(width - dv))
            );
        }
        else {
            obliqueThickness = std::max(
                std::min(fabs(du), fabs(length - du)),
                std::min(fabs(dv), fabs(width - dv))
            );
        }


        frameCorner1 = corner1 - u * obliqueThickness - v * obliqueThickness;
        frameCorner2 = corner2 + u * obliqueThickness - v * obliqueThickness;
        frameCorner3 = corner3 + u * obliqueThickness + v * obliqueThickness;
        frameCorner4 = corner4 - u * obliqueThickness + v * obliqueThickness;

        thickness = obliqueThickness * sin(angle412);
    }
    void DrawSketchHandlerRectangle::createFirstRectangleGeometries(Base::Vector2d vecL, Base::Vector2d vecW, double L1, double L2)
    {
        createFirstRectangleLines(vecL, vecW, L1, L2);

        if (roundCorners && radius > Precision::Confusion()) {
            createFirstRectangleFillets(vecL, vecW, L1, L2);
        }
    }
    void DrawSketchHandlerRectangle::createFirstRectangleLines(Base::Vector2d vecL, Base::Vector2d vecW, double L1, double L2)
    {
        addLineToShapeGeometry(
            toVector3d(corner1 + vecL * L2 * cos(angle412 / 2)),
            toVector3d(corner2 - vecL * L1 * cos(angle123 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(corner2 + vecW * L1 * cos(angle123 / 2)),
            toVector3d(corner3 - vecW * L2 * cos(angle412 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(corner3 - vecL * L2 * cos(angle412 / 2)),
            toVector3d(corner4 + vecL * L1 * cos(angle123 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(corner4 - vecW * L1 * cos(angle123 / 2)),
            toVector3d(corner1 + vecW * L2 * cos(angle412 / 2)),
            false
        );
    }
    void DrawSketchHandlerRectangle::createFirstRectangleFillets(Base::Vector2d vecL, Base::Vector2d vecW, double L1, double L2)
    {
        
        double pi = 3.141592653;
        // center points required later for special case of round corner frame with
        // radiusFrame = 0.
        double end = angle - pi / 2;

        Base::Vector2d b1 = (vecL + vecW) / (vecL + vecW).Length();
        Base::Vector2d b2 = (vecL - vecW) / (vecL - vecW).Length();
        center1 = toVector3d(corner1 + b1 * L2);
        center2 = toVector3d(corner2 - b2 * L1);
        center3 = toVector3d(corner3 - b1 * L2);
        center4 = toVector3d(corner4 + b2 * L1);

        addArcToShapeGeometry(center1, end - pi + angle412, end, radius, false);
        addArcToShapeGeometry(center2, end, end - pi - angle123, radius, false);
        addArcToShapeGeometry(center3, end + angle412, end - pi, radius, false);
        addArcToShapeGeometry(center4, end - pi, end - angle123, radius, false);
    }
    void DrawSketchHandlerRectangle::createSecondRectangleGeometries(Base::Vector2d vecL, Base::Vector2d vecW, double L1, double L2)
    {
        using std::numbers::pi;

        double end = angle - pi / 2;

        if (radius < Precision::Confusion()) {
            radiusFrame = 0.;
        }
        else {
            radiusFrame = radius + thickness;
            if (radiusFrame < 0.) {
                radiusFrame = 0.;
            }
        }

        Base::Vector2d vecLF = frameCorner2 - frameCorner1;
        Base::Vector2d vecWF = frameCorner4 - frameCorner1;
        double lengthF = vecLF.Length();
        double widthF = vecWF.Length();

        double L1F = 0.;
        double L2F = 0.;
        if (radius > Precision::Confusion()) {
            L1F = L1 * radiusFrame / radius;
            L2F = L2 * radiusFrame / radius;
        }

        addLineToShapeGeometry(
            toVector3d(frameCorner1 + vecLF / lengthF * L2F * cos(angle412 / 2)),
            toVector3d(frameCorner2 - vecLF / lengthF * L1F * cos(angle123 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(frameCorner2 + vecWF / widthF * L1F * cos(angle123 / 2)),
            toVector3d(frameCorner3 - vecWF / widthF * L2F * cos(angle412 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(frameCorner3 - vecLF / lengthF * L2F * cos(angle412 / 2)),
            toVector3d(frameCorner4 + vecLF / lengthF * L1F * cos(angle123 / 2)),
            false
        );
        addLineToShapeGeometry(
            toVector3d(frameCorner4 - vecWF / widthF * L1F * cos(angle123 / 2)),
            toVector3d(frameCorner1 + vecWF / widthF * L2F * cos(angle412 / 2)),
            false
        );

        if (roundCorners && radiusFrame > Precision::Confusion()) {
            Base::Vector2d b1 = (vecL + vecW) / (vecL + vecW).Length();
            Base::Vector2d b2 = (vecL - vecW) / (vecL - vecW).Length();

            addArcToShapeGeometry(
                toVector3d(frameCorner1 + b1 * L2F),
                end - pi + angle412,
                end,
                radiusFrame,
                false
            );
            addArcToShapeGeometry(
                toVector3d(frameCorner2 - b2 * L1F),
                end,
                end - pi - angle123,
                radiusFrame,
                false
            );
            addArcToShapeGeometry(
                toVector3d(frameCorner3 - b1 * L2F),
                end + angle412,
                end - pi,
                radiusFrame,
                false
            );
            addArcToShapeGeometry(
                toVector3d(frameCorner4 + b2 * L1F),
                end - pi,
                end - angle123,
                radiusFrame,
                false
            );
        }
    }
    void DrawSketchHandlerRectangle::finishOblongCreation(bool thicknessNotZero, bool negThicknessEqualRadius)
    {
       // addTangentCoincidences(firstCurve);

        //addAlignmentConstraints();

        //addArcEqualities();

        if (thicknessNotZero) {
            // There are 3 cases possible:
            // 1 - Thickness is negative and -thickness == radius.
            // In this case the inner rectangle is a normal rectangle and its corner
            // match the centers of the outer arcs.
            // 2 - Thickness is negative and  radius < -thickness.
            // In this case it's a normal rectangle but we need construction
            // lines to constraint it.
            // 3 - Thickness is either positive or negative and radius > -thickness.
            // In this case the second rectangle is also round-cornered
            if (radiusFrame < Precision::Confusion()) {
                //addRectangleCoincidences(firstCurve + 8);  // NOLINT

                // Case 1
                if (negThicknessEqualRadius) {
                    //finishOblongFrameCase1();
                }
                else {
                    finishOblongFrameCase2();
                }
            }
            else {  // case 3: inner rectangle is rounded rectangle
                //finishOblongFrameCase3();
            }
        }

        if (constructionMethod() == ConstructionMethod::ThreePoints) {
            finishOblongThreePoints(thicknessNotZero, negThicknessEqualRadius);
        }
        else if (constructionMethod() == ConstructionMethod::CenterAnd3Points) {
            finishOblongCenterAnd3Points(thicknessNotZero, negThicknessEqualRadius);
        }
        else if (constructionMethod() == ConstructionMethod::CenterAndCorner) {
            finishOblongCenterAndCorner(thicknessNotZero, negThicknessEqualRadius);
        }
        else {
            finishOblongDiagonal(thicknessNotZero, negThicknessEqualRadius);
        }
    }
    void DrawSketchHandlerRectangle::finishOblongFrameCase2()
    {
        // case 2: add construction lines +12, +13, +14, +15

        //addFrameAlignmentConstraints(firstCurve + 8);

        addLineToShapeGeometry(center1, Base::Vector3d(frameCorner1.x, frameCorner1.y, 0.), true);
        addLineToShapeGeometry(center2, Base::Vector3d(frameCorner2.x, frameCorner2.y, 0.), true);
        addLineToShapeGeometry(center3, Base::Vector3d(frameCorner3.x, frameCorner3.y, 0.), true);
        addLineToShapeGeometry(center4, Base::Vector3d(frameCorner4.x, frameCorner4.y, 0.), true);

        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 12,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 4,  // NOLINT
        //    Sketcher::PointPos::mid
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 12,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 8,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 13,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 5,  // NOLINT
        //    Sketcher::PointPos::mid
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 13,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 9,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 14,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 6,  // NOLINT
        //    Sketcher::PointPos::mid
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 14,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 10,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 15,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 7,  // NOLINT
        //    Sketcher::PointPos::mid
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 15,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 11,  // NOLINT
        //    Sketcher::PointPos::start
        //);

        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 12,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 13
        //);  // NOLINT
        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 13,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 14
        //);  // NOLINT
        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 14,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 15
        //);  // NOLINT
    }
    void DrawSketchHandlerRectangle::finishOblongThreePoints(bool thicknessNotZero, bool negThicknessEqualRadius)
    {
        if (thicknessNotZero) {
            if (negThicknessEqualRadius) {
                constructionPointOneId = firstCurve + 12;    // NOLINT
                constructionPointTwoId = firstCurve + 13;    // NOLINT
                constructionPointThreeId = firstCurve + 14;  // NOLINT
            }
            else {
                constructionPointOneId = firstCurve + 16;    // NOLINT
                constructionPointTwoId = firstCurve + 17;    // NOLINT
                constructionPointThreeId = firstCurve + 18;  // NOLINT
            }
        }
        else {
            constructionPointOneId = firstCurve + 8;     // NOLINT
            constructionPointTwoId = firstCurve + 9;     // NOLINT
            constructionPointThreeId = firstCurve + 10;  // NOLINT
        }

        addPointToShapeGeometry(Base::Vector3d(corner1.x, corner1.y, 0.), true);
        if (!cornersReversed) {
            addPointToShapeGeometry(Base::Vector3d(corner2.x, corner2.y, 0.), true);
 /*           addToShapeConstraints(
                Sketcher::PointOnObject,
                constructionPointTwoId,
                Sketcher::PointPos::start,
                firstCurve
            );
            addToShapeConstraints(
                Sketcher::PointOnObject,
                constructionPointTwoId,
                Sketcher::PointPos::start,
                firstCurve + 1
            );*/
        }
        else {
            addPointToShapeGeometry(Base::Vector3d(corner4.x, corner4.y, 0.), true);
      /*      addToShapeConstraints(
                Sketcher::PointOnObject,
                constructionPointTwoId,
                Sketcher::PointPos::start,
                firstCurve + 2
            );
            addToShapeConstraints(
                Sketcher::PointOnObject,
                constructionPointTwoId,
                Sketcher::PointPos::start,
                firstCurve + 3
            );*/
        }
        addPointToShapeGeometry(Base::Vector3d(corner3.x, corner3.y, 0.), true);
     /*   addToShapeConstraints(
            Sketcher::PointOnObject,
            constructionPointOneId,
            Sketcher::PointPos::start,
            firstCurve
        );
        addToShapeConstraints(
            Sketcher::PointOnObject,
            constructionPointOneId,
            Sketcher::PointPos::start,
            firstCurve + 3
        );
        addToShapeConstraints(
            Sketcher::PointOnObject,
            constructionPointThreeId,
            Sketcher::PointPos::start,
            firstCurve + 1
        );
        addToShapeConstraints(
            Sketcher::PointOnObject,
            constructionPointThreeId,
            Sketcher::PointPos::start,
            firstCurve + 2
        );*/
    }
    void DrawSketchHandlerRectangle::finishOblongCenterAnd3Points(bool thicknessNotZero, bool negThicknessEqualRadius)
    {
        if (thicknessNotZero) {
            if (negThicknessEqualRadius) {
                constructionPointOneId = firstCurve + 12;  // NOLINT
                constructionPointTwoId = firstCurve + 13;  // NOLINT
                centerPointId = firstCurve + 14;           // NOLINT
            }
            else {
                constructionPointOneId = firstCurve + 16;  // NOLINT
                constructionPointTwoId = firstCurve + 17;  // NOLINT
                centerPointId = firstCurve + 18;           // NOLINT
            }
        }
        else {
            constructionPointOneId = firstCurve + 8;  // NOLINT
            constructionPointTwoId = firstCurve + 9;  // NOLINT
            centerPointId = firstCurve + 10;          // NOLINT
        }

        addPointToShapeGeometry(Base::Vector3d(corner1.x, corner1.y, 0.), true);
        if (!cornersReversed) {
            addPointToShapeGeometry(Base::Vector3d(corner2.x, corner2.y, 0.), true);
            //addToShapeConstraints(
            //    Sketcher::PointOnObject,
            //    constructionPointTwoId,
            //    Sketcher::PointPos::start,
            //    firstCurve
            //);
            //addToShapeConstraints(
            //    Sketcher::PointOnObject,
            //    constructionPointTwoId,
            //    Sketcher::PointPos::start,
            //    firstCurve + 1
            //);
        }
        else {
            addPointToShapeGeometry(Base::Vector3d(corner4.x, corner4.y, 0.), true);
            //addToShapeConstraints(
            //    Sketcher::PointOnObject,
            //    constructionPointTwoId,
            //    Sketcher::PointPos::start,
            //    firstCurve + 2
            //);
            //addToShapeConstraints(
            //    Sketcher::PointOnObject,
            //    constructionPointTwoId,
            //    Sketcher::PointPos::start,
            //    firstCurve + 3
            //);
        }
        addPointToShapeGeometry(Base::Vector3d(center.x, center.y, 0.), true);
        //addToShapeConstraints(
        //    Sketcher::Symmetric,
        //    firstCurve + 2,
        //    Sketcher::PointPos::start,
        //    firstCurve,
        //    Sketcher::PointPos::start,
        //    centerPointId,
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 3
        //);  // NOLINT
    }
    void DrawSketchHandlerRectangle::finishOblongCenterAndCorner(bool thicknessNotZero, bool negThicknessEqualRadius)
    {
        if (thicknessNotZero) {
            if (negThicknessEqualRadius) {
                constructionPointOneId = firstCurve + 12;  // NOLINT
                centerPointId = firstCurve + 13;           // NOLINT
            }
            else {
                constructionPointOneId = firstCurve + 16;  // NOLINT
                centerPointId = firstCurve + 17;           // NOLINT
            }
        }
        else {
            constructionPointOneId = firstCurve + 8;  // NOLINT
            centerPointId = firstCurve + 9;           // NOLINT
        }

        addPointToShapeGeometry(Base::Vector3d(corner3.x, corner3.y, 0.), true);
        addPointToShapeGeometry(Base::Vector3d(center.x, center.y, 0.), true);
        //addToShapeConstraints(
        //    Sketcher::Symmetric,
        //    firstCurve + 2,
        //    Sketcher::PointPos::start,
        //    firstCurve,
        //    Sketcher::PointPos::start,
        //    centerPointId,
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 1
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 2
        //);
    }
    void DrawSketchHandlerRectangle::finishOblongDiagonal(bool thicknessNotZero, bool negThicknessEqualRadius)
    {
        if (thicknessNotZero) {
            if (negThicknessEqualRadius) {
                constructionPointOneId = firstCurve + 12;  // NOLINT
                constructionPointTwoId = firstCurve + 13;  // NOLINT
            }
            else {
                constructionPointOneId = firstCurve + 16;  // NOLINT
                constructionPointTwoId = firstCurve + 17;  // NOLINT
            }
        }
        else {
            constructionPointOneId = firstCurve + 8;  // NOLINT
            constructionPointTwoId = firstCurve + 9;  // NOLINT
        }

        addPointToShapeGeometry(Base::Vector3d(corner1.x, corner1.y, 0.), true);
        addPointToShapeGeometry(Base::Vector3d(corner3.x, corner3.y, 0.), true);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointOneId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 3
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointTwoId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 1
        //);
        //addToShapeConstraints(
        //    Sketcher::PointOnObject,
        //    constructionPointTwoId,
        //    Sketcher::PointPos::start,
        //    firstCurve + 2
        //);
    }
    void DrawSketchHandlerRectangle::finishRectangleCreation(bool thicknessNotZero)
    {
        //addRectangleCoincidences(firstCurve);

        //addAlignmentConstraints();

        if (thicknessNotZero) {
            finishRectangleFrameCreation();
        }

        if (constructionMethod() == ConstructionMethod::CenterAndCorner
            || constructionMethod() == ConstructionMethod::CenterAnd3Points) {
            finishCenteredRectangleCreation(thicknessNotZero);
        }
    }
    void DrawSketchHandlerRectangle::finishRectangleFrameCreation()
    {
        //addRectangleCoincidences(firstCurve + 4);

        //addFrameAlignmentConstraints(firstCurve + 4);

        addRectangleFrameConstructionLines();
    }
    void DrawSketchHandlerRectangle::addRectangleFrameConstructionLines()
    {
        addLineToShapeGeometry(
            Base::Vector3d(corner1.x, corner1.y, 0.),
            Base::Vector3d(frameCorner1.x, frameCorner1.y, 0.),
            true
        );
        addLineToShapeGeometry(
            Base::Vector3d(corner2.x, corner2.y, 0.),
            Base::Vector3d(frameCorner2.x, frameCorner2.y, 0.),
            true
        );
        addLineToShapeGeometry(
            Base::Vector3d(corner3.x, corner3.y, 0.),
            Base::Vector3d(frameCorner3.x, frameCorner3.y, 0.),
            true
        );
        addLineToShapeGeometry(
            Base::Vector3d(corner4.x, corner4.y, 0.),
            Base::Vector3d(frameCorner4.x, frameCorner4.y, 0.),
            true
        );

        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 8,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve,
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 8,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 4,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 9,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 1,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 9,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 5,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 10,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 2,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 10,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 6,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 11,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve + 3,  // NOLINT
        //    Sketcher::PointPos::start
        //);
        //addToShapeConstraints(
        //    Sketcher::Coincident,
        //    firstCurve + 11,  // NOLINT
        //    Sketcher::PointPos::end,
        //    firstCurve + 7,  // NOLINT
        //    Sketcher::PointPos::start
        //);

        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 8,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 9
        //);  // NOLINT
        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 9,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 10
        //);  // NOLINT
        //addToShapeConstraints(
        //    Sketcher::Perpendicular,
        //    firstCurve + 10,  // NOLINT
        //    Sketcher::PointPos::none,
        //    firstCurve + 11
        //);  // NOLINT
    }
    void DrawSketchHandlerRectangle::finishCenteredRectangleCreation(bool thicknessNotZero)
    {
        if (thicknessNotZero) {
            centerPointId = firstCurve + 12;  // NOLINT
        }
        else {
            centerPointId = firstCurve + 4;  // NOLINT
        }

        addPointToShapeGeometry(Base::Vector3d(center.x, center.y, 0.), true);
        //addToShapeConstraints(
        //    Sketcher::Symmetric,
        //    firstCurve + 2,  // NOLINT
        //    Sketcher::PointPos::start,
        //    firstCurve,
        //    Sketcher::PointPos::start,
        //    centerPointId,
        //    Sketcher::PointPos::start
        //);
    }
}
