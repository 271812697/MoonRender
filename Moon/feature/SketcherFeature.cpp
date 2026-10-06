
#include "TopoShape.h"
#include "SketcherFeature.h"
#include "core/log.h"
#include "Sketcher/SketcherObj.h"


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
		if (mInternal->sketcher.get()) {
        setResultShape(mInternal->sketcher->getDoneWireShape());
			CORE_INFO("Make a SketcherFeature");
			return true;
	    }
        return false;
	}
}
