#pragma once
#include <Eigen/Core>
#include "Sketcher/Datatypes/GeoEnum.h"

namespace MOON {
	/** One element of a sketch selection: a curve (pointPos == none) or one of the
	 * markers that stand on it (start / end / mid, i.e. a centre).
	 *
	 * This lives in its own header because both halves of the sketcher name it: the
	 * sketch data (SketcherObj) and the widget that picks and draws it
	 * (SketcherObjWidget). Neither owns the other's header, so the vocabulary they
	 * share - this type, the element ids, the constraint positions - sits here. */
	struct SelectGeoId
	{
		int GeoId = Sketcher::GeoEnum::GeoUndef;
		Sketcher::PointPos pointPos = Sketcher::PointPos::none;
	};

	/** "No geometry" for the selection state. The negative solver ids are taken by
	 * the external geometry, so an unused element is GeoUndef - the same value the
	 * solver uses for an element that is not set. */
	constexpr int NoGeoId = Sketcher::GeoEnum::GeoUndef;

	/** Viewport drawing options for sketch geometry and constraint annotations.
	 * Colours are stored in ABGR byte order, matching the renderer's
	 * Eigen::Vector4<uint8_t> convention.
	 *
	 * The widget owns the instance (and draws with it); it sits here because the
	 * sketch data hands it out, so the panels can read the colours of the sketch
	 * they edit without knowing the widget. */
	struct DrawOption
	{
		Eigen::Vector4<uint8_t> pointColor { 255, 0, 0, 255 };
		Eigen::Vector4<uint8_t> preselectColor { 255, 0, 255, 255 };
		Eigen::Vector4<uint8_t> selectColor { 255, 255, 255, 0 };
		Eigen::Vector4<uint8_t> constraintColor { 255, 255, 47, 186 };
		Eigen::Vector4<uint8_t> curveColor { 255, 255, 134, 120 };
		Eigen::Vector4<uint8_t> constructionColor { 255, 255, 107, 142 };
		/** Geometry projected in from another feature: same idea as construction
		 * geometry (reference only, never part of the shape the sketch produces),
		 * with its own colour so it cannot be mistaken for something drawn here. */
		Eigen::Vector4<uint8_t> externalColor { 255, 100, 0, 255 };
		/** The axes of the sketch plane, each in the colour CAD packages draw it
		 * in: the horizontal one (the x axis) red, the vertical one (the y axis)
		 * green. The origin is the start of the horizontal axis and keeps the
		 * external colour, so the two lines stay apart from the point they meet
		 * at. */
		Eigen::Vector4<uint8_t> xAxisColor { 255, 0, 0, 255 };
		Eigen::Vector4<uint8_t> yAxisColor { 255, 0, 255, 0 };
		float curveLineWidth = 4.0f;
		/** The axes are a backdrop, but they are what a sketch is laid out against,
		 * so they are drawn a little heavier than the curves on top of them. */
		float axisLineWidth = 3.0f;
		float pointSize = 10.0f;
	};
}
