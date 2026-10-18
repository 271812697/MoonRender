#include "Interactive/Widgets/DrawSketchHandlerTranslate.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"

namespace MOON {

	namespace
	{
		/** @return where p_geoId sits in p_geoIds, or -1 when it is not one of them. */
		int indexOfGeoId(const std::vector<int>& p_geoIds, int p_geoId)
		{
			for (size_t i = 0; i < p_geoIds.size(); i++) {
				if (p_geoIds[i] == p_geoId) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}
	}

	DrawSketchHandlerTranslate::DrawSketchHandlerTranslate(const std::string& name) :SupperClass(name),
		deleteOriginal(false),
		cloneConstraints(false),
		numberOfCopies(0),
		secondNumberOfCopies(1)
	{

	}

	DrawSketchHandlerTranslate::~DrawSketchHandlerTranslate()
	{
	}

	void DrawSketchHandlerTranslate::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj) {
			listOfGeoIds = Obj->getSelectIds();
		}
	}

	void DrawSketchHandlerTranslate::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			referencePoint = onSketchPos;
		} break;
		case SelectMode::SeekSecond: {
			firstTranslationPoint = onSketchPos;
			firstTranslationVector = Base::Vector3d(
				firstTranslationPoint.x - referencePoint.x,
				firstTranslationPoint.y - referencePoint.y,
				0.0);

			CreateAndDrawShapeGeometry();
		} break;
		case SelectMode::SeekThird: {
			secondTranslationPoint = onSketchPos;
			secondTranslationVector = Base::Vector3d(
				secondTranslationPoint.x - referencePoint.x,
				secondTranslationPoint.y - referencePoint.y,
				0.0);

			CreateAndDrawShapeGeometry();
		} break;
		default:
			break;
		}
	}

	void DrawSketchHandlerTranslate::createShape(bool onlyeditoutline)
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj == nullptr) {
			return;
		}

		// The bookkeeping of the previous call describes geometry that is about to go.
		ShapeGeometry.clear();
		mCopyElements.clear();
		mBlockCount = 0;

		// No copy asked for is the plain move: one translated copy is made and the
		// original is deleted instead. That is the meaning FreeCAD gives the number.
		int numberOfCopiesToMake = numberOfCopies;
		if (numberOfCopies == 0) {
			numberOfCopiesToMake = 1;
			deleteOriginal = true;
		}
		else {
			deleteOriginal = false;
		}

		// One block of copies per (row, column) of the array, the original's own place
		// left out: a block is the whole selection translated by one vector.
		for (int row = 0; row < secondNumberOfCopies; row++) {
			for (int column = 0; column <= numberOfCopiesToMake; column++) {
				if (row == 0 && column == 0) {
					continue;
				}
				const Base::Vector3d vec
					= firstTranslationVector * column + secondTranslationVector * row;

				for (size_t selectionIndex = 0; selectionIndex < listOfGeoIds.size(); selectionIndex++) {
					const Part::Geometry* pGeo = Obj->getGeometry(listOfGeoIds[selectionIndex]);
					if (pGeo == nullptr) {
						continue;
					}
					auto geoUniquePtr = std::unique_ptr<Part::Geometry>(pGeo->copy());

					Base::Matrix4D translation;
					translation.move(vec);
					geoUniquePtr->transform(translation);

					mCopyElements.push_back(
						CopyElement{ mBlockCount, static_cast<int>(selectionIndex) });
					ShapeGeometry.emplace_back(std::move(geoUniquePtr));
				}
				mBlockCount++;
			}
		}

		if (onlyeditoutline) {
			// The two vectors, drawn from the reference point they were measured from.
			if (firstTranslationVector.Length() > Precision::Confusion()) {
				addLineToShapeGeometry(
					Base::Vector3d(referencePoint.x, referencePoint.y, 0.0),
					Base::Vector3d(firstTranslationPoint.x, firstTranslationPoint.y, 0.0),
					true
				);
			}
			if (secondTranslationVector.Length() > Precision::Confusion()) {
				addLineToShapeGeometry(
					Base::Vector3d(referencePoint.x, referencePoint.y, 0.0),
					Base::Vector3d(secondTranslationPoint.x, secondTranslationPoint.y, 0.0),
					true
				);
			}
		}
	}

	void DrawSketchHandlerTranslate::deleteOriginalGeos()
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj) {
			Obj->deleteGeometries(listOfGeoIds);
		}
	}

	bool DrawSketchHandlerTranslate::canGoToNextMode()
	{
		if (state() == SelectMode::SeekSecond
			&& firstTranslationVector.Length() < Precision::Confusion()) {
			// A null translation is not a translation.
			return false;
		}
		if (state() == SelectMode::SeekThird
			&& secondTranslationVector.Length() < Precision::Confusion()
			&& secondNumberOfCopies > 1) {
			// The rows of an array have to be told apart.
			return false;
		}
		return true;
	}

	void DrawSketchHandlerTranslate::onButtonPressed(Base::Vector2d pos)
	{
		updateDataAndDrawToPosition(pos);
		if (state() == SelectMode::SeekSecond && secondNumberOfCopies == 1) {
			// One row: there is no second vector to ask for, the translation is done.
			setState(SelectMode::End);
		}
		else {
			moveToNextMode();
		}
	}

	void DrawSketchHandlerTranslate::executeCommands()
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (Obj == nullptr) {
			return;
		}

		// Only the copies, without the vectors drawn while the tool was running.
		createShape(false);
		if (ShapeGeometry.empty()) {
			return;
		}

		// The copies go in first, so that the constraints can name them; the originals go
		// last, because deleting them would move every id above them.
		std::vector<int> newGeoIds;
		newGeoIds.reserve(ShapeGeometry.size());
		for (auto& geo : ShapeGeometry) {
			if (!geo) {
				continue;
			}
			const bool isConstruction = geo->getConstruction();
			const int geoId = Obj->addGeometry((geo));
			if (isConstruction) {
				Obj->setConstruction(geoId, true);
			}
			newGeoIds.push_back(geoId);
		}

		addConstraintCopies(Obj, newGeoIds);

		if (deleteOriginal) {
			deleteOriginalGeos();
		}
	}

	void DrawSketchHandlerTranslate::addConstraintCopies(
		SketcherObj* p_sketch,
		const std::vector<int>& p_newGeoIds)
	{
		const int selectionSize = static_cast<int>(listOfGeoIds.size());
		if (selectionSize == 0 || mBlockCount == 0
			|| static_cast<int>(p_newGeoIds.size()) < mBlockCount * selectionSize) {
			return;
		}

		// The constraints as they are now: the sketch is about to grow, and walking a list
		// that is being added to is not something to do.
		std::vector<std::unique_ptr<Sketcher::Constraint>> originals;
		for (const Sketcher::Constraint* constraint : p_sketch->constraints()) {
			originals.emplace_back(constraint->copy());
		}

		// The geometry a constraint's element refers to is one of the selected ones, and
		// its copy in the block is the element at the same place in the block: the copies
		// were made block by block, each block in the order of the selection.
		const auto copyOf = [&](int p_block, int p_selectionIndex) {
			return p_newGeoIds[p_block * selectionSize + p_selectionIndex];
		};

		// A line whose distance is copied as an Equal takes two constraints with it
		// (distanceX and distanceY), and one Equal is enough: this remembers the copies
		// that already have one.
		std::vector<int> copiesWithEqual;

		for (const auto& original : originals) {
			const int firstIndex = indexOfGeoId(listOfGeoIds, original->First);
			const int secondIndex = indexOfGeoId(listOfGeoIds, original->Second);
			const int thirdIndex = indexOfGeoId(listOfGeoIds, original->Third);
			const Sketcher::ConstraintType type = original->Type;

			for (int block = 0; block < mBlockCount; block++) {
				auto constraint = std::unique_ptr<Sketcher::Constraint>(original->copy());

				if ((type == Sketcher::Symmetric || type == Sketcher::Tangent
						|| type == Sketcher::Perpendicular || type == Sketcher::Angle)
					&& firstIndex >= 0 && secondIndex >= 0 && thirdIndex >= 0) {
					// Three elements, all of them ours: all three point at the copies.
					constraint->First = copyOf(block, firstIndex);
					constraint->Second = copyOf(block, secondIndex);
					constraint->Third = copyOf(block, thirdIndex);
				}
				else if ((type == Sketcher::Coincident || type == Sketcher::Tangent
						|| type == Sketcher::Symmetric || type == Sketcher::Perpendicular
						|| type == Sketcher::Parallel || type == Sketcher::Equal
						|| type == Sketcher::Angle || type == Sketcher::PointOnObject
						|| type == Sketcher::Horizontal || type == Sketcher::Vertical
						|| type == Sketcher::InternalAlignment)
					&& firstIndex >= 0 && secondIndex >= 0
					&& original->Third == Sketcher::GeoEnum::GeoUndef) {
					constraint->First = copyOf(block, firstIndex);
					constraint->Second = copyOf(block, secondIndex);
				}
				else if ((type == Sketcher::Radius || type == Sketcher::Diameter
						|| type == Sketcher::Weight)
					&& firstIndex >= 0) {
					if (deleteOriginal || !cloneConstraints) {
						// The copy carries its own dimension, with the value the original
						// has.
						constraint->First = copyOf(block, firstIndex);
						constraint->setValue(original->getValue());
					}
					else {
						// Or the two are equal, and the original keeps the dimension.
						constraint->Type = Sketcher::Equal;
						constraint->First = original->First;
						constraint->Second = copyOf(block, firstIndex);
					}
				}
				else if ((type == Sketcher::Distance || type == Sketcher::DistanceX
						|| type == Sketcher::DistanceY)
					&& firstIndex >= 0 && secondIndex >= 0) {
					if (!deleteOriginal && cloneConstraints && original->First == original->Second) {
						// A distance of a line onto itself: equal is what the option asks
						// for, and one is enough for both of its directions.
						const int copyId = copyOf(block, secondIndex);
						if (indexOfGeoId(copiesWithEqual, copyId) != -1) {
							continue;
						}
						constraint->Type = Sketcher::Equal;
						constraint->First = original->First;
						constraint->Second = copyId;
						copiesWithEqual.push_back(copyId);
					}
					else {
						constraint->First = copyOf(block, firstIndex);
						constraint->Second = copyOf(block, secondIndex);
					}
				}
				else if ((type == Sketcher::Block || type == Sketcher::Horizontal
						|| type == Sketcher::Vertical)
					&& firstIndex >= 0) {
					constraint->First = copyOf(block, firstIndex);
				}
				else {
					// A constraint that reaches outside the selection (to the axes, to
					// geometry that stays put, ...): it belongs to the original only.
					continue;
				}

				p_sketch->addConstraint(std::move(constraint));
			}
		}
	}

	void DrawSketchHandlerTranslate::onKeyPress(const std::string& key)
	{
		// The keys FreeCAD's tool widget carries: copies with U/J, rows with R/F, and its
		// "apply equal constraints" checkbox with E.
		if (key == "U") {
			numberOfCopies++;
		}
		else if (key == "J") {
			numberOfCopies = std::max(numberOfCopies - 1, 0);
		}
		else if (key == "R") {
			secondNumberOfCopies++;
		}
		else if (key == "F") {
			secondNumberOfCopies = std::max(secondNumberOfCopies - 1, 1);
		}
		else if (key == "E") {
			cloneConstraints = !cloneConstraints;
		}
		else {
			return;
		}

		CORE_INFO(
			"[Sketch] translate: {0} copy(ies), {1} row(s), equal constraints {2}",
			numberOfCopies, secondNumberOfCopies, cloneConstraints ? "on" : "off");

		// The counts changed what the picture will be: redraw it, but only once there is a
		// translation to draw it at.
		if (state() != SelectMode::SeekFirst) {
			CreateAndDrawShapeGeometry();
		}
	}
}
