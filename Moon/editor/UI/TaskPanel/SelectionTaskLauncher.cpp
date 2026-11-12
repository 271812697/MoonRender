#include "editor/UI/TaskPanel/SelectionTaskLauncher.h"

#include <memory>

#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

#include "TopoShape.h"
#include "core/SelectionManager.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "feature/FeatureBody.h"
#include "feature/Feature.h"
#include "editor/UI/TaskPanel/TaskSession.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "editor/UI/TaskPanel/PadInteraction.h"
#include "editor/UI/TaskPanel/ExtrudeTaskDialog.h"
#include "editor/UI/TaskPanel/FilletInteraction.h"
#include "editor/UI/TaskPanel/FilletTask.h"

namespace MOON
{
	namespace
	{
		/** A planar face is a profile a pad can be grown from. */
		bool offersPad(const Part::TopoShape& p_picked)
		{
			if (p_picked.isNull() || p_picked.getShape().ShapeType() != TopAbs_FACE)
			{
				return false;
			}
			gp_Pln plane;
			return p_picked.findPlane(plane);
		}

		std::unique_ptr<FeatureInteraction> makePad()
		{
			return std::make_unique<PadInteraction>(ExtrudeType::Additive);
		}

		/** An edge of a body is what a fillet runs along. */
		bool offersFillet(const Part::TopoShape& p_picked)
		{
			return !p_picked.isNull() && p_picked.getShape().ShapeType() == TopAbs_EDGE;
		}

		std::unique_ptr<FeatureInteraction> makeFillet()
		{
			return std::make_unique<FilletInteraction>();
		}

		/** The operations a pick can offer, in the order they are tried.
		 *
		 * A row is all it takes: what the operation accepts, how its interaction is made in the
		 * offered state, and the panel that then edits it. A fillet would be
		 * { "Fillet", offersFillet (an edge of a body), makeFillet, &MakeFilletPanel }, with
		 * nothing else in the picking path to touch.
		 */
		const TaskOfferRule kOfferRules[] = {
			{ "Pad", offersPad, makePad, &MakePadPanel },
			{ "Fillet", offersFillet, makeFillet, &MakeFilletPanel },
		};

		const TaskOfferRule* findRule(const Part::TopoShape& p_picked)
		{
			for (const TaskOfferRule& rule : kOfferRules)
			{
				if (rule.matches(p_picked))
				{
					return &rule;
				}
			}
			return nullptr;
		}
	}

	SelectionTaskLauncher::SelectionTaskLauncher(TaskSession& p_session, TaskViewWidget& p_host)
		: m_session(p_session)
		, m_host(p_host)
	{
		m_observer = SelectionManager::instance()
			.AddObserver(SelectAny, this, &SelectionTaskLauncher::onSelectionChanged);
		m_clearObserver = SelectionManager::instance()
			.AddObserver(ClearSelect, this, &SelectionTaskLauncher::onSelectionCleared);
	}

	SelectionTaskLauncher::~SelectionTaskLauncher()
	{
		if (m_observer.command != nullptr)
		{
			SelectionManager::instance().RemoveObserver(m_observer.tag);
			delete m_observer.command;
			m_observer = { 0, nullptr };
		}
		if (m_clearObserver.command != nullptr)
		{
			SelectionManager::instance().RemoveObserver(m_clearObserver.tag);
			delete m_clearObserver.command;
			m_clearObserver = { 0, nullptr };
		}
	}

	void SelectionTaskLauncher::onSelectionChanged()
	{
		if (m_starting)
		{
			return;
		}
		// The same pick arrives here again when the dispatcher restarts its list (building an
		// interaction adds an observer to this very event), so the pick this has acted on is
		// remembered: offering the same face twice would replace the offer with itself.
		const ::Core::ECS::Actor* picked = ViewTool::getLastestActorSelected();
		const size_t pickId = picked != nullptr ? picked->GetID() : 0;
		if (pickId == m_handledPick)
		{
			return;
		}
		m_handledPick = pickId;

		// A task that is open owns what is on screen. Its own panel and handles are what a pick
		// talks to now (up to face, a fillet's edge), so nothing is offered over it.
		if (m_host.hasTask())
		{
			return;
		}
		if (pickId == 0)
		{
			// Nothing selected: the offer has nothing to stand on.
			m_session.DropOffer();
			return;
		}
		std::vector<Part::TopoShape> shapes;
		if (!ViewTool::getSelectedTopoShape(shapes) || shapes.size() < 2)
		{
			m_session.DropOffer();
			return;
		}
		const TaskOfferRule* rule = findRule(shapes[1]);
		if (rule == nullptr)
		{
			m_session.DropOffer();
			return;
		}
		// The operation is built on the feature the pick belongs to, and it lands in the active
		// body: without either of them there is nothing to offer.
		Feature* owner = nullptr;
		std::vector<std::string> subValues;
		if (!ViewTool::getSelectedBasedFeature(owner, subValues) || owner == nullptr)
		{
			CORE_WARN("[SelectionTaskLauncher] the picked shape belongs to no feature; nothing offered");
			m_session.DropOffer();
			return;
		}
		if (FeatureBody::Active() == nullptr)
		{
			CORE_WARN("[SelectionTaskLauncher] there is no active body to model into");
			m_session.DropOffer();
			return;
		}
		CORE_INFO("[SelectionTaskLauncher] offering '{0}' on the picked shape", rule->name);
		m_starting = true;
		m_session.Offer(rule->makeInteraction(), shapes[1], rule->panel);
		m_starting = false;
		// A pick that is replaced forgets the old one, so that picking the same shape again once
		// the offer is gone offers it again - SelectionManager does not repeat a selection that
		// did not change, so nothing else would.
		if (m_session.GetState() != TaskSession::State::Offering)
		{
			m_handledPick = 0;
		}
	}

	void SelectionTaskLauncher::onSelectionCleared()
	{
		// The click went to empty space. The pick this launcher acted on is forgotten with the
		// selection, so picking the same shape again offers it again.
		m_handledPick = 0;
		if (!m_host.hasTask())
		{
			m_session.DropOffer();
		}
	}
}
