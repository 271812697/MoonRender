#pragma once
#include "feature/FeatureBaseProfile.h"
#include <gp_Ax1.hxx>
namespace MOON { 
	class RevolveFeature :public FeatureBaseProfile {
	public:
		RevolveFeature(const std::string& p_name,int addSubType);
		virtual ~RevolveFeature() override;
		virtual bool execute();
		/** The swept solid this revolve added or this groove took away. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;
		float origin[3];
		float angle = 90.0;
		int addSubType = 0;
		gp_Ax1 axis;
		int axisType = 0;
		
		bool  reverse = false;
		Part::TopoShape toolShape;
	};
}
