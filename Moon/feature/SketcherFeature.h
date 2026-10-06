#pragma once
#include "feature/Feature.h"
namespace MOON { 

	class SketcherObj;
	class SketcherObjWidget;
	class SketcherFeature :public ProfileFeature {
	public:
		SketcherFeature(const std::string& p_name);
		SketcherObj* getSketcherObj();
		/** The editing face of the sketch (the widget that draws and picks it), or
		 * null when it has never been opened (a sketch read from a file has none:
		 * nothing draws it until it is edited). */
		SketcherObjWidget* getSketcherWidget();
		/** The same, creating the widget when there is none yet: opening the sketch
		 * for editing goes through here. */
		SketcherObjWidget* ensureSketcherWidget();
		virtual ~SketcherFeature() override;
		virtual bool execute();
		/** A sketch hands on a wire: there is nothing to merge, and the names of its
		 * elements are what the features above reference, so it is left alone. */
		virtual bool isRefineActive() const override { return false; }
	private:
		class Internal;
		Internal* mInternal = nullptr;
	};
}
