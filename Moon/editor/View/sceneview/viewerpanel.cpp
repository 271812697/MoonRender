#include "viewerpanel.h"
#include "viewerwidget.h"

#include "viewertitlebar.h"

#include <QVBoxLayout>

namespace MOON {
	ViewerPanel::ViewerPanel(QWidget* parent, Qt::WindowFlags f) :QWidget(parent)
	{
		auto sceneWindow = new ViewerWidget(this);

		// The view toolbar floats inside the viewport: it is parented to the render
		// widget (a QOpenGLWidget composites its children on top of the GL content)
		// and puts itself along the left edge, see ViewerWindowTitleBar::PlaceOverlay().
		new ViewerWindowTitleBar(sceneWindow);
		
		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(sceneWindow);
		// default to strong focus
		this->setFocusPolicy(Qt::StrongFocus);
		this->setMouseTracking(true);
	}
	void ViewerPanel::keyPressEvent(QKeyEvent* event) {
	}
}
