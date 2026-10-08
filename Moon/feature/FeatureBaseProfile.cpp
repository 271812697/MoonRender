
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
		// No sketch: the profile is then a face or an edge picked on the shape below
		// (that is what a pad built on the body is), which the feature stores as a
		// sub-shape reference. The lookup belongs here, and a lookup that finds
		// something is not a problem to report - only one that comes back empty is.
		Part::TopoShape subShape = getBaseTopoFaceShape();
		if (subShape.isNull()) {
			CORE_ERROR(
				"{0}: it has no profile: neither a sketch nor a sub-shape that can be "
				"resolved on the shape below",
				GetName());
			return subShape;
		}
		// A vertex is a point, so there is nothing to sweep: it can be *referenced*
		// (that is what the reference is kept for) but it is not a profile, and
		// saying so beats handing the face maker a point and letting it fail.
		if (subShape.getShape().ShapeType() == TopAbs_VERTEX) {
			CORE_ERROR(
				"{0}: the profile is a vertex; pick a face or a closed edge of the "
				"shape below",
				GetName());
			return Part::TopoShape();
		}
		CORE_INFO(
			"{0}: the profile is the sub-shape '{1}' of the shape below",
			GetName(),
			getSubValues().empty() ? std::string("<none>") : getSubValues().front());
		return subShape;
	}
}
