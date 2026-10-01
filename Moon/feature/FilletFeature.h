#pragma once
#include "feature/Feature.h"
namespace MOON { 
	class FilletFeature :public Feature3D {
	public:
		FilletFeature(const std::string& p_name);
		virtual ~FilletFeature() override;
		virtual bool execute();
		/** The material the fillet removed from, or added to, the shape below: a
		 * convex edge loses a sliver of material, a concave one gains it. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;
		float radius = 0.5;
		bool useAllEdges = false;
		float origin1[3];
		float origin2[3];
		float dir1[3];
		float dir2[3];
		float len=0;
		Part::TopoShape toolShape;
		bool toolSubtractive = true;
	};
}
