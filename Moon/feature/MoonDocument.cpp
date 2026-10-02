#include "feature/MoonDocument.h"

#include <tinyxml2.h>

#include <cstdio>
#include <memory>
#include <sstream>
#include <vector>
#include <filesystem>

#include "Geometry.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/component/TopoShapeActor.h"
#include "core/log.h"
#include "feature/ChamferFeature.h"
#include "feature/DatumLineFeature.h"
#include "feature/ExtrudeFeature.h"
#include "feature/FeatureBaseProfile.h"
#include "feature/FeatureBody.h"
#include "feature/FilletFeature.h"
#include "feature/LinearPatternFeature.h"
#include "feature/MirrorFeature.h"
#include "feature/PolarPatternFeature.h"
#include "feature/RevolveFeature.h"
#include "feature/SketcherFeature.h"
#include "feature/ThicknessFeature.h"

namespace MOON
{
	namespace
	{
		constexpr int kVersion = 1;
		constexpr const char* kRoot = "MoonDocument";
		constexpr const char* kBody = "Body";
		constexpr const char* kFeature = "Feature";
		constexpr const char* kPose = "Pose";
		constexpr const char* kReferences = "References";
		constexpr const char* kSub = "Sub";
		constexpr const char* kName = "Name";
		constexpr const char* kSketch = "Sketch";
		constexpr const char* kGeometry = "Geometry";
		constexpr const char* kConstraint = "Constraint";
		constexpr const char* kParameters = "Parameters";
		constexpr const char* kUpToFace = "UpToFace";
		constexpr const char* kHidden = "Hidden";
		constexpr const char* kActor = "Actor";

		/** Opens a document for reading or writing.
		 *
		 * The path arrives as UTF-8, which is what Qt hands over, and the wide API is
		 * what understands it on Windows: fopen() would read the bytes as the local code
		 * page and fail on any path with a character outside it. */
		FILE* openStream(const std::string& p_path, bool p_write)
		{
#ifdef _WIN32
			return _wfopen(
				std::filesystem::u8path(p_path).c_str(),
				p_write ? L"wb" : L"rb");
#else
			return std::fopen(p_path.c_str(), p_write ? "wb" : "rb");
#endif
		}

		// ------------------------------------------------------------- small values
		/** A number written so that reading it back gives the same double: a fixed
		 * number of decimals would round a sketch coordinate away. */
		std::string num(double p_value)
		{
			char buffer[48] = { 0 };
			std::snprintf(buffer, sizeof(buffer), "%.17g", p_value);
			return std::string(buffer);
		}

		std::string join(const std::vector<double>& p_values)
		{
			std::string text;
			for (const double value : p_values) {
				if (!text.empty()) {
					text += ' ';
				}
				text += num(value);
			}
			return text;
		}

		std::vector<double> split(const char* p_text)
		{
			std::vector<double> values;
			if (p_text == nullptr) {
				return values;
			}
			std::istringstream stream(p_text);
			double value = 0.0;
			while (stream >> value) {
				values.push_back(value);
			}
			return values;
		}

		void setNumber(tinyxml2::XMLElement& p_node, const char* p_name, double p_value)
		{
			p_node.SetAttribute(p_name, num(p_value).c_str());
		}

		void setText(tinyxml2::XMLElement& p_node, const char* p_name, const std::string& p_text)
		{
			p_node.SetAttribute(p_name, p_text.c_str());
		}

		void setValues(
			tinyxml2::XMLElement& p_node,
			const char* p_name,
			const std::vector<double>& p_values
		)
		{
			setText(p_node, p_name, join(p_values));
		}

		double getNumber(
			const tinyxml2::XMLElement& p_node,
			const char* p_name,
			double p_default = 0.0
		)
		{
			return p_node.DoubleAttribute(p_name, p_default);
		}

		int getInt(const tinyxml2::XMLElement& p_node, const char* p_name, int p_default = 0)
		{
			return p_node.IntAttribute(p_name, p_default);
		}

		bool getFlag(const tinyxml2::XMLElement& p_node, const char* p_name, bool p_default = false)
		{
			return p_node.IntAttribute(p_name, p_default ? 1 : 0) != 0;
		}

		std::string getText(
			const tinyxml2::XMLElement& p_node,
			const char* p_name,
			const char* p_default = ""
		)
		{
			const char* value = p_node.Attribute(p_name);
			return value != nullptr ? std::string(value) : std::string(p_default);
		}

		std::vector<double> getValues(const tinyxml2::XMLElement& p_node, const char* p_name)
		{
			return split(p_node.Attribute(p_name));
		}

		Base::Vector3d toPoint(const std::vector<double>& p_values, size_t p_offset = 0)
		{
			if (p_values.size() < p_offset + 3) {
				return Base::Vector3d();
			}
			return Base::Vector3d(
				p_values[p_offset],
				p_values[p_offset + 1],
				p_values[p_offset + 2]);
		}

		Base::Vector3d getPoint(const tinyxml2::XMLElement& p_node, const char* p_name)
		{
			return toPoint(getValues(p_node, p_name));
		}

		std::string components(const Base::Vector3d& p_point)
		{
			return join({ p_point.x, p_point.y, p_point.z });
		}

		std::vector<double> fromPoint(const Base::Vector3d& p_point)
		{
			return { p_point.x, p_point.y, p_point.z };
		}

		/** Reads three numbers into the fixed array the features carry their picked
		 * geometry in. A node that does not have them leaves the array alone. */
		void readVector3(
			const tinyxml2::XMLElement& p_node,
			const char* p_name,
			float p_out[3]
		)
		{
			const std::vector<double> values = getValues(p_node, p_name);
			if (values.size() < 3) {
				return;
			}
			p_out[0] = static_cast<float>(values[0]);
			p_out[1] = static_cast<float>(values[1]);
			p_out[2] = static_cast<float>(values[2]);
		}

		int indexOf(const std::vector<Feature*>& p_features, const Feature* p_feature)
		{
			if (p_feature == nullptr) {
				return -1;
			}
			for (int i = 0; i < static_cast<int>(p_features.size()); ++i) {
				if (p_features[i] == p_feature) {
					return i;
				}
			}
			return -1;
		}

		Feature* at(const std::vector<Feature*>& p_features, int p_index)
		{
			if (p_index < 0 || p_index >= static_cast<int>(p_features.size())) {
				return nullptr;
			}
			return p_features[p_index];
		}

		std::vector<int> indicesOf(
			const std::vector<Feature*>& p_features,
			const std::vector<Feature*>& p_targets
		)
		{
			std::vector<int> indices;
			for (const Feature* feature : p_targets) {
				const int index = indexOf(p_features, feature);
				if (index >= 0) {
					indices.push_back(index);
				}
			}
			return indices;
		}

		std::vector<int> getIndices(const tinyxml2::XMLElement& p_node, const char* p_name)
		{
			std::vector<int> indices;
			for (const double value : getValues(p_node, p_name)) {
				indices.push_back(static_cast<int>(value));
			}
			return indices;
		}

		void setIndices(
			tinyxml2::XMLElement& p_node,
			const char* p_name,
			const std::vector<int>& p_indices
		)
		{
			std::vector<double> values;
			values.reserve(p_indices.size());
			for (const int index : p_indices) {
				values.push_back(index);
			}
			setValues(p_node, p_name, values);
		}

		std::vector<double> poleValues(const std::vector<Base::Vector3d>& p_poles)
		{
			std::vector<double> values;
			values.reserve(p_poles.size() * 3);
			for (const Base::Vector3d& pole : p_poles) {
				values.push_back(pole.x);
				values.push_back(pole.y);
				values.push_back(pole.z);
			}
			return values;
		}

