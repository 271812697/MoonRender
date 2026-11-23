#include "editor/UI/TaskPanel/FilletInteraction.h"

#include <algorithm>
#include <vector>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <TopoDS_Shape.hxx>

#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "feature/FilletFeature.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Widgets/AxisTranslationWidget.h"
#include "base/BoundBox.h"
#include "App/GizmoHelper.h"

namespace MOON
{
	namespace
	{
		/** The smallest radius a fillet may be built at: it only has to be non-zero, an edge
		 * cannot be rounded by nothing. */
		constexpr double kSmallestRadius = 1.0e-3;

		/** A conservative starting radius: an edge cannot be filleted with a radius larger than a
		 * quarter of its own length, so the shortest selected edge limits the value. */
		float safeInitialFilletRadius(const std::vector<Part::TopoShape>& p_edges,
			double p_baseDiagonal)
		{
			double radius = p_baseDiagonal * 0.03;
			for (const Part::TopoShape& edge : p_edges)
			{
				if (edge.isNull())
				{
					continue;
				}
				GProp_GProps props;
				BRepGProp::LinearProperties(edge.getShape(), props);
				const double length = props.Mass();
				if (length > Precision::Confusion())
				{
					radius = std::min(radius, length * 0.25);
				}
			}
			return static_cast<float>(radius);
		}
	}

	FilletInteraction::FilletInteraction()
		: ShapeHelper(nullptr)
	{
		// The preview of a fillet is a real surface, not a ghost: the panel used to set this and
		// the interaction took the preview over with the rest.
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
	}

	FilletInteraction::FilletInteraction(FilletFeature* p_feature)
		: ShapeHelper(p_feature)
		, m_feature(p_feature)
	{
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
	}

	void FilletInteraction::ensureHandles()
	{
		if (m_handle1 != nullptr)
		{
			return;
		}
		// Made when they are first shown, like the pad's: a widget that spent its construction
		// hidden and is activated later is the one thing that path never does.
		m_handle1 = new AxisTranslationWidget("fillet");
		m_handle2 = new AxisTranslationWidget("fillet");
		// A widget that was only constructed is drawn - the renderer walks its own list -
		// but does not receive mouse events until it is enabled. The pad's handles enable
		// themselves in their constructor, which is the difference between the arrows that
		// could be dragged and these.
		m_handle1->setActive(true);
		m_handle2->setActive(true);
		m_lengthObserver1 = m_handle1->AddObserver(AxisTranslationEvent::LengthChange, this,
			&FilletInteraction::onHandle1Changed);
		m_lengthObserver2 = m_handle2->AddObserver(AxisTranslationEvent::LengthChange, this,
			&FilletInteraction::onHandle2Changed);
	}

	FilletInteraction::~FilletInteraction()
	{
		// The observers go while the handles they were put on are still there: removal only takes
		// the entry out of the list, the command behind it is this object's to free.
		if (m_lengthObserver1.command != nullptr)
		{
			m_handle1->RemoveObserver(m_lengthObserver1.tag);
			delete m_lengthObserver1.command;
			m_lengthObserver1 = { 0, nullptr };
		}
		if (m_lengthObserver2.command != nullptr)
		{
			m_handle2->RemoveObserver(m_lengthObserver2.tag);
			delete m_lengthObserver2.command;
			m_lengthObserver2 = { 0, nullptr };
		}
		delete m_handle1;
		delete m_handle2;
		m_handle1 = nullptr;
		m_handle2 = nullptr;
	}

	Feature* FilletInteraction::GetFeature()
	{
		return m_feature;
	}

	bool FilletInteraction::HasBegun() const
	{
		return m_started;
	}

	void FilletInteraction::SetVisible(bool p_visible)
	{
		if (p_visible)
		{
			ensureHandles();
		}
		for (AxisTranslationWidget* handle : { m_handle1, m_handle2 })
		{
			if (handle != nullptr)
			{
				handle->setVisible(p_visible);
				handle->setActive(p_visible);
			}
		}
	}

	void FilletInteraction::SetBeginCallback(std::function<void()> p_callback)
	{
		m_onBegin = std::move(p_callback);
	}

	void FilletInteraction::SetParamChangedCallback(std::function<void()> p_callback)
	{
		m_onParamChanged = std::move(p_callback);
	}

