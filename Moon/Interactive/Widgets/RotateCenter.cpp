#include "Interactive/Widgets/RotateCenter.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
namespace MOON {
	namespace
	{
		/** Whether the marker is shown: it follows the right mouse button. */
		bool drawCenter = false;
	}

	RotateCenter::RotateCenter(const std::string& name) :EventWidget(name)
	{
	}

	RotateCenter::~RotateCenter()
	{
	}

	void RotateCenter::onUpdate()
	{
		if (!drawCenter) {
			return;
		}
		const auto rc = m_sceneView->GetRoaterCenter();
		const Eigen::Vector3f center = { rc.x,rc.y,rc.z };
		renderer->drawOneMesh(
			center,
			Eigen::Matrix3f::Identity(),
			Eigen::Vector3f{ 0.1f,0.1f,0.1f },
			"CoordAxis");
	}

	void RotateCenter::onRightMousePressed()
	{
		drawCenter = true;
	}

	void RotateCenter::onRightMouseReleased()
	{
		drawCenter = false;
	}
}