		std::vector<Base::Vector3d> toPoles(const std::vector<double>& p_values)
		{
			std::vector<Base::Vector3d> poles;
			for (size_t i = 0; i + 2 < p_values.size(); i += 3) {
				poles.push_back(toPoint(p_values, i));
			}
			return poles;
		}

		// --------------------------------------------------------------- visibility
		/** Every actor hanging under p_actor that was switched off on its own.
		 *
		 * The tree view's eye is SetActive(), and the actors below a feature - the two
		 * render anchors and the topology ones (Solid_*, Shell_*, Face_*, Edge_*) - are
		 * built again by the recompute. Their names come from the topology itself, so
		 * they are the same names on the next load: that is what makes it possible to
		 * switch them off again. */
		void collectHiddenActors(Core::ECS::Actor& p_actor, std::vector<std::string>& p_names)
		{
			for (Core::ECS::Actor* child : p_actor.GetChildren()) {
				if (child == nullptr) {
					continue;
				}
				if (!child->IsSelfActive()) {
					p_names.push_back(child->GetName());
				}
				collectHiddenActors(*child, p_names);
			}
		}

		/** The actor called p_name below p_actor, or null. */
		Core::ECS::Actor* findActor(Core::ECS::Actor& p_actor, const std::string& p_name)
		{
			for (Core::ECS::Actor* child : p_actor.GetChildren()) {
				if (child == nullptr) {
					continue;
				}
				if (child->GetName() == p_name) {
					return child;
				}
				if (Core::ECS::Actor* found = findActor(*child, p_name)) {
					return found;
				}
			}
			return nullptr;
		}

		// ----------------------------------------------------------------- geometry
		const char* geometryTypeName(const Part::Geometry* p_geo)
		{
			if (p_geo == nullptr) {
				return nullptr;
			}
			if (p_geo->is<Part::GeomPoint>()) {
				return "GeomPoint";
			}
			if (p_geo->is<Part::GeomLineSegment>()) {
				return "GeomLineSegment";
			}
			if (p_geo->is<Part::GeomCircle>()) {
				return "GeomCircle";
			}
			if (p_geo->is<Part::GeomArcOfCircle>()) {
				return "GeomArcOfCircle";
			}
			if (p_geo->is<Part::GeomEllipse>()) {
				return "GeomEllipse";
			}
			if (p_geo->is<Part::GeomArcOfEllipse>()) {
				return "GeomArcOfEllipse";
			}
			if (p_geo->is<Part::GeomBSplineCurve>()) {
				return "GeomBSplineCurve";
			}
			if (p_geo->is<Part::GeomBezierCurve>()) {
				return "GeomBezierCurve";
			}
			return nullptr;
		}

		void writeGeometry(
			tinyxml2::XMLElement& p_parent,
			const Part::Geometry* p_geo,
			bool p_visible,
			bool p_external = false
		)
		{
			const char* type = geometryTypeName(p_geo);
			if (type == nullptr) {
				CORE_WARN(
					"[MoonDocument] a {0} curve has no writer yet; it is left out of the file",
					p_geo != nullptr ? p_geo->getTypeId().getName() : "null");
				return;
			}
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kGeometry);
			node->SetAttribute("type", type);
			node->SetAttribute("construction", p_geo->getConstruction() ? 1 : 0);
			node->SetAttribute("visible", p_visible ? 1 : 0);
			if (p_external) {
				// A reference the external geometry tool brought in: the sketch keeps it
				// as a plain curve next to its own, and the constraints that name it count
				// their ids from the end of that block - so the order here is what makes
				// them land on the same curves after a load.
				node->SetAttribute("external", 1);
			}
			if (p_geo->is<Part::GeomPoint>()) {
				const auto* point = static_cast<const Part::GeomPoint*>(p_geo);
				setText(*node, "p", components(point->getPoint()));
			}
			else if (p_geo->is<Part::GeomLineSegment>()) {
				const auto* line = static_cast<const Part::GeomLineSegment*>(p_geo);
				setText(*node, "p1", components(line->getStartPoint()));
				setText(*node, "p2", components(line->getEndPoint()));
			}
			else if (p_geo->is<Part::GeomCircle>()) {
				const auto* circle = static_cast<const Part::GeomCircle*>(p_geo);
				setText(*node, "center", components(circle->getCenter()));
				setNumber(*node, "radius", circle->getRadius());
			}
			else if (p_geo->is<Part::GeomArcOfCircle>()) {
				const auto* arc = static_cast<const Part::GeomArcOfCircle*>(p_geo);
				double first = 0.0;
				double last = 0.0;
				arc->getRange(first, last, /*emulateCCWXY=*/true);
				setText(*node, "center", components(arc->getCenter()));
				setNumber(*node, "radius", arc->getRadius());
				setNumber(*node, "first", first);
				setNumber(*node, "last", last);
			}
			else if (p_geo->is<Part::GeomEllipse>()) {
				const auto* ellipse = static_cast<const Part::GeomEllipse*>(p_geo);
				setText(*node, "center", components(ellipse->getCenter()));
				setNumber(*node, "major", ellipse->getMajorRadius());
				setNumber(*node, "minor", ellipse->getMinorRadius());
				setText(*node, "majorDir", components(ellipse->getMajorAxisDir()));
			}
			else if (p_geo->is<Part::GeomArcOfEllipse>()) {
				const auto* arc = static_cast<const Part::GeomArcOfEllipse*>(p_geo);
				double first = 0.0;
				double last = 0.0;
				arc->getRange(first, last, /*emulateCCWXY=*/true);
				setText(*node, "center", components(arc->getCenter()));
				setNumber(*node, "major", arc->getMajorRadius());
				setNumber(*node, "minor", arc->getMinorRadius());
				setText(*node, "majorDir", components(arc->getMajorAxisDir()));
				setNumber(*node, "first", first);
				setNumber(*node, "last", last);
			}
			else if (p_geo->is<Part::GeomBSplineCurve>()) {
				const auto* spline = static_cast<const Part::GeomBSplineCurve*>(p_geo);
				std::vector<double> multiplicities;
				for (const int value : spline->getMultiplicities()) {
					multiplicities.push_back(value);
				}
				setValues(*node, "poles", poleValues(spline->getPoles()));
				setValues(*node, "weights", spline->getWeights());
				setValues(*node, "knots", spline->getKnots());
				setValues(*node, "multiplicities", multiplicities);
				node->SetAttribute("degree", spline->getDegree());
				node->SetAttribute("periodic", spline->isPeriodic() ? 1 : 0);
			}
			else if (p_geo->is<Part::GeomBezierCurve>()) {
				const auto* bezier = static_cast<const Part::GeomBezierCurve*>(p_geo);
				setValues(*node, "poles", poleValues(bezier->getPoles()));
				setValues(*node, "weights", bezier->getWeights());
			}
			p_parent.InsertEndChild(node);
		}

