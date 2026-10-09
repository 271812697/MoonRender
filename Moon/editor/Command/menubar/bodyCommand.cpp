#include "bodyCommand.h"
#include "Core/Global/ServiceLocator.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include "core/log.h"
#include <QAction>
#include <string>
#include <vector>

namespace MOON
{
	NewBodyCommand::NewBodyCommand(QObject* parentObject)
		: Command(parentObject)
	{
		auto* action = new QAction(this);
		setAction(action);
		action->setObjectName(QString::fromUtf8("actionNewBody"));
		action->setText("New &Body");
		action->setStatusTip("Make a body: a chain of features of its own");
		// The same icon the body wears in the tree (FreeCAD's body icon).
		action->setIcon(QIcon(":/widgets/icons/partdesign/PartDesign_Body.svg"));
	}

	void NewBodyCommand::execute()
	{
		auto* body = FeatureBody::Create(FeatureBody::UniqueName("Body"));
		if (body == nullptr) {
			return;
		}
		// The body is the active one from here on, so the next feature the user makes
		// lands in it (see FeatureBody::Active and the Feature constructor).
		GetViewerWidget.refreshTreeView();
		CORE_INFO(
			"[Body] '{0}' was made and is now the body new features go into",
			body->GetName());
	}
}
