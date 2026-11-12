#include "editor/UI/TaskPanel/ChamferInteraction.h"

#include <algorithm>
#include <exception>

#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>

#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "feature/ChamferFeature.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Widgets/AxisTranslationWidget.h"
#include "base/BoundBox.h"
#include "App/GizmoHelper.h"

namespace MOON
{
	namespace
	{
		/** The smallest size a chamfer may be built at: it only has to be non-zero, an edge
		 * cannot be bevelled by nothing. */
		constexpr double kSmallestSize = 1.0e-3;

		/** The size a chamfer starts at when it is opened from the task bar: a share of the shape
		 * it is cut into, small enough to fit any edge of it (see the old ChamferTask). */
		constexpr double kDefaultSizeShare = 0.03;
	}

	ChamferInteraction::ChamferInteraction()
		: ShapeHelper(nullptr)
	{
		// The preview of a chamfer is a real surface, not a ghost: the panel used to set this and
		// the interaction took the preview over with the rest.
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
	}

	ChamferInteraction::ChamferInteraction(ChamferFeature* p_feature)
		: ShapeHelper(p_feature)
		, m_feature(p_feature)
	{
		mPreviewOption.isTransparent = false;
		mPreviewOption.isBlend = true;
		mPreviewOption.useDomainColor = false;
	}

	void ChamferInteraction::ensureHandles()
	{
		if (m_handle1 != nullptr)
		{
			return;
		}
		// Made when they are first shown, like the pad's and the fillet's: a widget that spent its
		// construction hidden and is activated later is the one thing that path never does.
		m_handle1 = new AxisTranslationWidget("chamfer");
		m_handle2 = new AxisTranslationWidget("chamfer");
		// A widget that was only constructed is drawn - the renderer walks its own list - but does
		// not receive mouse events until it is enabled. Both of the "the arrow cannot be dragged"
		// bugs came from here.
		m_handle1->setActive(true);
		m_handle2->setActive(true);
		m_lengthObserver1 = m_handle1->AddObserver(AxisTranslationEvent::LengthChange, this,
			&ChamferInteraction::onHandle1Changed);
		m_lengthObserver2 = m_handle2->AddObserver(AxisTranslationEvent::LengthChange, this,
			&ChamferInteraction::onHandle2Changed);
	}

	ChamferInteraction::~ChamferInteraction()
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

	Feature* ChamferInteraction::GetFeature()
	{
		return m_feature;
	}

	bool ChamferInteraction::HasBegun() const
	{
		return m_started;
	}

	void ChamferInteraction::SetVisible(bool p_visible)
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

	void ChamferInteraction::SetBeginCallback(std::function<void()> p_callback)
	{
		m_onBegin = std::move(p_callback);
	}

	void ChamferInteraction::SetParamChangedCallback(std::function<void()> p_callback)
	{
		m_onParamChanged = std::move(p_callback);
	}

	void ChamferInteraction::SetEdgesChangedCallback(std::function<void()> p_callback)
	{
		m_onEdgesChanged = std::move(p_callback);
	}

