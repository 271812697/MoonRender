#include "editor/UI/TaskPanel/OriginalsList.h"

#include "feature/Feature.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace MOON
{
	namespace
	{
		/** True when p_candidate is one of the features the transform sits on. */
		bool isPartOfBodyBelow(Feature& p_owner, Feature* p_candidate)
		{
			for (Feature* f = p_owner.getBaseFeature(); f != nullptr; f = f->getBaseFeature()) {
				if (f == p_candidate) {
					return true;
				}
			}
			return false;
		}
	}

	void AddBaseFeatureAsDefaultOriginal(
		Feature& p_owner,
		int p_mode,
		std::vector<Feature*>& p_originals)
	{
		if (p_mode != static_cast<int>(TransformMode::Feature)) {
			return;
		}
		if (!p_originals.empty()) {
			return;
		}

		Feature* base = p_owner.getBaseFeature();
		if (base == nullptr || base->getToolShape().isNull()) {
			return;
		}
		p_originals.push_back(base);
	}

	class OriginalsList::Internal
	{
	public:
		Internal(
			OriginalsList* p_self,
			Feature& p_owner,
			std::vector<Feature*>& p_originals,
			std::function<void()> p_onChanged
		)
			: self(p_self)
			, owner(p_owner)
			, originals(p_originals)
			, onChanged(std::move(p_onChanged))
		{
		}

		OriginalsList* self = nullptr;
		Feature& owner;
		std::vector<Feature*>& originals;
		std::function<void()> onChanged;
		QListWidget* list = nullptr;
	};

	OriginalsList::OriginalsList(
		Feature& p_owner,
		std::vector<Feature*>& p_originals,
		std::function<void()> p_onChanged,
		QWidget* parent
	)
		: QWidget(parent)
		, mInternal(new Internal(this, p_owner, p_originals, std::move(p_onChanged)))
	{
		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(2);

		auto* row = new QHBoxLayout();
		row->setContentsMargins(0, 0, 0, 0);
		auto* addButton = new QToolButton(this);
		addButton->setText(tr("Add from Selection"));
		auto* removeButton = new QToolButton(this);
		removeButton->setText(tr("Remove"));
		row->addWidget(addButton);
		row->addWidget(removeButton);
		row->addStretch();
		layout->addLayout(row);

		mInternal->list = new QListWidget(this);
		mInternal->list->setMinimumHeight(80);
		mInternal->list->setStyleSheet(
			"QListWidget { background: transparent; border: none; outline: 0; }"
			"QListWidget::item { height: 20px; padding-left: 2px; }"
			"QListWidget::item:selected { background-color: #7ab2e8; color: white; }"
		);
		layout->addWidget(mInternal->list);

		QObject::connect(
			addButton, &QToolButton::clicked, this, [this]() { addSelected(); });
		QObject::connect(
			removeButton, &QToolButton::clicked, this, [this]() { removeCurrent(); });

		refresh();
	}

	OriginalsList::~OriginalsList()
	{
		delete mInternal;
	}

	void OriginalsList::refresh()
	{
		if (mInternal->list == nullptr) {
			return;
		}
		mInternal->list->clear();
		for (Feature* original : mInternal->originals) {
			if (original == nullptr) {
				continue;
			}
			mInternal->list->addItem(QString::fromStdString(original->GetName()));
		}
	}

	void OriginalsList::addSelected()
	{
		Feature* selected = ViewTool::getSelectedFeature();
		if (selected == nullptr) {
			CORE_WARN(
				"[Transform] {0}: select the feature to transform in the tree or the "
				"view first",
				mInternal->owner.GetName());
			return;
		}
		if (selected == &mInternal->owner) {
			CORE_WARN(
				"[Transform] {0}: a pattern cannot be one of its own features",
				mInternal->owner.GetName());
			return;
		}
		if (selected->getToolShape().isNull()) {
			CORE_WARN(
				"[Transform] {0}: '{1}' has no material of its own to transform",
				mInternal->owner.GetName(), selected->GetName());
			return;
		}
		if (!isPartOfBodyBelow(mInternal->owner, selected)) {
			CORE_WARN(
				"[Transform] {0}: '{1}' is not part of the shape below it",
				mInternal->owner.GetName(), selected->GetName());
			return;
		}
		if (std::find(
				mInternal->originals.begin(), mInternal->originals.end(), selected)
			!= mInternal->originals.end()) {
			return;
		}

		mInternal->originals.push_back(selected);
		refresh();
		if (mInternal->onChanged) {
			mInternal->onChanged();
		}
	}

	void OriginalsList::removeCurrent()
	{
		const int row = mInternal->list->currentRow();
		if (row < 0 || row >= static_cast<int>(mInternal->originals.size())) {
			return;
		}
		mInternal->originals.erase(
			mInternal->originals.begin() + static_cast<std::ptrdiff_t>(row));
		refresh();
		if (mInternal->onChanged) {
			mInternal->onChanged();
		}
	}
}
