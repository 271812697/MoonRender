#include "editor/UI/TaskPanel/TaskSession.h"

#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "editor/UI/TaskPanel/BaseTaskDialog.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"

namespace MOON
{
	TaskSession::TaskSession(TaskViewWidget& p_host)
		: m_host(p_host)
	{
	}

	TaskSession::~TaskSession()
	{
		DropOffer();
	}

	TaskSession::State TaskSession::GetState() const
	{
		return m_state;
	}

	FeatureInteraction* TaskSession::GetInteraction() const
	{
		return m_interaction.get();
	}

	void TaskSession::Offer(std::unique_ptr<FeatureInteraction> p_interaction,
		const Part::TopoShape& p_picked, PanelFactory p_panel)
	{
		DropOffer();
		if (!p_interaction)
		{
			return;
		}
		m_interaction = std::move(p_interaction);
		m_panel = std::move(p_panel);
		m_interaction->SetBeginCallback([this]() { onBegan(); });
		m_interaction->Attach(p_picked);
		// An operation that has already built itself (editing one that exists) is active from the
		// start, and its panel has already been asked for by the begin callback.
		m_state = m_interaction ? (m_interaction->HasBegun() ? State::Active : State::Offering)
			: State::Idle;
	}

	void TaskSession::Start(std::unique_ptr<FeatureInteraction> p_interaction, PanelFactory p_panel)
	{
		DropOffer();
		if (!p_interaction)
		{
			return;
		}
		m_interaction = std::move(p_interaction);
		m_panel = std::move(p_panel);
		m_interaction->SetBeginCallback([this]() { onBegan(); });
		m_interaction->Begin();
		if (m_interaction)
		{
			// Nothing opened the panel (an interaction that builds nothing): ask for it directly.
			onBegan();
		}
	}

	void TaskSession::DropOffer()
	{
		if (m_interaction)
		{
			m_interaction->Abort();
			m_interaction.reset();
		}
		m_panel = nullptr;
		m_state = State::Idle;
	}

	void TaskSession::onBegan()
	{
		if (!m_interaction || m_state == State::Active || !m_panel)
		{
			return;
		}
		// The panel takes the interaction over: both ends of the task - the handles the user drags
		// and the numbers the panel edits - are then the same object.
		m_state = State::Active;
		BaseTaskDialog* panel = m_panel(std::move(m_interaction));
		if (panel != nullptr)
		{
			m_host.setTaskDialog(panel);
		}
	}
}
