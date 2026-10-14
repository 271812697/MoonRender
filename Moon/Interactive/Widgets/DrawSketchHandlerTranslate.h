#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/Widgets/DrawSketchDefaultHandler.h"
namespace MOON
{
	/** Moves or arrays the selected sketch geometry - FreeCAD's sketcher translate tool
	 * (DrawSketchHandlerTranslate), ported to this sketcher.
	 *
	 * Three clicks, like the other transformation tools:
	 *   - the reference point the translation is measured from;
	 *   - the translation vector. "U"/"J" add or remove copies: with none the selection
	 *     is simply moved, with N it is the original plus N copies along that vector;
	 *   - the second vector, which is what turns those copies into rows ("R"/"F" add or
	 *     remove rows). It is only asked for when there is more than one row.
	 *
	 * The constraints that only involve the selected geometry are carried over to every
	 * copy, the way FreeCAD does it: dimensional ones keep their value (so each copy is
	 * dimensioned like the original), unless "E" (= FreeCAD's "Apply equal constraints"
	 * checkbox) is on, in which case they become Equal constraints between the original
	 * and its copies instead.
	 */
	class DrawSketchHandlerTranslate : public DrawSketchDefaultHandler<DrawSketchHandlerTranslate, StateMachines::ThreeSeekEnd, 0>
	{
		using SupperClass = DrawSketchDefaultHandler<DrawSketchHandlerTranslate, StateMachines::ThreeSeekEnd, 0>;
	public:
		DrawSketchHandlerTranslate(const std::string& name);
		virtual ~DrawSketchHandlerTranslate();
		virtual void onUpdate()override;
		virtual void updateDataAndDrawToPosition(Base::Vector2d onSketchPos)override;
		void createShape(bool onlyeditoutline) override;
		void deleteOriginalGeos();
		bool canGoToNextMode() override;
		virtual void onButtonPressed(Base::Vector2d pos) override;
		void executeCommands() override;
		virtual void onKeyPress(const std::string& key)override;
	private:
		/** Where one element of the ShapeGeometry built by createShape() came from: which
		 * block of copies it belongs to (a block is one translation of the whole
		 * selection) and which of the selected geometries it is a copy of. The cloned
		 * constraints are given the ids of the copies through both. */
		struct CopyElement
		{
			int block = 0;
			int selectionIndex = 0;
		};
		/** Adds, to the sketch, a copy of every constraint that lies inside the selection
		 * - once per block of copies - naming the copies the ids they got in
		 * p_newGeoIds. */
		void addConstraintCopies(SketcherObj* p_sketch, const std::vector<int>& p_newGeoIds);

		std::vector<int> listOfGeoIds;
		Base::Vector2d referencePoint, firstTranslationPoint, secondTranslationPoint;
		Base::Vector3d firstTranslationVector, secondTranslationVector;

		bool deleteOriginal, cloneConstraints;
		int numberOfCopies, secondNumberOfCopies;

		std::vector<CopyElement> mCopyElements;
		int mBlockCount = 0;
	};
}
