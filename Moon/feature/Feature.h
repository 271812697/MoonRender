#pragma once
#include "core/component/TopoShapeActor.h"
namespace MOON { 
	class Feature :public TopoActor {
	public:
		Feature(const std::string& p_name,const std::string& tag);
		virtual ~Feature() override;
		virtual bool execute();
		void setBaseFeature(Feature* f) {
			m_baseFeature = f;
		}
		void setSubValues(const std::vector<std::string>& values) {
			subValues = values;
		}
		Feature* getBaseFeature() { return m_baseFeature; }
		Part::TopoShape getBaseTopoShape();
		Part::TopoShape getBaseTopoFaceShape();
		std::vector<Part::TopoShape> getBaseTopoFaceShapes();
		Part::TopoShape getBaseTopoEdgeShape();
		std::vector<Part::TopoShape> getBaseTopoEdgeShapes();
	    Part::TopoShape& getPreviewShape();
		void makeDone();
	protected:
		/** Resolves one entry of subValues against the *current* shape of the base
		 * feature.
		 *
		 * The entry stored by the task panels is "<Type>_<index>" ("Edge_3"), a
		 * position in the enumeration of the base shape - and a recompute is free
		 * to change that order, which is what used to break a reference. The first
		 * time a reference is resolved its mapped name is remembered and used from
		 * then on, with the index kept as the fallback.
		 */
		Part::TopoShape resolveBaseSubShape(int p_index);
		Feature* m_baseFeature = nullptr;
		std::vector<std::string> subValues;
		/** Every mapped name each entry of subValues is known by, filled in on the
		 * first use. One element can carry several names (the one it got from its
		 * creator plus the ones later operations added), and which of them survives
		 * a recompute depends on the operations downstream: a letter box that had a
		 * single name while the profile had one curve only exposes the tagged one
		 * once the profile grows a second curve. Remembering all of them keeps the
		 * reference resolvable either way. */
		std::vector<std::vector<std::string>> m_referenceNames;
	private:
		bool hasInTree = false;
		class Internal;
		Internal* mInternal = nullptr;
	};
	class Feature3D :public Feature {
	public:
		Feature3D(const std::string& p_name, const std::string& tag)
			:Feature(p_name, tag)
		{

		}
	};
	class DatumFeature :public Feature
	{
	public:
		DatumFeature(const std::string& p_name, const std::string& tag)
			:Feature(p_name, tag)
		{

		}
	};
	class ProfileFeature :public Feature
	{
	public:
		ProfileFeature(const std::string& p_name, const std::string& tag)
			:Feature(p_name,tag)
		{

		}
	};
}
