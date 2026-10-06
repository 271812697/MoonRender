#pragma once
#include <vector>
namespace MOON {
	class SketcherFeature;
	class SketcherObj;
	class SketcherObjWidget;
	class SketcherObjManager {
	public:
		static SketcherObjManager& instance();
		~SketcherObjManager();
		SketcherFeature* CreateSketcherFeature();
		SketcherFeature* GetCurrentActiveSketcherFeature();
		SketcherFeature* GetLastSketcherFeature();
		SketcherObj* GetCurrentActiveSketcherObj();
		/** The editing widget of the sketch that is open: it is the one that holds
		 * the selection, picks and draws, so panels and tools that ask "what is
		 * picked" ask it, not the sketch data. Null when no sketch is being edited. */
		SketcherObjWidget* GetCurrentActiveSketcherWidget();
		void setCurrentActiveSketcherFeature(SketcherFeature* obj);
		/** Registers a sketch feature this manager does not know yet: a document hands
		 * over the sketches it read, so that the drawing tools (which ask this manager
		 * for the current sketch) find them. Does nothing for one it already has. */
		void addSketcherFeature(SketcherFeature* p_feature);
		/** Forgets a sketch feature that is about to be deleted. */
		void removeSketcherFeature(SketcherFeature* p_feature);
		std::vector<SketcherObj*> GetAllSketcherObjs();
	private:
		class SketcherObjManagerInternal;
		SketcherObjManagerInternal* mInternal = nullptr;
		SketcherObjManager();
		SketcherObj* CreateSketcherObj();
	};
}