		std::unique_ptr<Part::Geometry> readGeometry(const tinyxml2::XMLElement& p_node)
		{
			const std::string type = getText(p_node, "type");
			if (type == "GeomPoint") {
				auto geo = std::make_unique<Part::GeomPoint>();
				geo->setPoint(getPoint(p_node, "p"));
				return geo;
			}
			if (type == "GeomLineSegment") {
				auto geo = std::make_unique<Part::GeomLineSegment>();
				geo->setPoints(getPoint(p_node, "p1"), getPoint(p_node, "p2"));
				return geo;
			}
			if (type == "GeomCircle") {
				auto geo = std::make_unique<Part::GeomCircle>();
				geo->setCenter(getPoint(p_node, "center"));
				geo->setRadius(getNumber(p_node, "radius"));
				return geo;
			}
			if (type == "GeomArcOfCircle") {
				auto geo = std::make_unique<Part::GeomArcOfCircle>();
				geo->setCenter(getPoint(p_node, "center"));
				geo->setRadius(getNumber(p_node, "radius"));
				geo->setRange(
					getNumber(p_node, "first"),
					getNumber(p_node, "last"),
					/*emulateCCWXY=*/true);
				return geo;
			}
			if (type == "GeomEllipse") {
				auto geo = std::make_unique<Part::GeomEllipse>();
				geo->setCenter(getPoint(p_node, "center"));
				geo->setMajorRadius(getNumber(p_node, "major"));
				geo->setMinorRadius(getNumber(p_node, "minor"));
				geo->setMajorAxisDir(getPoint(p_node, "majorDir"));
				return geo;
			}
			if (type == "GeomArcOfEllipse") {
				auto geo = std::make_unique<Part::GeomArcOfEllipse>();
				geo->setCenter(getPoint(p_node, "center"));
				geo->setMajorRadius(getNumber(p_node, "major"));
				geo->setMinorRadius(getNumber(p_node, "minor"));
				geo->setMajorAxisDir(getPoint(p_node, "majorDir"));
				geo->setRange(
					getNumber(p_node, "first"),
					getNumber(p_node, "last"),
					/*emulateCCWXY=*/true);
				return geo;
			}
			if (type == "GeomBSplineCurve") {
				std::vector<int> multiplicities;
				for (const double value : getValues(p_node, "multiplicities")) {
					multiplicities.push_back(static_cast<int>(value));
				}
				return std::make_unique<Part::GeomBSplineCurve>(
					toPoles(getValues(p_node, "poles")),
					getValues(p_node, "weights"),
					getValues(p_node, "knots"),
					multiplicities,
					getInt(p_node, "degree", 3),
					getFlag(p_node, "periodic"),
					/*checkrational=*/true);
			}
			if (type == "GeomBezierCurve") {
				return std::make_unique<Part::GeomBezierCurve>(
					toPoles(getValues(p_node, "poles")),
					getValues(p_node, "weights"));
			}
			return nullptr;
		}

