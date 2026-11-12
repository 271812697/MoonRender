
#include "TopoShape.h"
#include "SketcherFeature.h"
#include "core/log.h"
#include "Sketcher/SketcherObj.h"

#include <vector>


namespace MOON {
	class SketcherFeature::Internal {
	public:
		Internal(SketcherFeature* s):self(s) {
			sketcher = std::make_shared<SketcherObj>();
			// The sketch writes its own name into the messages it logs.
			sketcher->setName(s != nullptr ? s->GetName() : std::string("SketcherObj"));
		}
		~Internal() {
		}
	private:
		friend SketcherFeature;
		SketcherFeature* self = nullptr;
		std::shared_ptr<SketcherObj> sketcher;
	};
    SketcherFeature::SketcherFeature(const std::string& p_name) :ProfileFeature(p_name, "Sketcher"),mInternal(new Internal(this))
	{
	}
	SketcherObj* SketcherFeature::getSketcherObj()
	{
		return mInternal->sketcher.get();
	}
    SketcherFeature::~SketcherFeature()
	{
		delete mInternal;
	}
	bool SketcherFeature::execute()
	{
		if (!mInternal->sketcher.get()) {
			return false;
		}
		// The faces, with the wires inside them drilled out as holes, are the sketch object's:
		// that is where the face maker runs (see SketcherObj::makeDone). What the faces do not
		// cover - the curves that enclose no region - is the rest of what the user drew, and
		// goes into the same shape beside them: the sketch is one thing on screen and in the
		// tree, and a reference to any of its curves resolves against the same shape.
		std::vector<Part::TopoShape> parts;
		const Part::TopoShape faces = mInternal->sketcher->getDoneFaceShape();
		if (!faces.isNull()) {
			for (Part::TopoShape& face : faces.getSubTopoShapes(TopAbs_FACE)) {
				parts.push_back(face);
			}
		}
		for (const Part::TopoShape& freeWire : mInternal->sketcher->getDoneFreeWires()) {
			parts.push_back(freeWire);
		}

		Part::TopoShape shape;
		if (!parts.empty()) {
			// One part is the shape itself: a sketch of a single closed outline is its face,
			// which is what the features above want to be handed.
			shape.makeElementCompound(
				parts,
				nullptr,
				Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
		}
		setResultShape(shape);
		CORE_INFO("Make a SketcherFeature");
		return true;
	}
}