	void ChamferInteraction::onSelectEdge(const std::vector<Part::TopoShape>& p_edge)
	{
		// The picked edge is added to the chamfer, which is the same thing the panel's "Add from
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
			// A chamfer runs along edges of the one shape it is built on: an edge of another
			// feature is not something it could carry.
			return;
		}
		std::vector<std::string> references = m_feature->getSubValues();
		for (const std::string& reference : picked)
		{
			if (reference.rfind("Edge_", 0) != 0)
			{
				continue;  // a chamfer runs along edges; a face or a whole object is not one
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

	void ChamferInteraction::Attach(const Part::TopoShape& p_edge)
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
				CORE_WARN("[ChamferInteraction] the picked edge belongs to no feature");
				return;
			}
			try
			{
				m_feature = new ChamferFeature("Chamfer");
				m_createdFeature = true;
				m_feature->setBaseFeature(baseFeature);
				m_feature->setSubValues(subValues);
				m_feature->useAllEdges = false;
				setFeature(m_feature);
				setGenerateShapeName("ChamferShape");

				const Part::TopoShape baseShape = m_feature->getBaseTopoShape();
				const double diagonal = baseShape.getBoundBoxOptimal().CalcDiagonalLength();
				// The identity value, like a fillet of no radius: a chamfer of no size is the shape
				// as it already is. Nothing is previewed yet, and the size the first drag reaches
				// is what it starts from - so it grows out of the original edge instead of
				// appearing at some size chosen for the user.
				m_feature->len = diagonal * 0.01;
				// Never zero: a chamfer of nothing is not something the kernel can build, and a
				// threshold of zero would let it be asked for.
				m_minBuildSize = std::max(static_cast<double>(m_feature->len), kSmallestSize);
				// The second size is the panel's to set: it is only read by the two-distances
				// type, and the feature brings its own default for it.
				m_feature->size = 0.0f;
			}
			catch (const Standard_Failure& e)
			{
				CORE_ERROR("[ChamferInteraction] {0}", e.GetMessageString());
			}
			catch (const std::exception& e)
			{
				CORE_ERROR("[ChamferInteraction] {0}", e.what());
			}
		}
		if (m_feature != nullptr && placeHandles())
		{
			SetVisible(true);
		}
	}

	void ChamferInteraction::Begin()
	{
		if (m_started)
		{
			return;
		}
		if (m_feature == nullptr && !buildFeature())
		{
			return;
		}
		// The size is what the drag has already reached (set by setSizeFrom before this runs), so
		// the result grows from the edge the user picked. A chamfer opened from the task bar, or
		// edited after being built, brought its own size instead.
		if (m_feature->size <= 0.0)
		{
			m_feature->size = kSmallestSize;
		}
		// A chamfer that already exists has no arrows yet - the edit path opens on it without a
		// pick to attach to - and one offered on a pick got them in Attach already. Placing them
		// again is idempotent: same edge, same directions, the size the feature holds.
		if (placeHandles())
		{
			SetVisible(true);
		}
		m_started = true;
		chamferByCurrentSize();
		if (m_onBegin)
		{
			m_onBegin();
		}
	}

	void ChamferInteraction::Commit()
	{
		if (m_feature == nullptr)
		{
			return;
		}
		generateFinalShape();
		m_createdFeature = false;
	}

	void ChamferInteraction::Abort()
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

	void ChamferInteraction::onHandle1Changed()
	{
		setSizeFrom(m_handle1);
	}

	void ChamferInteraction::onHandle2Changed()
	{
		setSizeFrom(m_handle2);
	}

	void ChamferInteraction::setSizeFrom(AxisTranslationWidget* p_handle)
	{
		if (p_handle == nullptr)
		{
			return;
		}
		// Nothing has been built yet: the drag that reaches here is the one that starts the task,
		// and the size it has reached is what it starts from.
		if (!m_started)
		{
			if (m_feature != nullptr)
			{
				m_feature->size = static_cast<float>(p_handle->getLength() * m_sizePerUnit);
			}
			Begin();
			return;
		}
		if (m_feature == nullptr)
		{
			return;
		}
		m_feature->size = static_cast<float>(p_handle->getLength() * m_sizePerUnit);
		// The other arrow is the same size on the other side of the edge.
		const float travel = static_cast<float>(m_feature->size / m_sizePerUnit);
		if (p_handle == m_handle1 && m_handle2 != nullptr)
		{
			m_handle2->setLength(travel);
		}
		else if (p_handle == m_handle2 && m_handle1 != nullptr)
		{
			m_handle1->setLength(travel);
		}
		chamferByCurrentSize();
		if (m_onParamChanged)
		{
			m_onParamChanged();
		}
	}

	void ChamferInteraction::SetSize(double p_size)
	{
		if (m_feature == nullptr)
		{
			return;
		}
		m_feature->size = static_cast<float>(p_size);
		const float travel = static_cast<float>(m_feature->size / m_sizePerUnit);
		if (m_handle1 != nullptr)
		{
			m_handle1->setLength(travel);
		}
		if (m_handle2 != nullptr)
		{
			m_handle2->setLength(travel);
		}
	}

	double ChamferInteraction::GetSize() const
	{
		return m_feature != nullptr ? m_feature->size : 0.0;
	}

	void ChamferInteraction::UpdatePreview()
	{
		if (m_started)
		{
			chamferByCurrentSize();
		}
	}

	bool ChamferInteraction::chamferByCurrentSize()
	{
		if (m_feature == nullptr)
		{
			return false;
		}
		if (static_cast<double>(m_feature->size) < m_minBuildSize)
		{
			// The identity state: nothing is chamfered, so the shape the user already sees is the
			// right answer. Asking the kernel for a chamfer of nothing only produces errors.
			clearPreviewShape();
			return false;
		}
		// A size can still be too large for the edge it runs along. execute() only touches the
		// shape when it succeeds, so trying smaller ones leaves the last valid result in place.
		int shrinkGuards = 0;
		while (m_feature->size > m_minBuildSize * 0.5f && !m_feature->execute())
		{
			m_feature->size *= 0.5f;
			if (++shrinkGuards > 6)
			{
				break;
			}
		}
		if (shrinkGuards > 6 || m_feature->size < m_minBuildSize * 0.5f)
		{
			return false;
		}
		previewShape();
		return true;
	}

	bool ChamferInteraction::placeHandles()
	{
		ensureHandles();
		if (m_feature == nullptr)
		{
			return false;
		}
		// Resolving the edge and the two faces next to it is where an exception would come from
		// (a chamfer whose edges no longer resolve is still an object the user can open), so the
		// whole placement is guarded - a task without arrows is worse than one without a preview,
		// but an exception out of here ends the application.
		try
		{
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
		}
		catch (const Standard_Failure& e)
		{
			CORE_ERROR("[ChamferInteraction] {0}", e.GetMessageString());
			return false;
		}
		catch (const std::exception& e)
		{
			CORE_ERROR("[ChamferInteraction] {0}", e.what());
			return false;
		}
		// No extra world scale: the arrows are sized to a fixed number of pixels by the widget
		// itself (see AxisTranslationWidget::onUpdate), and scaling their geometry by a share of
		// the model's size on top of that made them small and different from model to model.
		//
		// The arrow walks along the face next to the edge, and the point where the chamfer face
		// meets that face is exactly the size away from the edge (m_sizePerUnit, see the header).
		m_handle1->setUpScale(1.0f);
		m_handle2->setUpScale(1.0f);
		m_handle1->setUpOrigin(m_feature->origin1[0], m_feature->origin1[1], m_feature->origin1[2]);
		m_handle1->setUpDir(m_feature->dir1[0], m_feature->dir1[1], m_feature->dir1[2]);
		m_handle2->setUpOrigin(m_feature->origin2[0], m_feature->origin2[1], m_feature->origin2[2]);
		m_handle2->setUpDir(m_feature->dir2[0], m_feature->dir2[1], m_feature->dir2[2]);
		m_handle1->setLength(static_cast<float>(m_feature->size / m_sizePerUnit));
		m_handle2->setLength(static_cast<float>(m_feature->size / m_sizePerUnit));
		return true;
	}

	bool ChamferInteraction::buildFeature()
	{
		// Opened from the task bar: the edges are whatever the viewport has selected.
		Feature* baseFeature = nullptr;
		std::vector<std::string> subValues;
		if (!ViewTool::getSelectedBasedFeature(baseFeature, subValues) || baseFeature == nullptr)
		{
			CORE_WARN("[ChamferInteraction] there is no shape to chamfer");
			return false;
		}
		try
		{
			m_feature = new ChamferFeature("Chamfer");
			m_createdFeature = true;
			m_feature->setBaseFeature(baseFeature);
			m_feature->setSubValues(subValues);
			setFeature(m_feature);
			setGenerateShapeName("ChamferShape");

			const Part::TopoShape baseShape = m_feature->getBaseTopoShape();
			const double diagonal = baseShape.getBoundBoxOptimal().CalcDiagonalLength();
			m_feature->len = diagonal * 0.01;
			m_minBuildSize = std::max(static_cast<double>(m_feature->len), kSmallestSize);
			m_feature->size = static_cast<float>(diagonal * kDefaultSizeShare);
		}
		catch (const Standard_Failure& e)
		{
			CORE_ERROR("[ChamferInteraction] {0}", e.GetMessageString());
			return false;
		}
		catch (const std::exception& e)
		{
			CORE_ERROR("[ChamferInteraction] {0}", e.what());
			return false;
		}
		return true;
	}
}
