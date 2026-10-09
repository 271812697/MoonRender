#pragma once
#include <memory>
namespace Core::ECS {
	class Actor;
}
#include <string>
#include <vector>
namespace MOON { 
	class Feature;
	class FeatureBody  {
	public:
		FeatureBody(const std::string& p_name);
		/** Every body of the current scene, in the order they were made. */
		static const std::vector<FeatureBody*>& All();
		/** Makes a body, lists it and makes it the active one. */
		static FeatureBody* Create(const std::string& p_name);
		/** The body a feature created now joins, and the one the panels edit. Null when
		 * the scene has no body yet, in which case the caller makes one. */
		static FeatureBody* Active();
		static void SetActive(FeatureBody* p_body);
		/** The body whose scene node p_anchor is, or null: the tree uses it to make the
		 * body the user clicked the active one. */
		static FeatureBody* Of(const Core::ECS::Actor* p_anchor);
		/** A name for a new body of this scene, based on p_base: "Body", then "Body001",
		 * "Body002" ... the way FreeCAD numbers them, so a scene with several bodies has
		 * names that tell them apart. */
		static std::string UniqueName(const std::string& p_base);
		/** Takes the bodies of the scene out of the list: the loader puts the chain it is
		 * about to replace aside with this, and puts it back with Adopt() when the
		 * document cannot be read. */
		static std::vector<std::unique_ptr<FeatureBody>> TakeAll();
		static void Adopt(std::vector<std::unique_ptr<FeatureBody>> p_bodies);

		const std::string& GetName() const { return m_name; }
		/** Renames the body: the name is what the tree shows it as and what a document
		 * writes into its <Body> node, so the scene node is renamed with it. */
		void SetName(const std::string& p_name);
		/** The actor this body is shown as, and the parent its features hang under. */
		Core::ECS::Actor* GetAnchor() const { return m_anchor; }
		virtual ~FeatureBody() ;
		void addFeature(Feature* feature);
		bool removeFeature(Feature* feature);
		void populateFeature(Feature* feature);
		/** Rebuilds one feature - execute() and makeDone() - with nothing it throws
		 * getting out: the propagation runs from makeDone(), so there is no caller left
		 * to catch anything (see populateFeature). */
		void rebuildFeature(Feature* feature);
		Feature* getLastBaseFeature(Feature* feature);
		bool setBaseFeatureFor(Feature* feature);
		/** Moves a feature to another place in the chain.
		 *
		 * The order of the list *is* the chain: a 3D feature is stacked on the one
		 * before it, and a document stores the features in this order. So a move
		 * re-derives the bases from the new order and rebuilds everything from the
		 * new place on.
		 *
		 * A move that would put a feature before something it is built from - the
		 * shape below it, its profile, a pipe's path, the face a pad ends at, the
		 * instances a pattern repeats - is refused: none of those can be resolved
		 * from a feature that has not been built yet.
		 *
		 * @return true when the feature moved. */
		bool moveFeature(Feature* feature, int p_index);
		/** The features of the body, in the order they were created: that order is the
		 * chain, and it is what a document stores. */
		const std::vector<Feature*>& getFeatures() const;
		/** Puts a whole chain in place, without executing it: the caller rebuilds the
		 * features itself (see MoonDocument::open). */
		void setFeatures(const std::vector<Feature*>& p_features);
		/** Drops every feature: their actors leave the scene with them (the render
		 * anchors and topology actors they spawned go too). */
		void clear();
	private:
		std::string m_name;
		/** The scene node of this body; the features are its children. */
		Core::ECS::Actor* m_anchor = nullptr;
		class Internal;
		Internal* mInternal = nullptr;
	};
}