	void FilletInteraction::SetEdgesChangedCallback(std::function<void()> p_callback)
	{
		m_onEdgesChanged = std::move(p_callback);
	}

	void FilletInteraction::onSelectEdge(const std::vector<Part::TopoShape>& p_edge)
	{
		// The picked edge is added to the fillet, which is the same thing the panel's "Add from
		// Selection" button does with the selection - without the extra click, since a pick is
		// exactly what the user means by it. While the operation is only offered there is no
		// panel and no list, and a pick there is the launcher's (it replaces the offer).
		if (!m_started || m_feature == nullptr)
		{
			return;
		}
		Feature* base = nullptr;
		std::vector<std::string> picked;
		if (!ViewTool::getSelectedBasedFeature(base, picked) || base == nullptr)
		{
			return;
		}
		if (base != m_feature->getBaseFeature())
		{
			// A fillet runs along edges of the one shape it is built on: an edge of another
			// feature is not something it could carry.
			return;
		}
		std::vector<std::string> references = m_feature->getSubValues();
		for (const std::string& reference : picked)
		{
			if (reference.rfind("Edge_", 0) != 0)
			{
				continue;  // a fillet runs along edges; a face or a whole object is not one
			}
			if (std::find(references.begin(), references.end(), reference) == references.end())
			{
				references.push_back(reference);
			}
		}
		if (references.size() == m_feature->getSubValues().size())
		{
			return;  // nothing new was selected
		}
		// The mapped names stay with their reference: the ones already there are kept, the new
		// edges start without any and are resolved - and named - on the next build.
		std::vector<std::vector<std::string>> names = m_feature->getReferenceNames();
		names.resize(references.size());
		m_feature->setSubValues(references);
		m_feature->setReferenceNames(names);
		// The list first, the preview second: that is the order the panel's own button path has,
		// and it leaves the list right even if the kernel refuses the new edge.
		if (m_onEdgesChanged)
		{
			m_onEdgesChanged();
		}
		UpdatePreview();
	}

	void FilletInteraction::Attach(const Part::TopoShape& p_edge)
	{
		if (p_edge.isNull())
		{
			return;
		}
		ensureHandles();
		// Attached to the shape below, not executed: enough to resolve the edge and where the
		// arrows belong, and nothing is shown of it until the user starts one.
		if (m_feature == nullptr)
		{
			Feature* baseFeature = nullptr;
			std::vector<std::string> subValues;
			if (!ViewTool::getSelectedBasedFeature(baseFeature, subValues) || baseFeature == nullptr)
			{
				CORE_WARN("[FilletInteraction] the picked edge belongs to no feature");
				return;
			}
			try
			{
				m_feature = new FilletFeature("Fillet");
				m_createdFeature = true;
				m_feature->setBaseFeature(baseFeature);
				m_feature->setSubValues(subValues);
				m_feature->useAllEdges = false;
				setFeature(m_feature);
				setGenerateShapeName("FilletShape");

				const Part::TopoShape baseShape = m_feature->getBaseTopoShape();
				const std::vector<Part::TopoShape> edges = m_feature->getBaseTopoEdgeShapes();
				const double diagonal = baseShape.getBoundBoxOptimal().CalcDiagonalLength();
				m_feature->len = diagonal * 0.01;
				m_minBuildRadius = m_feature->len;
				// The identity value, like a pad's zero length: a fillet of no radius is the shape
				// as it already is. Nothing is previewed yet, and the radius the first drag has
				// reached is what it starts from - so it grows out of the original edge instead of
				// appearing at some radius chosen for the user.
				m_feature->radius = 0.0f;
			}
			catch (const Standard_Failure& e)
			{
				CORE_ERROR("[FilletInteraction] {0}", e.GetMessageString());
			}
			catch (const std::exception& e)
			{
				CORE_ERROR("[FilletInteraction] {0}", e.what());
			}
		}
		if (m_feature != nullptr && placeHandles())
		{
			SetVisible(true);
		}
	}

	void FilletInteraction::Begin()
	{
		if (m_started)
		{
			return;
		}
		if (m_feature == nullptr && !buildFeature())
		{
			return;
		}
		// The radius is what the drag has already reached (set by setRadiusFrom before this runs),
		// so the result grows from the edge the user picked. A fillet opened from the task bar
		// brought its own starting radius instead, from buildFeature.
		if (m_feature->radius <= 0.0)
		{
			m_feature->radius = kSmallestRadius;
		}
		m_started = true;
		roundByCurrentRadius();
		if (m_onBegin)
		{
			m_onBegin();
		}
	}

