#pragma once
#include <gp_Dir.hxx>
#include <string>
namespace Part {
	class TopoShape;
}
namespace MOON {
	class Feature;
	class ShapeHelper {
	public:
		ShapeHelper(Feature* feature);
		virtual ~ShapeHelper();
		
		void previewShape();
		void generateFinalShape();
		void clearPreviewShape();
		/** Shows the shape this feature is built on in place of the feature itself
		 * while its panel is open.
		 *
		 * The shape of a feature already contains the shape below - an operation
		 * carries its faces and edges over, names included - so a face picked for
		 * "up to face" or an edge picked for a fillet would be taken from the
		 * feature's own result and end up referencing the feature it is being built
		 * for. Rolling the tip back to the shape below leaves only what such a
		 * reference can be taken from on screen, which is what FreeCAD does while a
		 * feature is edited. Nothing happens for a feature that was never built. */
		void rollBackToBase();
		/** Puts the visibility back the way it was before rollBackToBase(). */
		void restoreFeature();
		void setFeature(Feature* feature);
		Feature* getFeature();
		void setFeatureSubValues(const std::vector<std::string>& subValues);
		//void 
		Part::TopoShape& getPreviewShape();
		
	protected:
		void onSelectAny();
		virtual void onSelectEdge(const std::vector<Part::TopoShape>& edge);
		virtual void onSelectFace(const std::vector<Part::TopoShape>& face);
		void setGenerateShapeName(const char* name);
		struct PreviewOption {
			bool isTransparent = true;
			float r=1.0f, g=0.0f, b=1.0f, a = 0.4f;
			bool isBlend = true;
			bool useDomainColor = true;
		};
		PreviewOption mPreviewOption;
		/// Find a valid face to extrude up to
		static void getUpToFace(
			Part::TopoShape& upToFace,
			const Part::TopoShape& support,
			const Part::TopoShape& sketchshape,
			const std::string& method,
			gp_Dir& dir
		);
	private:
		class Internal;
		Internal* mInternal = nullptr;
	};
}
