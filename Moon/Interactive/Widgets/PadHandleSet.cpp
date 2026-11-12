#include "Interactive/Widgets/PadHandleSet.h"

#include "Interactive/Widgets/PadTaskWidget.h"

namespace MOON
{
	PadHandleSet::PadHandleSet()
		: m_forward(new PadTaskWidget("pad"))
		, m_reverse(new PadTaskWidget("pad"))
	{
	}

	PadHandleSet::~PadHandleSet()
	{
		delete m_forward;
		delete m_reverse;
		m_forward = nullptr;
		m_reverse = nullptr;
	}

	void PadHandleSet::Attach(const gp_Pln& p_plane, bool p_additive)
	{
		const gp_Dir& normal = p_plane.Axis().Direction();
		const gp_Dir& xAxis = p_plane.XAxis().Direction();
		const gp_Dir& yAxis = p_plane.YAxis().Direction();
		const double sign = p_additive ? 1.0 : -1.0;

		m_forward->setUpOrigin(p_plane.Location().X(), p_plane.Location().Y(), p_plane.Location().Z());
		m_forward->setUpDir(sign * normal.X(), sign * normal.Y(), sign * normal.Z());
		m_forward->setUpXAxis(xAxis.X(), xAxis.Y(), xAxis.Z());
		m_forward->setUpYAxis(yAxis.X(), yAxis.Y(), yAxis.Z());

		m_reverse->setUpOrigin(p_plane.Location().X(), p_plane.Location().Y(), p_plane.Location().Z());
		m_reverse->setUpDir(-sign * normal.X(), -sign * normal.Y(), -sign * normal.Z());
		m_reverse->setUpXAxis(xAxis.X(), xAxis.Y(), xAxis.Z());
		m_reverse->setUpYAxis(yAxis.X(), yAxis.Y(), yAxis.Z());
	}

	void PadHandleSet::SetActive(bool p_active)
	{
		for (PadTaskWidget* handle : { m_forward, m_reverse })
		{
			handle->setVisible(p_active);
			handle->setActive(p_active);
		}
	}
}