	void FilletInteraction::Commit()
	{
		if (m_feature == nullptr)
		{
			return;
		}
		generateFinalShape();
		m_createdFeature = false;
	}

	void FilletInteraction::Abort()
	{
		if (m_feature != nullptr)
		{
			clearPreviewShape();
			if (m_createdFeature)
			{
				m_feature->RemoveFromScene();
				setFeature(nullptr);
				delete m_feature;
				m_createdFeature = false;
			}
			m_feature = nullptr;
		}
		m_started = false;
		SetVisible(false);
	}

	void FilletInteraction::onHandle1Changed()
	{
		setRadiusFrom(m_handle1);
	}

	void FilletInteraction::onHandle2Changed()
	{
		setRadiusFrom(m_handle2);
	}

	void FilletInteraction::setRadiusFrom(AxisTranslationWidget* p_handle)
	{
		if (p_handle == nullptr)
		{
			return;
		}
		// Nothing has been built yet: the drag that reaches here is the one that starts the task,
		// and the radius it has reached is what it starts from.
		if (!m_started)
		{
			if (m_feature != nullptr)
			{
				m_feature->radius = static_cast<double>(p_handle->getLength()) * m_radiusPerUnit;
			}
			Begin();
			return;
		}
		if (m_feature == nullptr)
		{
			return;
		}
		m_feature->radius = static_cast<double>(p_handle->getLength()) * m_radiusPerUnit;
		// The other arrow is the same radius on the other side of the edge.
		const float travel = static_cast<float>(m_feature->radius / m_radiusPerUnit);
		if (p_handle == m_handle1 && m_handle2 != nullptr)
		{
			m_handle2->setLength(travel);
		}
		else if (p_handle == m_handle2 && m_handle1 != nullptr)
		{
			m_handle1->setLength(travel);
		}
		roundByCurrentRadius();
		if (m_onParamChanged)
		{
			m_onParamChanged();
		}
	}

	void FilletInteraction::SetRadius(double p_radius)
	{
		if (m_feature == nullptr)
		{
			return;
		}
		m_feature->radius = static_cast<float>(p_radius);
		const float travel = static_cast<float>(m_feature->radius / m_radiusPerUnit);
		m_handle1->setLength(travel);
		m_handle2->setLength(travel);
	}

	double FilletInteraction::GetRadius() const
	{
		return m_feature != nullptr ? m_feature->radius : 0.0;
	}

	void FilletInteraction::UpdatePreview()
	{
		if (m_started)
		{
			roundByCurrentRadius();
		}
	}

	bool FilletInteraction::roundByCurrentRadius()
	{
		if (m_feature == nullptr)
		{
			return false;
		}
		if (static_cast<double>(m_feature->radius) < m_minBuildRadius)
		{
			// The identity state: nothing is rounded, so the shape the user already sees is the
			// right answer. Asking the kernel for a fillet of nothing only produces errors.
			clearPreviewShape();
			return false;
		}
		// A radius can still be too large for the edge it runs along. execute() only touches the
		// shape when it succeeds, so trying smaller ones leaves the last valid result in place.
		int shrinkGuards = 0;
		while (m_feature->radius > m_minBuildRadius * 0.5f && !m_feature->execute())
		{
			m_feature->radius *= 0.5f;
			if (++shrinkGuards > 6)
			{
				break;
			}
		}
		if (shrinkGuards > 6 || m_feature->radius < m_minBuildRadius * 0.5f)
		{
			return false;
		}
		previewShape();
		return true;
	}

