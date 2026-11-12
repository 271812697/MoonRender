#include "editor/UI/TaskPanel/SelectionTaskLauncher.h"

#include <memory>
#include <string>
#include <utility>

#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

#include "TopoShape.h"
#include "core/SelectionManager.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "feature/FeatureBody.h"
#include "feature/Feature.h"
#include "editor/Language.h"
#include "editor/UI/TaskPanel/TaskSession.h"
#include "editor/UI/TaskPanel/TaskOfferFlyout.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "editor/UI/TaskPanel/PadInteraction.h"
#include "editor/UI/TaskPanel/ExtrudeTaskDialog.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "editor/UI/TaskPanel/FilletInteraction.h"
#include "editor/UI/TaskPanel/FilletTask.h"
#include "editor/UI/TaskPanel/ChamferInteraction.h"
#include "editor/UI/TaskPanel/ChamferTask.h"

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

		/** An edge of a body is what a fillet or a chamfer runs along. */
		bool offersEdge(const Part::TopoShape& p_picked)
		{
			return !p_picked.isNull() && p_picked.getShape().ShapeType() == TopAbs_EDGE;
		}

		std::unique_ptr<FeatureInteraction> makeFillet()
		{
			return std::make_unique<FilletInteraction>();
		}

		std::unique_ptr<FeatureInteraction> makeChamfer()
		{
			return std::make_unique<ChamferInteraction>();
		}

		/** The operations a pick can offer, in the order they are tried.
		 *
		 * A row is all it takes: what the operation accepts, how its interaction is made in the
		 * offered state, and the panel that then edits it. An edge offers two rows, which is what
		 * the switcher at the cursor is for; anything else in the picking path is untouched.
		 */
		const TaskOfferRule kOfferRules[] = {
			{ "Pad", ":/widgets/icons/model32/PartDesign_Pad.svg",
				offersPad, makePad, &MakePadPanel },
			{ "Fillet", ":/widgets/icons/model32/PartDesign_Fillet.svg",
				offersEdge, makeFillet, &MakeFilletPanel },
			{ "Chamfer", ":/widgets/icons/model32/PartDesign_Chamfer.svg",
				offersEdge, makeChamfer, &MakeChamferPanel },
		};

		/** Every operation the picked sub-shape offers, in the order of the table. */
		std::vector<const TaskOfferRule*> collectRules(const Part::TopoShape& p_picked)
		{
			std::vector<const TaskOfferRule*> rules;
			for (const TaskOfferRule& rule : kOfferRules)
			{
				if (rule.matches(p_picked))
				{
					rules.push_back(&rule);
				}
			}
			return rules;
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
		const std::vector<const TaskOfferRule*> rules = collectRules(shapes[1]);
		if (rules.empty())
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
		// Which operation this user meant: remembered per kind of sub-shape, asked when the pick
		// really has alternatives.
		const bool isEdge = shapes[1].getShape().ShapeType() == TopAbs_EDGE;
		const char*& remembered = isEdge ? m_lastEdgeRule : m_lastFaceRule;
		const TaskOfferRule* chosen = rules.front();
		if (remembered != nullptr)
		{
			for (const TaskOfferRule* rule : rules)
			{
				if (std::string(rule->name) == remembered)
				{
					chosen = rule;
					break;
				}
			}
		}
		if (rules.size() > 1 && !chooseRuleAtCursor(rules, chosen))
		{
			// The user dismissed the switcher: nothing is offered.
			m_session.DropOffer();
			m_handledPick = 0;
			return;
		}
		remembered = chosen->name;

		CORE_INFO("[SelectionTaskLauncher] offering '{0}' on the picked shape", chosen->name);
		m_starting = true;
		m_session.Offer(chosen->makeInteraction(), shapes[1], chosen->panel);
		m_starting = false;
		// A pick that is replaced forgets the old one, so that picking the same shape again once
		// the offer is gone offers it again - SelectionManager does not repeat a selection that
		// did not change, so nothing else would.
		if (m_session.GetState() != TaskSession::State::Offering)
		{
			m_handledPick = 0;
		}
	}

	bool SelectionTaskLauncher::chooseRuleAtCursor(
		const std::vector<const TaskOfferRule*>& p_rules, const TaskOfferRule*& p_chosen)
	{
		// The flyout is the switcher: the operations that can be built on what was just picked,
		// right where it was picked, with the one this user chose last time lit. The names go in
		// in the language the ribbon is set to, so the two are the same button.
		std::vector<OfferChoice> choices;
		choices.reserve(p_rules.size());
		int current = 0;
		for (size_t i = 0; i < p_rules.size(); ++i)
		{
			const TaskOfferRule* rule = p_rules[i];
			choices.push_back({ Language::Localize(QString::fromUtf8(rule->name)),
				QString::fromUtf8(rule->icon) });
			if (rule == p_chosen)
			{
				current = static_cast<int>(i);
			}
		}
		TaskOfferFlyout flyout(std::move(choices), current);
		const int picked = flyout.run();
		if (picked < 0 || picked >= static_cast<int>(p_rules.size()))
		{
			return false;
		}
		p_chosen = p_rules[static_cast<size_t>(picked)];
		return true;
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
