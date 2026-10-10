#pragma once
#include <string>

namespace tinyxml2 {
	class XMLElement;
}
namespace MOON {
	class FeatureBody;
	/** The .moon document: the feature chain of the body, written to a file and read
	 * back.
	 *
	 * What is written is what the features *are* - their type, their parameters, the
	 * features they are built from and the elements they refer to - never the shapes
	 * they produced. Reading builds the features again and recomputes the chain, so a
	 * document holds a feature chain rather than a pile of frozen geometry, and the
	 * topological references keep working across a save/load exactly as they do across
	 * a recompute (they are stored with the mapped names they resolved to, see
	 * Feature::getReferenceNames).
	 *
	 * What a feature carries of the scene - its pose, the elements it refers to and
	 * the eye that was switched off on it - goes with it, and so does the placement
	 * of the body the features hang under. No camera, no materials and no other view
	 * state is written yet.
	 */
	class MoonDocument
	{
	public:
		/** Writes the body's feature chain to p_path.
		 * @return false when the file could not be written. */
		static bool save(const std::string& p_path);

		/** Reads p_path, replacing the feature chain the body has.
		 *
		 * The features that were loaded before are dropped first, together with the
		 * actors they spawned. A file that cannot be read, or that names a feature that
		 * cannot be built, leaves the body as it was: everything is built in a first
		 * pass and the body is only taken apart once that succeeded.
		 *
		 * @return false when the document could not be opened. */
		static bool open(const std::string& p_path);

		/** Writes one body the way a document writes it: the placement of its node, its
		 * features, the links between them (indices into this body's own chain) and
		 * their poses - the features are relative to that node. */
		static void writeBody(tinyxml2::XMLElement& p_parent, FeatureBody* p_body);
		/** Copies a body into a text of that same form. A copy is kept as text so that
		 * it cannot dangle when the body it was taken from is deleted. */
		static bool copyBody(FeatureBody* p_body, std::string& p_out);
		/** Makes a body out of such a text: a deep copy - every feature is built again
		 * from what was written, and the links between them land on the copies. The new
		 * body is named after the one it came from, made unique, and becomes the active
		 * body. @return the new body, or null when the text cannot be read. */
		static FeatureBody* pasteBody(const std::string& p_text);

		/** The extension of a document, without the dot. */
		static const char* extension() { return "moon"; }
		/** The filter the file dialogs offer. */
		static const char* fileFilter() { return "Moon Document (*.moon)"; }
	};
}
