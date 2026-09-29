#pragma once
#include <QWidget>

#include <functional>
#include <vector>

#include "feature/TransformMode.h"

namespace MOON
{
	class Feature;

	/** Puts the feature the transform sits on into its list of originals when the
	 * list is still empty.
	 *
	 * The shape below the transform is the one the user built last, so patterning
	 * its material is what they would pick first anyway: this way the panel shows a
	 * result right away instead of waiting for a first pick. Whole mode does not look
	 * at the list at all, and a feature that has no material of its own (a sketch, a
	 * datum, another transform) is left out.
	 */
	void AddBaseFeatureAsDefaultOriginal(
		Feature& p_owner,
		int p_mode,
		std::vector<Feature*>& p_originals
	);

	/** The list of features a transform feature works on in its "feature" mode.
	 *
	 * It shows the features that were picked, offers to add the one selected in the
	 * scene and to remove the row that is current, and it only accepts a feature
	 * that is part of the body below the transform and that has material of its own
	 * to transform: a pad, a pocket, a revolve, a fillet or a chamfer, but not a
	 * sketch, a datum, or another transform feature.
	 *
	 * The whole shape mode does not use the list, but the list stays visible so that
	 * switching the mode back and forth does not lose the selection.
	 */
	class OriginalsList : public QWidget
	{
	public:
		OriginalsList(
			Feature& p_owner,
			std::vector<Feature*>& p_originals,
			std::function<void()> p_onChanged,
			QWidget* parent = nullptr
		);
		~OriginalsList() override;

		/** Rebuilds the rows from the features the transform holds. */
		void refresh();

	private:
		void addSelected();
		void removeCurrent();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
