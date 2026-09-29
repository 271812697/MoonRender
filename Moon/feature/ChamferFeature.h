#pragma once
#include "feature/Feature.h"
namespace MOON {
	class ChamferFeature : public Feature3D {
	public:
		ChamferFeature(const std::string& p_name);
		virtual ~ChamferFeature() override;
		virtual bool execute();
		/** The material the chamfer removed from, or added to, the shape below. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;

		// Part::ChamferType: 0 = equalDistance, 1 = twoDistances, 2 = distanceAngle
		int chamferType = 0;
		float size = 0.5f;      // primary chamfer size
		float size2 = 0.5f;     // second size (two distances)
		float angle = 45.0f;    // angle in degrees (distance and angle)
		bool flipDirection = false;
		bool useAllEdges = false;

		float origin1[3];
		float origin2[3];
		float dir1[3];
		float dir2[3];
		float len = 0;
		Part::TopoShape toolShape;
		bool toolSubtractive = true;
	};
}
