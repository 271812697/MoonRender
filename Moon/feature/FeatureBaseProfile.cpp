
#include "FeatureBaseProfile.h"
#include "SketcherFeature.h"
#include "Sketcher/SketcherObj.h"
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
			Feature::applyWorldTransform(*mProfile, face);
			return face;
		}
		return getBaseTopoFaceShape();
	}
}
