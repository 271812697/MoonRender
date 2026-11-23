#pragma once
#include <functional>
#include <memory>

namespace Part { class TopoShape; }

namespace MOON
{
	class TaskViewWidget;
	class FeatureInteraction;
	class BaseTaskDialog;

	/** The task that is going on in the viewport, in one place.
	 *
	 * A task starts in one of two ways and they differ only in when it begins:
	 *
	 *   from the task bar   - the operation is built at once, at its default size, and its panel
	 *                         opens with it;
	 *   from a pick         - the operation is *offered*: its handles are put on the picked face
	 *                         and nothing is built. The first drag of a handle is what begins it,
	 *                         at the size that drag has reached, and its panel opens then.
	 *
	 * The session is what holds the difference, and it knows nothing about which operation it is
	 * carrying: an interaction is whatever implements FeatureInteraction (see that header), and the
	 * panel is made by the factory handed in with it. Adding an operation therefore means writing
	 * its interaction and its panel and passing them here - no change to this class, to the pick
	 * launcher, or to the task bar.
	 */
	class TaskSession
	{
	public:
		enum class State
		{
			Idle,
			Offering,   // handles on the picked sub-shape, nothing built
			Active      // built, panel open
		};

		/** Builds the panel an operation is edited with, taking over its interaction. */
		using PanelFactory = std::function<BaseTaskDialog*(std::unique_ptr<FeatureInteraction>)>;

		explicit TaskSession(TaskViewWidget& p_host);
		~TaskSession();

		TaskSession(const TaskSession&) = delete;
		TaskSession& operator=(const TaskSession&) = delete;

		State GetState() const;

		/** Offers an operation on a picked sub-shape. Nothing is built until the user drags a
		 * handle; the factory is kept for when that happens. */
		void Offer(std::unique_ptr<FeatureInteraction> p_interaction, const Part::TopoShape& p_picked,
			PanelFactory p_panel);
		/** Starts an operation straight away, the way the task bar does. */
		void Start(std::unique_ptr<FeatureInteraction> p_interaction, PanelFactory p_panel);
		/** Drops whatever is being offered - the pick was replaced, or the click went elsewhere. */
		void DropOffer();

		/** The interaction being offered, or nullptr. */
		FeatureInteraction* GetInteraction() const;

	private:
		/** The offered interaction began: its panel is what the user needs now. */
		void onBegan();

		TaskViewWidget& m_host;
		/** The interaction while it is only offered; the panel owns it once the task is open. */
		std::unique_ptr<FeatureInteraction> m_interaction;
		PanelFactory m_panel;
		State m_state = State::Idle;
	};
}
