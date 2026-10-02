
#include "FeatureBaseProfile.h"
#include "SketcherFeature.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"
namespace MOON {
	
	FeatureBaseProfile::FeatureBaseProfile(const std::string& p_name, const std::string& tag) :Feature3D(p_name,tag)
	{

	}
	FeatureBaseProfile::~FeatureBaseProfile()
	{
		
	}
	bool FeatureBaseProfile::execute()
	{
		return false;
	}
	Part::TopoShape FeatureBaseProfile::getProfileFace()
	{
		if (mProfile) {
			// The sketch keeps the face it produced - carrying the placement of its plane
			// - on the sketch object, so this is one of the consumers that cannot go
			// through Feature::getWorldTopoShape(). The pose of the sketch feature has to
			// be applied here instead, otherwise moving a sketch would leave the pad made
			// from it behind.
			Part::TopoShape face = mProfile->getSketcherObj()->getDoneFaceShape();
			if (face.isNull()) {
				// Without this the prism below only reports a null input, which says
				// nothing about which sketch it came from.
				CORE_ERROR(
					"{0}: the profile sketch '{1}' has no face to build on",
					GetName(),
					mProfile->GetName());
			}
			Feature::applyWorldTransform(*mProfile, face);
			return face;
		}
		CORE_ERROR("{0}: it has no profile sketch and no shape below it", GetName());
		return getBaseTopoFaceShape();
	}
}