		// -------------------------------------------------------------- constraints
		void writeConstraint(
			tinyxml2::XMLElement& p_parent,
			const Sketcher::Constraint* p_constraint
		)
		{
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kConstraint);
			node->SetAttribute("type", static_cast<int>(p_constraint->Type));
			node->SetAttribute("first", p_constraint->First);
			node->SetAttribute("firstPos", static_cast<int>(p_constraint->FirstPos));
			node->SetAttribute("second", p_constraint->Second);
			node->SetAttribute("secondPos", static_cast<int>(p_constraint->SecondPos));
			node->SetAttribute("third", p_constraint->Third);
			node->SetAttribute("thirdPos", static_cast<int>(p_constraint->ThirdPos));
			setNumber(*node, "value", p_constraint->getValue());
			node->SetAttribute("driving", p_constraint->isDriving ? 1 : 0);
			node->SetAttribute("visible", p_constraint->isVisible ? 1 : 0);
			node->SetAttribute("active", p_constraint->isActive ? 1 : 0);
			p_parent.InsertEndChild(node);
		}

		std::unique_ptr<Sketcher::Constraint> readConstraint(const tinyxml2::XMLElement& p_node)
		{
			auto constraint = std::make_unique<Sketcher::Constraint>();
			constraint->Type = static_cast<Sketcher::ConstraintType>(
				getInt(p_node, "type", static_cast<int>(Sketcher::ConstraintType::None)));
			constraint->First = getInt(p_node, "first", Sketcher::GeoEnum::GeoUndef);
			constraint->FirstPos = static_cast<Sketcher::PointPos>(
				getInt(p_node, "firstPos", static_cast<int>(Sketcher::PointPos::none)));
			constraint->Second = getInt(p_node, "second", Sketcher::GeoEnum::GeoUndef);
			constraint->SecondPos = static_cast<Sketcher::PointPos>(
				getInt(p_node, "secondPos", static_cast<int>(Sketcher::PointPos::none)));
			constraint->Third = getInt(p_node, "third", Sketcher::GeoEnum::GeoUndef);
			constraint->ThirdPos = static_cast<Sketcher::PointPos>(
				getInt(p_node, "thirdPos", static_cast<int>(Sketcher::PointPos::none)));
			constraint->setValue(getNumber(p_node, "value"));
			constraint->isDriving = getFlag(p_node, "driving", true);
			constraint->isVisible = getFlag(p_node, "visible", true);
			constraint->isActive = getFlag(p_node, "active", true);
			return constraint;
		}

		// ------------------------------------------------------------------- sketch
		void writeSketch(tinyxml2::XMLElement& p_parent, SketcherObj* p_sketch)
		{
			if (p_sketch == nullptr) {
				return;
			}
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kSketch);
			const SketcherPlane2D plane = p_sketch->getPlane();
			setText(*node, "origin", components(plane.origin));
			setText(*node, "xAxis", components(plane.xAxis));
			setText(*node, "yAxis", components(plane.yAxis));
			setText(*node, "normal", components(plane.normal));
			node->SetAttribute("drawGrid", p_sketch->isDrawGrid() ? 1 : 0);
			node->SetAttribute("snapGrid", p_sketch->isSnapToGrid() ? 1 : 0);
			for (int i = 0; i <= p_sketch->getHighestCurveIndex(); ++i) {
				const Part::Geometry* geo = p_sketch->getGeometry(i);
				if (geo != nullptr) {
					writeGeometry(*node, geo, p_sketch->isGeometryVisible(i));
				}
			}
			for (int i = 0; i < p_sketch->getExternalGeometryCount(); ++i) {
				const Part::Geometry* geo = p_sketch->getExternalGeometry(i);
				if (geo != nullptr) {
					// The curves of another feature that were projected into this sketch.
					// They are written after the sketch's own, in the order the block
					// holds them, so the ids the constraints use come out the same.
					writeGeometry(*node, geo, true, /*p_external=*/true);
				}
			}
			for (int i = 0; i < p_sketch->getConstraintCount(); ++i) {
				const Sketcher::Constraint* constraint = p_sketch->getConstraint(i);
				if (constraint != nullptr) {
					writeConstraint(*node, constraint);
				}
			}
			p_parent.InsertEndChild(node);
		}

		bool readSketch(const tinyxml2::XMLElement& p_node, SketcherObj* p_sketch)
		{
			if (p_sketch == nullptr) {
				return true;
			}
			// The geometry goes in first: setting the plane also fits the camera, and it
			// has to have something to fit to.
			for (const tinyxml2::XMLElement* child = p_node.FirstChildElement(kGeometry);
				child != nullptr;
				child = child->NextSiblingElement(kGeometry)) {
				std::unique_ptr<Part::Geometry> geo = readGeometry(*child);
				if (geo == nullptr) {
					CORE_ERROR(
						"[MoonDocument] a '{0}' curve cannot be read",
						getText(*child, "type"));
					return false;
				}
				geo->setConstruction(getFlag(*child, "construction"));
				if (getFlag(*child, "external")) {
					// Back into the reference block: addExternalGeometry() numbers it from
					// the end of the block and tells the solver it is fixed, which is what
					// the constraints on it expect.
					if (p_sketch->addExternalGeometry(std::move(geo)) == SketcherObj::NoGeoId) {
						CORE_ERROR(
							"[MoonDocument] the sketch refused the external {0} curve",
							getText(*child, "type"));
						return false;
					}
				}
				else {
					const int geoId = p_sketch->addGeometry(geo);
					if (geoId < 0) {
						CORE_ERROR(
							"[MoonDocument] the sketch refused a {0} curve",
							getText(*child, "type"));
						return false;
					}
					p_sketch->setGeometryVisible(geoId, getFlag(*child, "visible", true));
				}
			}
			for (const tinyxml2::XMLElement* child = p_node.FirstChildElement(kConstraint);
				child != nullptr;
				child = child->NextSiblingElement(kConstraint)) {
				std::unique_ptr<Sketcher::Constraint> constraint = readConstraint(*child);
				if (constraint == nullptr || p_sketch->addConstraint(std::move(constraint)) < 0) {
					CORE_ERROR("[MoonDocument] the sketch refused a constraint");
					return false;
				}
			}
			SketcherPlane2D plane;
			plane.origin = getPoint(p_node, "origin");
			plane.xAxis = getPoint(p_node, "xAxis");
			plane.yAxis = getPoint(p_node, "yAxis");
			plane.normal = getPoint(p_node, "normal");
			p_sketch->setPlane(plane);
			p_sketch->setDrawGrid(getFlag(p_node, "drawGrid", true));
			p_sketch->setSnapToGrid(getFlag(p_node, "snapGrid", false));
			p_sketch->solve();
			p_sketch->makeDone();
			// A sketch that is not being edited does not draw itself: the curves come
			// from the feature's actor (which the tree view's eye switches off). The
			// sketch object is active by default - it is created to be edited - and
			// nobody else would switch it off for a sketch that was read from a file,
			// so it would keep painting its curves over the viewport.
			p_sketch->setActive(false);
			return true;
		}

		// --------------------------------------------------------------- the feature
		const char* typeNameOf(const Feature* p_feature)
		{
			if (dynamic_cast<const SketcherFeature*>(p_feature) != nullptr) {
				return "SketcherFeature";
			}
			if (dynamic_cast<const ExtrudeFeature*>(p_feature) != nullptr) {
				return "ExtrudeFeature";
			}
			if (dynamic_cast<const RevolveFeature*>(p_feature) != nullptr) {
				return "RevolveFeature";
			}
			if (dynamic_cast<const FilletFeature*>(p_feature) != nullptr) {
				return "FilletFeature";
			}
			if (dynamic_cast<const ChamferFeature*>(p_feature) != nullptr) {
				return "ChamferFeature";
			}
			if (dynamic_cast<const ThicknessFeature*>(p_feature) != nullptr) {
				return "ThicknessFeature";
			}
			if (dynamic_cast<const DatumLineFeature*>(p_feature) != nullptr) {
				return "DatumLineFeature";
			}
			if (dynamic_cast<const PolarPatternFeature*>(p_feature) != nullptr) {
				return "PolarPatternFeature";
			}
			if (dynamic_cast<const LinearPatternFeature*>(p_feature) != nullptr) {
				return "LinearPatternFeature";
			}
			if (dynamic_cast<const MirrorFeature*>(p_feature) != nullptr) {
				return "MirrorFeature";
			}
			return nullptr;
		}

		Feature* createFeature(const std::string& p_type, const std::string& p_name, int p_addSubType)
		{
			if (p_type == "SketcherFeature") {
				return new SketcherFeature(p_name);
			}
			if (p_type == "ExtrudeFeature") {
				return new ExtrudeFeature(p_name, p_addSubType);
			}
			if (p_type == "RevolveFeature") {
				return new RevolveFeature(p_name, p_addSubType);
			}
			if (p_type == "FilletFeature") {
				return new FilletFeature(p_name);
			}
			if (p_type == "ChamferFeature") {
				return new ChamferFeature(p_name);
			}
			if (p_type == "ThicknessFeature") {
				return new ThicknessFeature(p_name);
			}
			if (p_type == "DatumLineFeature") {
				return new DatumLineFeature(p_name);
			}
			if (p_type == "PolarPatternFeature") {
				return new PolarPatternFeature(p_name);
			}
			if (p_type == "LinearPatternFeature") {
				return new LinearPatternFeature(p_name);
			}
			if (p_type == "MirrorFeature") {
				return new MirrorFeature(p_name);
			}
			return nullptr;
		}

		void writePose(tinyxml2::XMLElement& p_parent, const Feature& p_feature)
		{
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kPose);
			const Maths::FVector3& position = p_feature.transform.GetLocalPosition();
			const Maths::FQuaternion& rotation = p_feature.transform.GetLocalRotation();
			const Maths::FVector3& scale = p_feature.transform.GetLocalScale();
			setValues(*node, "pos", { position.x, position.y, position.z });
			setValues(*node, "rot", { rotation.x, rotation.y, rotation.z, rotation.w });
			setValues(*node, "scale", { scale.x, scale.y, scale.z });
			p_parent.InsertEndChild(node);
		}

		void readPose(const tinyxml2::XMLElement& p_node, Feature& p_feature)
		{
			const tinyxml2::XMLElement* pose = p_node.FirstChildElement(kPose);
			if (pose == nullptr) {
				return;
			}
			const std::vector<double> position = getValues(*pose, "pos");
			const std::vector<double> rotation = getValues(*pose, "rot");
			const std::vector<double> scale = getValues(*pose, "scale");
			if (position.size() >= 3) {
				p_feature.transform.SetLocalPosition(
					Maths::FVector3(position[0], position[1], position[2]));
			}
			if (rotation.size() >= 4) {
				p_feature.transform.SetLocalRotation(
					Maths::FQuaternion(rotation[0], rotation[1], rotation[2], rotation[3]));
			}
			if (scale.size() >= 3) {
				p_feature.transform.SetLocalScale(
					Maths::FVector3(scale[0], scale[1], scale[2]));
			}
		}

		void writeReferences(tinyxml2::XMLElement& p_parent, const Feature& p_feature)
		{
			tinyxml2::XMLDocument* doc = p_parent.GetDocument();
			tinyxml2::XMLElement* node = doc->NewElement(kReferences);
			const std::vector<std::string>& values = p_feature.getSubValues();
			const std::vector<std::vector<std::string>>& names = p_feature.getReferenceNames();
			for (int i = 0; i < static_cast<int>(values.size()); ++i) {
				tinyxml2::XMLElement* sub = doc->NewElement(kSub);
				setText(*sub, "value", values[i]);
				if (i < static_cast<int>(names.size())) {
					for (const std::string& name : names[i]) {
						tinyxml2::XMLElement* nameNode = doc->NewElement(kName);
						nameNode->SetText(name.c_str());
						sub->InsertEndChild(nameNode);
					}
				}
				node->InsertEndChild(sub);
			}
			p_parent.InsertEndChild(node);
		}

		void readReferences(const tinyxml2::XMLElement& p_node, Feature& p_feature)
		{
			const tinyxml2::XMLElement* references = p_node.FirstChildElement(kReferences);
			if (references == nullptr) {
				return;
			}
			std::vector<std::string> values;
			std::vector<std::vector<std::string>> names;
			for (const tinyxml2::XMLElement* sub = references->FirstChildElement(kSub);
				sub != nullptr;
				sub = sub->NextSiblingElement(kSub)) {
				values.push_back(getText(*sub, "value"));
				std::vector<std::string> subNames;
				for (const tinyxml2::XMLElement* name = sub->FirstChildElement(kName);
					name != nullptr;
					name = name->NextSiblingElement(kName)) {
					const char* value = name->GetText();
					if (value != nullptr) {
						subNames.push_back(value);
					}
				}
				names.push_back(std::move(subNames));
			}
			p_feature.setSubValues(values);
			p_feature.setReferenceNames(names);
		}

		/** The face an "up to face" pad or pocket ends at: the feature it was
		 * picked on, the name of the element, and the mapped names it was last
		 * seen under. The feature is an index into the body's chain, which is
		 * only turned into a pointer once every feature of the file exists. */
		struct StoredUpToFace
		{
			int feature = -1;
			std::string value;
			std::vector<std::string> names;
		};

		StoredUpToFace readUpToFace(const tinyxml2::XMLElement& p_node)
		{
			StoredUpToFace result;
			const tinyxml2::XMLElement* parameters = p_node.FirstChildElement(kParameters);
			const tinyxml2::XMLElement* node = parameters != nullptr
				? parameters->FirstChildElement(kUpToFace) : nullptr;
			if (node == nullptr) {
				return result;
			}
			result.feature = getInt(*node, "feature", -1);
			result.value = getText(*node, "value");
			for (const tinyxml2::XMLElement* name = node->FirstChildElement(kName);
				name != nullptr;
				name = name->NextSiblingElement(kName)) {
				const char* value = name->GetText();
				if (value != nullptr) {
					result.names.push_back(value);
				}
			}
			return result;
		}

		void writeParameters(tinyxml2::XMLElement& p_parent, Feature& p_feature)
		{
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kParameters);
			const std::vector<Feature*>& all = FeatureBody::instance().getFeatures();
			if (auto* sketch = dynamic_cast<SketcherFeature*>(&p_feature)) {
				writeSketch(*node, sketch->getSketcherObj());
			}
			else if (auto* extrude = dynamic_cast<ExtrudeFeature*>(&p_feature)) {
				setNumber(*node, "lengthForward", extrude->lengthForward);
				setNumber(*node, "angleForward", extrude->angleForward);
				setNumber(*node, "lengthRev", extrude->lengthRev);
				setNumber(*node, "angleRev", extrude->angleRev);
				setValues(*node, "finalDir", {
					extrude->finalDir.X(), extrude->finalDir.Y(), extrude->finalDir.Z() });
				node->SetAttribute("dirType", extrude->dirType);
				node->SetAttribute("extrudeType", extrude->extrudeType);
				node->SetAttribute("addSubType", extrude->addSubType);
				if (extrude->extrudeType == 2) {
					// The face is written as the reference it is - the feature it
					// was picked on and the name of the element - so it lands on
					// whatever that shape looks like when the file is read again.
					// Writing the shape itself would freeze the geometry at the
					// day it was picked.
					const int owner = indexOf(all, extrude->upToFaceFeature);
					if (owner >= 0 && !extrude->upToFaceRef.empty()) {
						tinyxml2::XMLElement* upTo
							= node->GetDocument()->NewElement(kUpToFace);
						upTo->SetAttribute("feature", owner);
						setText(*upTo, "value", extrude->upToFaceRef);
						for (const std::string& name : extrude->upToFaceNames) {
							tinyxml2::XMLElement* nameNode
								= node->GetDocument()->NewElement(kName);
							nameNode->SetText(name.c_str());
							upTo->InsertEndChild(nameNode);
						}
						node->InsertEndChild(upTo);
					}
					else {
						// Only a feature of this body has an index the document
						// can write, and only a pick that was kept as a reference
						// can be resolved again; neither is at hand here.
						CORE_WARN(
							"[MoonDocument] {0} is padded up to a face that was not "
							"picked as a reference of this body; it comes back as a "
							"plain length",
							extrude->GetName());
					}
				}
			}
			else if (auto* revolve = dynamic_cast<RevolveFeature*>(&p_feature)) {
				const gp_Pnt location = revolve->axis.Location();
				const gp_Dir direction = revolve->axis.Direction();
				setValues(*node, "origin", {
					revolve->origin[0], revolve->origin[1], revolve->origin[2] });
				setNumber(*node, "angle", revolve->angle);
				node->SetAttribute("addSubType", revolve->addSubType);
				node->SetAttribute("axisType", revolve->axisType);
				node->SetAttribute("reverse", revolve->reverse ? 1 : 0);
				setValues(*node, "axisLocation", {
					location.X(), location.Y(), location.Z() });
				setValues(*node, "axisDirection", {
					direction.X(), direction.Y(), direction.Z() });
			}
			else if (auto* fillet = dynamic_cast<FilletFeature*>(&p_feature)) {
				setNumber(*node, "radius", fillet->radius);
				setNumber(*node, "len", fillet->len);
				node->SetAttribute("useAllEdges", fillet->useAllEdges ? 1 : 0);
				node->SetAttribute("toolSubtractive", fillet->toolSubtractive ? 1 : 0);
				setValues(*node, "origin1", {
					fillet->origin1[0], fillet->origin1[1], fillet->origin1[2] });
				setValues(*node, "origin2", {
					fillet->origin2[0], fillet->origin2[1], fillet->origin2[2] });
				setValues(*node, "dir1", {
					fillet->dir1[0], fillet->dir1[1], fillet->dir1[2] });
				setValues(*node, "dir2", {
					fillet->dir2[0], fillet->dir2[1], fillet->dir2[2] });
			}
			else if (auto* chamfer = dynamic_cast<ChamferFeature*>(&p_feature)) {
				setNumber(*node, "size", chamfer->size);
				setNumber(*node, "size2", chamfer->size2);
				setNumber(*node, "angle", chamfer->angle);
				setNumber(*node, "len", chamfer->len);
				node->SetAttribute("chamferType", chamfer->chamferType);
				node->SetAttribute("flipDirection", chamfer->flipDirection ? 1 : 0);
				node->SetAttribute("useAllEdges", chamfer->useAllEdges ? 1 : 0);
				node->SetAttribute("toolSubtractive", chamfer->toolSubtractive ? 1 : 0);
				setValues(*node, "origin1", {
					chamfer->origin1[0], chamfer->origin1[1], chamfer->origin1[2] });
				setValues(*node, "origin2", {
					chamfer->origin2[0], chamfer->origin2[1], chamfer->origin2[2] });
				setValues(*node, "dir1", {
					chamfer->dir1[0], chamfer->dir1[1], chamfer->dir1[2] });
				setValues(*node, "dir2", {
					chamfer->dir2[0], chamfer->dir2[1], chamfer->dir2[2] });
			}
			else if (auto* thickness = dynamic_cast<ThicknessFeature*>(&p_feature)) {
				setNumber(*node, "thickNessValue", thickness->thickNessValue);
				setNumber(*node, "scale", thickness->scale);
				node->SetAttribute("mode", thickness->mode);
				node->SetAttribute("joinType", thickness->joinType);
				node->SetAttribute("reverse", thickness->reverse ? 1 : 0);
				node->SetAttribute("intersection", thickness->intersection ? 1 : 0);
				setValues(*node, "dir", {
					thickness->dir[0], thickness->dir[1], thickness->dir[2] });
				setValues(*node, "midPoint", {
					thickness->midPoint[0], thickness->midPoint[1], thickness->midPoint[2] });
			}
			else if (auto* datum = dynamic_cast<DatumLineFeature*>(&p_feature)) {
				setValues(*node, "origin", {
					datum->origin.x, datum->origin.y, datum->origin.z });
				setValues(*node, "direction", {
					datum->direction.x, datum->direction.y, datum->direction.z });
				setNumber(*node, "length", datum->length);
			}
			else if (auto* pattern = dynamic_cast<PolarPatternFeature*>(&p_feature)) {
				const gp_Pnt location = pattern->axis.Location();
				const gp_Dir direction = pattern->axis.Direction();
				node->SetAttribute("mode", pattern->mode);
				node->SetAttribute("axisType", pattern->axisType);
				node->SetAttribute("occurrences", pattern->occurrences);
				node->SetAttribute("reverse", pattern->reverse ? 1 : 0);
				setNumber(*node, "angle", pattern->angle);
				setValues(*node, "axisLocation", {
					location.X(), location.Y(), location.Z() });
				setValues(*node, "axisDirection", {
					direction.X(), direction.Y(), direction.Z() });
				setIndices(*node, "originals", indicesOf(all, pattern->originals));
			}
			else if (auto* pattern = dynamic_cast<LinearPatternFeature*>(&p_feature)) {
				node->SetAttribute("mode", pattern->mode);
				node->SetAttribute("directionType", pattern->directionType);
				node->SetAttribute("dimensionMode", pattern->dimensionMode);
				node->SetAttribute("occurrences", pattern->occurrences);
				node->SetAttribute("reverse", pattern->reverse ? 1 : 0);
				setNumber(*node, "length", pattern->length);
				setValues(*node, "direction", {
					pattern->direction.X(), pattern->direction.Y(), pattern->direction.Z() });
				node->SetAttribute("directionType2", pattern->directionType2);
				node->SetAttribute("dimensionMode2", pattern->dimensionMode2);
				node->SetAttribute("occurrences2", pattern->occurrences2);
				node->SetAttribute("reverse2", pattern->reverse2 ? 1 : 0);
				setNumber(*node, "length2", pattern->length2);
				setValues(*node, "direction2", {
					pattern->direction2.X(), pattern->direction2.Y(), pattern->direction2.Z() });
				setIndices(*node, "originals", indicesOf(all, pattern->originals));
			}
			else if (auto* mirror = dynamic_cast<MirrorFeature*>(&p_feature)) {
				const gp_Pnt location = mirror->plane.Location();
				const gp_Dir direction = mirror->plane.Direction();
				node->SetAttribute("mode", mirror->mode);
				node->SetAttribute("planeType", mirror->planeType);
				setValues(*node, "planeLocation", {
					location.X(), location.Y(), location.Z() });
				setValues(*node, "planeDirection", {
					direction.X(), direction.Y(), direction.Z() });
				setIndices(*node, "originals", indicesOf(all, mirror->originals));
			}
			p_parent.InsertEndChild(node);
		}

		bool readParameters(const tinyxml2::XMLElement& p_node, Feature& p_feature)
		{
			const tinyxml2::XMLElement* node = p_node.FirstChildElement(kParameters);
			if (node == nullptr) {
				return true;  // nothing stored: the feature keeps its defaults
			}
			if (auto* sketch = dynamic_cast<SketcherFeature*>(&p_feature)) {
				// The curves and constraints live inside the <Sketch> node, one level
				// below <Parameters>: reading them off the parameters node itself finds
				// nothing at all, which is how a sketch comes back empty.
				const tinyxml2::XMLElement* sketchNode = node->FirstChildElement(kSketch);
				return sketchNode != nullptr
					? readSketch(*sketchNode, sketch->getSketcherObj())
					: true;
			}
			if (auto* extrude = dynamic_cast<ExtrudeFeature*>(&p_feature)) {
				extrude->lengthForward = static_cast<float>(
					getNumber(*node, "lengthForward", extrude->lengthForward));
				extrude->angleForward = getNumber(*node, "angleForward", extrude->angleForward);
				extrude->lengthRev = getNumber(*node, "lengthRev", extrude->lengthRev);
				extrude->angleRev = getNumber(*node, "angleRev", extrude->angleRev);
				const std::vector<double> direction = getValues(*node, "finalDir");
				if (direction.size() >= 3) {
					extrude->finalDir = gp_Vec(direction[0], direction[1], direction[2]);
				}
				extrude->dirType = getInt(*node, "dirType", extrude->dirType);
				extrude->extrudeType = getInt(*node, "extrudeType", extrude->extrudeType);
				extrude->addSubType = getInt(*node, "addSubType", extrude->addSubType);
				return true;
			}
			if (auto* revolve = dynamic_cast<RevolveFeature*>(&p_feature)) {
				const std::vector<double> origin = getValues(*node, "origin");
				if (origin.size() >= 3) {
					revolve->origin[0] = static_cast<float>(origin[0]);
					revolve->origin[1] = static_cast<float>(origin[1]);
					revolve->origin[2] = static_cast<float>(origin[2]);
				}
				revolve->angle = static_cast<float>(getNumber(*node, "angle", revolve->angle));
				revolve->addSubType = getInt(*node, "addSubType", revolve->addSubType);
				revolve->axisType = getInt(*node, "axisType", revolve->axisType);
				revolve->reverse = getFlag(*node, "reverse", revolve->reverse);
				const std::vector<double> location = getValues(*node, "axisLocation");
				const std::vector<double> direction = getValues(*node, "axisDirection");
				if (location.size() >= 3 && direction.size() >= 3) {
					revolve->axis = gp_Ax1(
						gp_Pnt(location[0], location[1], location[2]),
						gp_Dir(direction[0], direction[1], direction[2]));
				}
				return true;
			}
			if (auto* fillet = dynamic_cast<FilletFeature*>(&p_feature)) {
				fillet->radius = static_cast<float>(getNumber(*node, "radius", fillet->radius));
				fillet->len = static_cast<float>(getNumber(*node, "len", fillet->len));
				fillet->useAllEdges = getFlag(*node, "useAllEdges", fillet->useAllEdges);
				fillet->toolSubtractive = getFlag(
					*node, "toolSubtractive", fillet->toolSubtractive);
				readVector3(*node, "origin1", fillet->origin1);
				readVector3(*node, "origin2", fillet->origin2);
				readVector3(*node, "dir1", fillet->dir1);
				readVector3(*node, "dir2", fillet->dir2);
				return true;
			}
			if (auto* chamfer = dynamic_cast<ChamferFeature*>(&p_feature)) {
				chamfer->size = static_cast<float>(getNumber(*node, "size", chamfer->size));
				chamfer->size2 = static_cast<float>(getNumber(*node, "size2", chamfer->size2));
				chamfer->angle = static_cast<float>(getNumber(*node, "angle", chamfer->angle));
				chamfer->len = static_cast<float>(getNumber(*node, "len", chamfer->len));
				chamfer->chamferType = getInt(*node, "chamferType", chamfer->chamferType);
				chamfer->flipDirection = getFlag(
					*node, "flipDirection", chamfer->flipDirection);
				chamfer->useAllEdges = getFlag(*node, "useAllEdges", chamfer->useAllEdges);
				chamfer->toolSubtractive = getFlag(
					*node, "toolSubtractive", chamfer->toolSubtractive);
				readVector3(*node, "origin1", chamfer->origin1);
				readVector3(*node, "origin2", chamfer->origin2);
				readVector3(*node, "dir1", chamfer->dir1);
				readVector3(*node, "dir2", chamfer->dir2);
				return true;
			}
			if (auto* thickness = dynamic_cast<ThicknessFeature*>(&p_feature)) {
				thickness->thickNessValue = static_cast<float>(
					getNumber(*node, "thickNessValue", thickness->thickNessValue));
				thickness->scale = static_cast<float>(getNumber(*node, "scale", thickness->scale));
				thickness->mode = getInt(*node, "mode", thickness->mode);
				thickness->joinType = getInt(*node, "joinType", thickness->joinType);
				thickness->reverse = getFlag(*node, "reverse", thickness->reverse);
				thickness->intersection = getFlag(
					*node, "intersection", thickness->intersection);
				readVector3(*node, "dir", thickness->dir);
				readVector3(*node, "midPoint", thickness->midPoint);
				return true;
			}
			if (auto* datum = dynamic_cast<DatumLineFeature*>(&p_feature)) {
				const std::vector<double> origin = getValues(*node, "origin");
				const std::vector<double> direction = getValues(*node, "direction");
				if (origin.size() >= 3) {
					datum->origin = Maths::FVector3(origin[0], origin[1], origin[2]);
				}
				if (direction.size() >= 3) {
					datum->direction = Maths::FVector3(direction[0], direction[1], direction[2]);
				}
				datum->length = static_cast<float>(getNumber(*node, "length", datum->length));
				return true;
			}
			if (auto* pattern = dynamic_cast<PolarPatternFeature*>(&p_feature)) {
				pattern->mode = getInt(*node, "mode", pattern->mode);
				pattern->axisType = getInt(*node, "axisType", pattern->axisType);
				pattern->occurrences = getInt(*node, "occurrences", pattern->occurrences);
				pattern->reverse = getFlag(*node, "reverse", pattern->reverse);
				pattern->angle = static_cast<float>(getNumber(*node, "angle", pattern->angle));
				const std::vector<double> location = getValues(*node, "axisLocation");
				const std::vector<double> direction = getValues(*node, "axisDirection");
				if (location.size() >= 3 && direction.size() >= 3) {
					pattern->axis = gp_Ax1(
						gp_Pnt(location[0], location[1], location[2]),
						gp_Dir(direction[0], direction[1], direction[2]));
				}
				return true;
			}
			if (auto* pattern = dynamic_cast<LinearPatternFeature*>(&p_feature)) {
				pattern->mode = getInt(*node, "mode", pattern->mode);
				pattern->directionType = getInt(*node, "directionType", pattern->directionType);
				pattern->dimensionMode = getInt(*node, "dimensionMode", pattern->dimensionMode);
				pattern->occurrences = getInt(*node, "occurrences", pattern->occurrences);
				pattern->reverse = getFlag(*node, "reverse", pattern->reverse);
				pattern->length = static_cast<float>(getNumber(*node, "length", pattern->length));
				const std::vector<double> direction = getValues(*node, "direction");
				if (direction.size() >= 3) {
					pattern->direction = gp_Dir(direction[0], direction[1], direction[2]);
				}
				pattern->directionType2 = getInt(
					*node, "directionType2", pattern->directionType2);
				pattern->dimensionMode2 = getInt(
					*node, "dimensionMode2", pattern->dimensionMode2);
				pattern->occurrences2 = getInt(*node, "occurrences2", pattern->occurrences2);
				pattern->reverse2 = getFlag(*node, "reverse2", pattern->reverse2);
				pattern->length2 = static_cast<float>(getNumber(*node, "length2", pattern->length2));
				const std::vector<double> direction2 = getValues(*node, "direction2");
				if (direction2.size() >= 3) {
					pattern->direction2 = gp_Dir(direction2[0], direction2[1], direction2[2]);
				}
				return true;
			}
			if (auto* mirror = dynamic_cast<MirrorFeature*>(&p_feature)) {
				mirror->mode = getInt(*node, "mode", mirror->mode);
				mirror->planeType = getInt(*node, "planeType", mirror->planeType);
				const std::vector<double> location = getValues(*node, "planeLocation");
				const std::vector<double> direction = getValues(*node, "planeDirection");
				if (location.size() >= 3 && direction.size() >= 3) {
					mirror->plane = gp_Ax2(
						gp_Pnt(location[0], location[1], location[2]),
						gp_Dir(direction[0], direction[1], direction[2]));
				}
				return true;
			}
			return true;
		}

		void writeFeature(
			tinyxml2::XMLElement& p_parent,
			Feature& p_feature,
			const std::vector<Feature*>& p_features
		)
		{
			const char* type = typeNameOf(&p_feature);
			if (type == nullptr) {
				CORE_WARN(
					"[MoonDocument] {0} has no document type; it is left out of the file",
					p_feature.GetName());
				return;
			}
			tinyxml2::XMLElement* node = p_parent.GetDocument()->NewElement(kFeature);
			node->SetAttribute("type", type);
			node->SetAttribute("name", p_feature.GetName().c_str());
			node->SetAttribute("tag", p_feature.GetTag().c_str());
			node->SetAttribute("base", indexOf(p_features, p_feature.getBaseFeature()));
			if (auto* profile = dynamic_cast<FeatureBaseProfile*>(&p_feature)) {
				node->SetAttribute("profile", indexOf(p_features, profile->getProfile()));
			}
			// What the tree view's eye switched off is part of the document: a feature is
			// hidden with its own flag, and everything under it (the render anchors and
			// the topology actors) by name.
			node->SetAttribute("active", p_feature.IsSelfActive() ? 1 : 0);
			std::vector<std::string> hidden;
			collectHiddenActors(p_feature, hidden);
			if (!hidden.empty()) {
				tinyxml2::XMLElement* hosts =
					p_parent.GetDocument()->NewElement(kHidden);
				for (const std::string& name : hidden) {
					tinyxml2::XMLElement* actor = p_parent.GetDocument()->NewElement(kActor);
					actor->SetAttribute("name", name.c_str());
					hosts->InsertEndChild(actor);
				}
				node->InsertEndChild(hosts);
			}
			writePose(*node, p_feature);
			writeReferences(*node, p_feature);
			writeParameters(*node, p_feature);
			p_parent.InsertEndChild(node);
		}
	}

	bool MoonDocument::save(const std::string& p_path)
	{
		const std::vector<Feature*>& features = FeatureBody::instance().getFeatures();
		tinyxml2::XMLDocument doc;
		tinyxml2::XMLElement* root = doc.NewElement(kRoot);
		root->SetAttribute("version", kVersion);
		tinyxml2::XMLElement* body = doc.NewElement(kBody);
		body->SetAttribute("name", "Body");
		for (Feature* feature : features) {
			if (feature != nullptr) {
				writeFeature(*body, *feature, features);
			}
		}
		root->InsertEndChild(body);
		doc.InsertEndChild(root);
		FILE* stream = openStream(p_path, /*p_write=*/true);
		if (stream == nullptr) {
			CORE_ERROR("[MoonDocument] {0} could not be opened for writing", p_path);
			return false;
		}
		const tinyxml2::XMLError error = doc.SaveFile(stream);
		std::fclose(stream);
		if (error != tinyxml2::XML_SUCCESS) {
			CORE_ERROR(
				"[MoonDocument] {0} could not be written (tinyxml2 error {1})",
				p_path,
				static_cast<int>(error));
			return false;
		}
		CORE_INFO("[MoonDocument] wrote {0} feature(s) to {1}", features.size(), p_path);
		return true;
	}

	bool MoonDocument::open(const std::string& p_path)
	{
		tinyxml2::XMLDocument doc;
		{
			FILE* stream = openStream(p_path, /*p_write=*/false);
			if (stream == nullptr) {
				CORE_ERROR("[MoonDocument] {0} could not be opened", p_path);
				return false;
			}
			const tinyxml2::XMLError error = doc.LoadFile(stream);
			std::fclose(stream);
			if (error != tinyxml2::XML_SUCCESS) {
				CORE_ERROR("[MoonDocument] {0} could not be read", p_path);
				return false;
			}
		}
		const tinyxml2::XMLElement* root = doc.FirstChildElement(kRoot);
		if (root == nullptr) {
			CORE_ERROR("[MoonDocument] {0} is not a Moon document", p_path);
			return false;
		}
		const int version = getInt(*root, "version", 0);
		if (version > kVersion) {
			CORE_ERROR(
				"[MoonDocument] {0} was written by a newer version ({1} > {2})",
				p_path,
				version,
				kVersion);
			return false;
		}
		const tinyxml2::XMLElement* body = root->FirstChildElement(kBody);
		if (body == nullptr) {
			CORE_ERROR("[MoonDocument] {0} has no body", p_path);
			return false;
		}

		// The chain that is loaded now is put aside, and the body is emptied for the
		// time being: a feature lists itself in the body as soon as it is constructed, so
		// what is built here would otherwise end up mixed with what is already there -
		// and a file that cannot be built has to leave the loaded chain untouched, which
		// only works while the old one is still in one piece.
		const std::vector<Feature*> previous = FeatureBody::instance().getFeatures();
		FeatureBody::instance().setFeatures({});

		// First pass: build every feature with its own parameters. Nothing is executed
		// yet, because a feature's links may name features that appear later in the list.
		std::vector<Feature*> features;
		std::vector<int> baseIndices;
		std::vector<int> profileIndices;
		std::vector<std::vector<int>> originalIndices;
		std::vector<StoredUpToFace> upToFaceRefs;
		std::vector<bool> activeFlags;
		std::vector<std::vector<std::string>> hiddenNames;
		const auto dropBuilt = [&features, &previous]() {
			for (Feature* feature : features) {
				feature->RemoveFromScene();
				delete feature;
			}
			FeatureBody::instance().setFeatures(previous);
			};
		for (const tinyxml2::XMLElement* node = body->FirstChildElement(kFeature);
			node != nullptr;
			node = node->NextSiblingElement(kFeature)) {
			const std::string type = getText(*node, "type");
			const std::string name = getText(*node, "name", "feature");
			Feature* feature = createFeature(type, name, getInt(*node, "addSubType", 0));
			if (feature == nullptr) {
				CORE_ERROR("[MoonDocument] a '{0}' feature is not known to this build", type);
				dropBuilt();
				return false;
			}
			features.push_back(feature);  // from here on it is dropped with the rest
			feature->SetName(name);
			feature->SetTag(getText(*node, "tag", feature->GetTag().c_str()));
			readPose(*node, *feature);
			readReferences(*node, *feature);
			if (!readParameters(*node, *feature)) {
				dropBuilt();
				return false;
			}
			baseIndices.push_back(getInt(*node, "base", -1));
			profileIndices.push_back(getInt(*node, "profile", -1));
			upToFaceRefs.push_back(readUpToFace(*node));
			activeFlags.push_back(getFlag(*node, "active", true));
			std::vector<std::string> hidden;
			if (const tinyxml2::XMLElement* hosts = node->FirstChildElement(kHidden)) {
				for (const tinyxml2::XMLElement* actor = hosts->FirstChildElement(kActor);
					actor != nullptr;
					actor = actor->NextSiblingElement(kActor)) {
					hidden.push_back(getText(*actor, "name"));
				}
			}
			hiddenNames.push_back(std::move(hidden));
			const tinyxml2::XMLElement* parameters = node->FirstChildElement(kParameters);
			originalIndices.push_back(
				parameters != nullptr ? getIndices(*parameters, "originals") : std::vector<int>());
		}

		// Second pass: the links, now that every feature exists.
		for (int i = 0; i < static_cast<int>(features.size()); ++i) {
			Feature* feature = features[i];
			feature->setBaseFeature(at(features, baseIndices[i]));
			if (auto* profile = dynamic_cast<FeatureBaseProfile*>(feature)) {
				profile->setProfile(dynamic_cast<SketcherFeature*>(at(features, profileIndices[i])));
			}
			if (auto* extrude = dynamic_cast<ExtrudeFeature*>(feature)) {
				const StoredUpToFace& upTo = upToFaceRefs[i];
				if (Feature* target = at(features, upTo.feature)) {
					if (target == feature) {
						// A face picked on the feature itself (which is what a pad
						// that was already built and edited offers to the pick) is
						// only meaningful as the matching face of the shape below;
						// execute() takes it there.
						CORE_INFO(
							"[MoonDocument] {0}: the face '{1}' was picked on the "
							"feature itself, it is taken from the shape below",
							feature->GetName(),
							upTo.value);
					}
					extrude->upToFaceFeature = target;
					extrude->upToFaceRef = upTo.value;
					extrude->upToFaceNames = upTo.names;
				}
			}
			std::vector<Feature*> originals;
			for (const int index : originalIndices[i]) {
				if (Feature* original = at(features, index)) {
					originals.push_back(original);
				}
			}
			if (auto* pattern = dynamic_cast<PolarPatternFeature*>(feature)) {
				pattern->originals = originals;
			}
			else if (auto* pattern = dynamic_cast<LinearPatternFeature*>(feature)) {
				pattern->originals = originals;
			}
			else if (auto* mirror = dynamic_cast<MirrorFeature*>(feature)) {
				mirror->originals = originals;
			}
		}

		// Only now is the chain that was there before taken apart, and the actors leave
		// the scene with it: everything the file asked for exists, so nothing can fail
		// half way and leave the body without either chain.
		for (Feature* feature : previous) {
			feature->RemoveFromScene();
			delete feature;
		}
		// The sketch manager hands the current sketch to the drawing tools: the chain
		// that just went has to leave it (a stale pointer there is a crash the next time
		// a tool asks), and the one that was read has to be in it, or entering one of the
		// loaded sketches would find nothing.
		for (Feature* feature : previous) {
			if (auto* sketch = dynamic_cast<SketcherFeature*>(feature)) {
				SketcherObjManager::instance().removeSketcherFeature(sketch);
			}
		}
		for (Feature* feature : features) {
			if (auto* sketch = dynamic_cast<SketcherFeature*>(feature)) {
				SketcherObjManager::instance().addSketcherFeature(sketch);
			}
		}
		for (Feature* feature : features) {
			if (!feature->execute()) {
				CORE_ERROR("[MoonDocument] {0} could not be built", feature->GetName());
			}
			feature->makeDone();
		}

		// The visibility comes last: the actors below a feature - the render anchors and
		// the topology ones - are only there once the feature was discretized, and it is
		// those the eye switches off.
		for (int i = 0; i < static_cast<int>(features.size()); ++i) {
			Feature* feature = features[i];
			feature->SetActive(activeFlags[i]);
			for (const std::string& name : hiddenNames[i]) {
				if (Core::ECS::Actor* actor = findActor(*feature, name)) {
					actor->SetActive(false);
				}
			}
		}

		// A feature the file described but that came out without a shape is worth saying
		// out loud: the model would otherwise just be missing a part of itself.
		for (Feature* feature : features) {
			if (feature->GetTopoShape().isNull()) {
				CORE_WARN(
					"[MoonDocument] {0} came back without a shape",
					feature->GetName());
			}
		}
		CORE_INFO("[MoonDocument] read {0} feature(s) from {1}", features.size(), p_path);
		return true;
	}
}
