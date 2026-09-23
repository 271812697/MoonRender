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
		/** The shape another feature should model with: this feature's stored topology
		 * with its own transform applied.
		 *
		 * The stored shape already carries the placement of the operation - a sketch
		 * carries the placement of its plane, for instance - while the transform of the
		 * actor is the pose the feature was given from the outside (the property panel,
		 * the primitive dragger, ...). Drawing applies that pose through the actor
		 * matrices, so modeling has to apply it here too: otherwise moving a feature
		 * would move only what is drawn, and everything built on top of it would stay
		 * where it was.
		 *
		 * Every consumer of a feature goes through this - getBaseTopoShape(),
		 * resolveBaseSubShape() and ResolveSubShapeRef() - so a reference always lands on
		 * the shape as it is seen, whatever kind of feature it points at. */
		Part::TopoShape getWorldTopoShape();
		/** The same, for the consumers that hold a shape of their own rather than the
		 * actor's: a sketch keeps the face it produced on the sketch object, not on the
		 * feature, and the profile of a pad is taken from there. Does nothing while the
		 * feature sits at the origin with no scale. */
		static void applyWorldTransform(Feature& p_feature, Part::TopoShape& p_shape);
		Part::TopoShape getBaseTopoShape();
		Part::TopoShape getBaseTopoFaceShape();
		std::vector<Part::TopoShape> getBaseTopoFaceShapes();
		Part::TopoShape getBaseTopoEdgeShape();
		std::vector<Part::TopoShape> getBaseTopoEdgeShapes();
	    Part::TopoShape& getPreviewShape();
		/** Hands the shape this feature produced to the actor.
		 *
		 * The mapped names of that shape are what a feature above resolves its
		 * references against. A shape that arrives without them - the raw shape
		 * overload of setShape() resets the element map - turns every reference to
		 * this feature into a position in its enumeration, which the next recompute
		 * is free to move. The warning logged here is what makes that visible instead
		 * of leaving it to be discovered as a reference that points at the wrong
		 * element months later. */
		void setResultShape(Part::TopoShape p_shape);
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
