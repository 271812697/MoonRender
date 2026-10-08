#pragma once
#include <string>
#include <vector>
namespace MOON { 
	class Feature;
	class FeatureBody  {
	public:
		FeatureBody(const std::string& p_name);
		static FeatureBody& instance();
		virtual ~FeatureBody() ;
		void addFeature(Feature* feature);
		bool removeFeature(Feature* feature);
		void populateFeature(Feature* feature);
		Feature* getLastBaseFeature(Feature* feature);
		bool setBaseFeatureFor(Feature* feature);
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
		class Internal;
		Internal* mInternal = nullptr;
	};
}