	bool FilletInteraction::placeHandles()
	{
		ensureHandles();
		if (m_feature == nullptr)
		{
			return false;
		}
		Part::TopoShape baseShape = m_feature->getBaseTopoShape();
		const std::vector<Part::TopoShape> edges = m_feature->getBaseTopoEdgeShapes();
		if (edges.empty() || edges[0].isNull())
		{
			return false;
		}
		Part::TopoShape edge = edges[0];
		auto [face1, face2] = getAdjacentFacesFromEdge(edge, baseShape);
		const DraggerPlacementProps props1 = getDraggerPlacementFromEdgeAndFace(edge, face1);
		const DraggerPlacementProps props2 = getDraggerPlacementFromEdgeAndFace(edge, face2);
		m_feature->origin1[0] = props1.position.x;
		m_feature->origin1[1] = props1.position.y;
		m_feature->origin1[2] = props1.position.z;
		m_feature->dir1[0] = props1.dir.x;
		m_feature->dir1[1] = props1.dir.y;
		m_feature->dir1[2] = props1.dir.z;
		m_feature->origin2[0] = props2.position.x;
		m_feature->origin2[1] = props2.position.y;
		m_feature->origin2[2] = props2.position.z;
		m_feature->dir2[0] = props2.dir.x;
		m_feature->dir2[1] = props2.dir.y;
		m_feature->dir2[2] = props2.dir.z;

		// No extra world scale: the arrows are sized to a fixed number of pixels by the widget
		// itself (see AxisTranslationWidget::onUpdate), and scaling their geometry by a share of
		// the model's size on top of that made them small and different from model to model.
		// How much radius one unit of arrow travel buys, from the angle between the two directions
		// the arrows walk along. The tangency point of the fillet sits at r / tan(theta/2) from the
		// edge, so an angle other than a right one needs cot(theta/2) more travel for the same
		// radius.
		//
		// Exact for two flat faces meeting along a straight edge. On a curved edge (a cylinder
		// meeting a plane, say) these two directions are not the tangents of flat faces and the
		// relation is not linear at all - there the arrow is a scalar control, the way FreeCAD's
		// dress-up dragger is, and the panel's radius is the number that counts.
		const double cosAngle = std::clamp(
			static_cast<double>(props1.dir.x) * props2.dir.x
				+ static_cast<double>(props1.dir.y) * props2.dir.y
				+ static_cast<double>(props1.dir.z) * props2.dir.z,
			-1.0, 1.0);
		m_radiusPerUnit = std::max(std::tan(std::acos(cosAngle) * 0.5), 1.0e-3);

		m_handle1->setUpScale(1.0f);
		m_handle2->setUpScale(1.0f);
		m_handle1->setUpOrigin(m_feature->origin1[0], m_feature->origin1[1], m_feature->origin1[2]);
		m_handle1->setUpDir(m_feature->dir1[0], m_feature->dir1[1], m_feature->dir1[2]);
		m_handle2->setUpOrigin(m_feature->origin2[0], m_feature->origin2[1], m_feature->origin2[2]);
		m_handle2->setUpDir(m_feature->dir2[0], m_feature->dir2[1], m_feature->dir2[2]);
		m_handle1->setLength(static_cast<float>(m_feature->radius / m_radiusPerUnit));
		m_handle2->setLength(static_cast<float>(m_feature->radius / m_radiusPerUnit));
		return true;
	}

	bool FilletInteraction::buildFeature()
	{
		// Opened from the task bar: the edges are whatever the viewport has selected.
		Feature* baseFeature = nullptr;
		std::vector<std::string> subValues;
		if (!ViewTool::getSelectedBasedFeature(baseFeature, subValues) || baseFeature == nullptr)
		{
			CORE_WARN("[FilletInteraction] there is no shape to fillet");
			return false;
		}
		try
		{
			m_feature = new FilletFeature("Fillet");
			m_createdFeature = true;
			m_feature->setBaseFeature(baseFeature);
			m_feature->setSubValues(subValues);
			setFeature(m_feature);
			setGenerateShapeName("FilletShape");

			Part::TopoShape baseShape = m_feature->getBaseTopoShape();
			const std::vector<Part::TopoShape> edges = m_feature->getBaseTopoEdgeShapes();
			const double diagonal = baseShape.getBoundBoxOptimal().CalcDiagonalLength();
			m_feature->len = diagonal * 0.01;
			m_minBuildRadius = m_feature->len;
			m_feature->radius = safeInitialFilletRadius(edges, diagonal);
		}
		catch (const Standard_Failure& e)
		{
			CORE_ERROR("[FilletInteraction] {0}", e.GetMessageString());
			return false;
		}
		catch (const std::exception& e)
		{
			CORE_ERROR("[FilletInteraction] {0}", e.what());
			return false;
		}
		placeHandles();
		SetVisible(true);
		return true;
	}
}
