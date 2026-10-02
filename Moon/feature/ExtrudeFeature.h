#pragma once
#include "feature/FeatureBaseProfile.h"
#include <gp_Vec.hxx>
namespace MOON { 
	class SketcherFeature;
	class ExtrudeFeature :public FeatureBaseProfile {
	public:
		ExtrudeFeature(const std::string& p_name,int addsubType);
		virtual ~ExtrudeFeature() override;
		virtual bool execute();
		/** The prism this pad added or this pocket took away. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;
		SketcherFeature* sketcher = nullptr;
        float lengthForward =10 ;
        double angleForward = 0;
        double lengthRev = 10;
        double angleRev = 0;
        int dirType = 0;       // 0=正向,1=反向,2=双向,3=对称
		int extrudeType = 0;   //0 =length,1=through all,2=uptoface
		int addSubType = 0;//0=Add,1=sub
		gp_Vec finalDir;
		Part::TopoShape upToFace;
		/** The face a "up to face" pad or pocket extrudes to, kept the way the
		 * other references are: the feature it was picked on plus the name of the
		 * element inside it ("Face_3"). The shape below moves its elements around
		 * whenever it is recomputed, and a reference is what follows them - a copy
		 * of the face would keep pointing into the shape as it was on the day it
		 * was picked. A document carries the reference for the same reason. */
		Feature* upToFaceFeature = nullptr;
		std::string upToFaceRef;
		std::vector<std::string> upToFaceNames;
		/** Records a picked face and resolves it once: the preview can then be
		 * built right away, and the mapped names of the face are known, which is
		 * what makes the reference survive a recompute and a save. */
		void setUpToFaceReference(
			const Part::TopoShape& p_picked,
			Feature* p_feature,
			const std::string& p_reference);
		Part::TopoShape supportShape;
		Part::TopoShape toolShape;
	};
}
