#include "editor/UI/TaskPanel/PadInteraction.h"

#include <algorithm>

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepLProp_SLProps.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "feature/ExtrudeFeature.h"
#include "feature/FeatureBody.h"
#include "feature/SketcherFeature.h"
#include "Sketcher/SketcherObjManager.h"
#include "Interactive/Widgets/PadTaskWidget.h"
#include "Interactive/Widgets/PadHandleSet.h"

namespace MOON
{
	namespace
	{
		/** Where a pad opened from the task bar starts: some way out, so its preview shows the
		 * shape it will make. A pad offered on a pick starts at what its handle holds instead. */
		constexpr double kDefaultLength = 10.0;

		/** The smallest length a pad may be built at. It only has to be non-zero: an extrusion of
		 * exactly zero has no direction, and the shape builder has nothing to sweep. */
		constexpr double kSmallestLength = 1.0e-3;
	}

	PadInteraction::PadInteraction(ExtrudeType p_type)
		: ShapeHelper(nullptr)
		, m_type(p_type)
		, m_handles(new PadHandleSet())
	{
		m_lengthObserver = m_handles->Forward()->AddObserver(PadTaskEvent::LengthChange, this,
			&PadInteraction::onHandleChanged);
		m_angleObserver = m_handles->Forward()->AddObserver(PadTaskEvent::AngleChange, this,
			&PadInteraction::onHandleChanged);
		SetVisible(false);
	}

	PadInteraction::PadInteraction(ExtrudeFeature* p_feature)
		: ShapeHelper(p_feature)
		, m_feature(p_feature)
		, m_handles(new PadHandleSet())
	{
		m_type = p_feature != nullptr && p_feature->extrudeType == 1 ? Subtractive : Additive;
		m_lengthObserver = m_handles->Forward()->AddObserver(PadTaskEvent::LengthChange, this,
			&PadInteraction::onHandleChanged);
		m_angleObserver = m_handles->Forward()->AddObserver(PadTaskEvent::AngleChange, this,
			&PadInteraction::onHandleChanged);
		SetVisible(false);
	}

	PadInteraction::~PadInteraction()
	{
		// The observers go first, while the handle they were put on is still there: removal only
		// takes the entry out of the list, the command behind it is this object's to free.
		if (m_lengthObserver.command != nullptr)
		{
			m_handles->Forward()->RemoveObserver(m_lengthObserver.tag);
			delete m_lengthObserver.command;
			m_lengthObserver = { 0, nullptr };
		}
		if (m_angleObserver.command != nullptr)
		{
			m_handles->Forward()->RemoveObserver(m_angleObserver.tag);
			delete m_angleObserver.command;
			m_angleObserver = { 0, nullptr };
		}
		// The handles belong to the set, which goes with this object. The feature is only deleted
		// through Abort(): once committed it belongs to the body.
	}

	Feature* PadInteraction::GetFeature()
	{
		return m_feature;
	}

	bool PadInteraction::HasBegun() const
	{
		return m_feature != nullptr;
	}

	void PadInteraction::SetVisible(bool p_visible)
	{
		m_handles->SetActive(p_visible);
		if (!p_visible)
		{
			return;
		}
		// `SetActive` on the set turns both arrows on, and a pad grows one way unless the user says
		// otherwise: the reverse arrow is there for "double" / "symmetric", not for the single side
		// a pad starts in. It used to be switched off in Attach and switched back on here, which is
		// why two arrows appeared on a single sided pad.
		m_handles->Reverse()->setActive(m_feature != nullptr && m_feature->dirType == 2);
	}

	void PadInteraction::SetBeginCallback(std::function<void()> p_callback)
	{
		m_onBegin = std::move(p_callback);
	}

	void PadInteraction::SetParamChangedCallback(std::function<void(Param)> p_callback)
	{
		m_onParamChanged = std::move(p_callback);
	}

	void PadInteraction::Attach(const Part::TopoShape& p_face)
	{
		// The plane comes from the face that was picked: at this point nothing has been built, so
		// there is no feature to ask for a profile yet.
		gp_Pln plane;
		if (p_face.isNull() || !p_face.findPlane(plane))
		{
			return;
		}
		m_offeredFromPick = true;
		m_plane = plane;
		m_hasPlane = true;
		m_handles->Attach(m_plane, m_type == ExtrudeType::Additive);
		// The identity value: a pad of no length is the shape that was picked, so nothing is built
		// while only the handles are showing. The arrows are fixed-size gizmos drawn at that spot,
		// so they stay visible and grabbable all the same.
		m_handles->Forward()->setLength(0.0f);
		m_handles->Forward()->setAngle(0.0f);
		m_handles->Reverse()->setLength(static_cast<float>(kSmallestLength));
		m_handles->Reverse()->setActive(false);
		SetVisible(true);
	}

	void PadInteraction::Begin()
	{
		if (m_started)
		{
			return;
		}
		if (m_feature != nullptr)
		{
			// A pad that already exists (it is being edited): nothing to build, only to show.
			startOnExisting();
			return;
		}
		ExtrudeFeature* feature = new ExtrudeFeature("extrude", m_type == ExtrudeType::Additive ? 0 : 1);
		m_createdFeature = true;
		m_feature = feature;
		setFeature(feature);
		setGenerateShapeName("PadShape");

		// Where it grows from: the profile face, resolved the same way the panel always did it -
		// either the pad sits on the feature the picked face belongs to, or on the last sketch.
		Part::TopoShape faceShape;
		Feature* baseFeature = nullptr;
		std::vector<std::string> subValues;
		if (ViewTool::getSelectedBasedFeature(baseFeature, subValues) && baseFeature != nullptr)
		{
			feature->setBaseFeature(baseFeature);
			feature->setSubValues(subValues);
			faceShape = feature->getProfileFace();
		}
		else
		{
			auto* sketchFeature = SketcherObjManager::instance().GetLastSketcherFeature();
			if (sketchFeature != nullptr && FeatureBody::Active() != nullptr)
			{
				FeatureBody::Active()->setBaseFeatureFor(feature);
				feature->setProfile(sketchFeature);
				faceShape = feature->getProfileFace();
			}
		}
		if (faceShape.isNull())
		{
			CORE_WARN("[PadInteraction] there is no profile to build on");
			return;
		}

		// The plane the handles stand on: an offer has already placed them on the picked face.
		if (!m_hasPlane)
		{
			if (!faceShape.findPlane(m_plane))
			{
				const TopoDS_Face face = TopoDS::Face(faceShape.getShape());
				BRepAdaptor_Surface adapt(face);
				const double u = adapt.FirstUParameter()
					+ (adapt.LastUParameter() - adapt.FirstUParameter()) / 2.0;
				const double v = adapt.FirstVParameter()
					+ (adapt.LastVParameter() - adapt.FirstVParameter()) / 2.0;
				BRepLProp_SLProps prop(adapt, u, v, 2, Precision::Confusion());
				if (prop.IsNormalDefined())
				{
					gp_Pnt pnt;
					gp_Vec vec;
					BRepGProp_Face(face).Normal(u, v, pnt, vec);
					m_plane = gp_Pln(pnt, gp_Dir(vec));
					m_hasPlane = true;
				}
			}
			else
			{
				m_hasPlane = true;
			}
			if (m_hasPlane && !m_offeredFromPick)
			{
				m_handles->Attach(m_plane, m_type == ExtrudeType::Additive);
			}
		}

		if (!m_hasPlane)
		{
			CORE_WARN("[PadInteraction] the profile has no plane to build on");
			return;
		}
		m_handles->Attach(m_plane, m_type == ExtrudeType::Additive);
		if (m_offeredFromPick)
		{
			// The length the drag has already reached: the pad is built at that size, so the shape
			// grows out of the one that was picked instead of appearing at a default.
			feature->lengthForward = std::max(static_cast<double>(m_handles->Forward()->getLength()),
				kSmallestLength);
			feature->lengthRev = std::max(static_cast<double>(m_handles->Reverse()->getLength()),
				kSmallestLength);
			feature->angleForward = m_handles->Forward()->getAngle();
			feature->angleRev = m_handles->Reverse()->getAngle();
		}
		else
		{
			// Started from the task bar: at its default size, so the preview shows what it makes
			// while the panel is open.
			feature->lengthForward = kDefaultLength;
			feature->lengthRev = kDefaultLength;
			feature->angleForward = 0.0;
			feature->angleRev = 0.0;
			m_handles->Forward()->setLength(static_cast<float>(kDefaultLength));
			m_handles->Reverse()->setLength(static_cast<float>(kDefaultLength));
		}

		syncHandleDirection();
		SetVisible(true);
		m_started = true;
		previewShape();
		if (m_onBegin)
		{
			m_onBegin();
		}
	}

	/** A pad that is already in the chain, being edited: build nothing, show its handles where its
	 * profile is and let the preview follow the panel. */
	void PadInteraction::startOnExisting()
	{
		if (m_hasPlane == false)
		{
			const Part::TopoShape faceShape = m_feature->getProfileFace();
			if (!faceShape.isNull() && faceShape.findPlane(m_plane))
			{
				m_hasPlane = true;
			}
		}
		if (!m_hasPlane)
		{
			return;
		}
		m_handles->Attach(m_plane, m_type == ExtrudeType::Additive);
		m_handles->Forward()->setLength(static_cast<float>(m_feature->lengthForward));
		m_handles->Forward()->setAngle(static_cast<float>(m_feature->angleForward));
		m_handles->Reverse()->setLength(static_cast<float>(m_feature->lengthRev));
		m_handles->Reverse()->setAngle(static_cast<float>(m_feature->angleRev));
		m_handles->Reverse()->setActive(m_feature->dirType == 2);
		syncHandleDirection();
		SetVisible(true);
		m_started = true;
		previewShape();
	}

	void PadInteraction::Commit()
	{
		if (m_feature == nullptr)
		{
			return;
		}
		generateFinalShape();
		m_createdFeature = false;
	}

	void PadInteraction::Abort()
	{
		// The handles go whether or not anything was built: most of the time this is called while
		// the operation is only being offered - a pick that the user dropped by clicking empty
		// space - and there is no feature yet, only arrows that have to leave the screen.
		if (m_feature != nullptr)
		{
			clearPreviewShape();
			if (m_createdFeature)
			{
				m_feature->RemoveFromScene();
				setFeature(nullptr);
				delete m_feature;
				m_createdFeature = false;
			}
			m_feature = nullptr;
		}
		m_started = false;
		SetVisible(false);
	}

	void PadInteraction::onHandleChanged()
	{
		// Nothing has been built yet: the drag that reaches here is the one that starts the task.
		if (m_feature == nullptr)
		{
			Begin();
			return;
		}
		m_feature->lengthForward = m_handles->Forward()->getLength();
		m_feature->angleForward = m_handles->Forward()->getAngle();
		previewShape();
		if (m_onParamChanged)
		{
			m_onParamChanged(Length1);
		}
	}

	void PadInteraction::syncHandleDirection()
	{
		if (m_feature == nullptr || m_handles == nullptr || !m_hasPlane)
		{
			return;
		}
		const gp_Dir& normal = m_plane.Axis().Direction();
		const double baseSign = m_type == ExtrudeType::Additive ? 1.0 : -1.0;
		m_feature->finalDir = { baseSign * normal.X(), baseSign * normal.Y(), baseSign * normal.Z() };

		// ExtrudeFeature::execute() keeps finalDir as the axis and implements "Reverse" by negating
		// the length, so the prism ends up along -finalDir while a handle set up along +finalDir
		// would point the wrong way: dragging it forward would grow the shape backwards. Mirror the
		// handle whenever the direction is reversed, keeping the length and angle the user has.
		const double handleSign = m_feature->dirType == 1 ? -baseSign : baseSign;
		const float length = m_handles->Forward()->getLength();
		const float angle = m_handles->Forward()->getAngle();
		m_handles->Forward()->setUpDir(static_cast<float>(handleSign * normal.X()),
			static_cast<float>(handleSign * normal.Y()), static_cast<float>(handleSign * normal.Z()));
		m_handles->Reverse()->setUpDir(static_cast<float>(-handleSign * normal.X()),
			static_cast<float>(-handleSign * normal.Y()), static_cast<float>(-handleSign * normal.Z()));
		m_handles->Forward()->setLength(length);
		m_handles->Forward()->setAngle(angle);
	}

	double PadInteraction::GetNumber(Param p_param) const
	{
		if (m_feature == nullptr)
		{
			return 0.0;
		}
		switch (p_param)
		{
		case Length1: return m_feature->lengthForward;
		case Angle1: return m_feature->angleForward;
		case Length2: return m_feature->lengthRev;
		case Angle2: return m_feature->angleRev;
		default: return 0.0;
		}
	}

	int PadInteraction::GetChoice(Param p_param) const
	{
		if (m_feature == nullptr)
		{
			return 0;
		}
		switch (p_param)
		{
		case Mode: return m_feature->extrudeType;
		case Direction: return m_feature->dirType;
		default: return 0;
		}
	}

	void PadInteraction::SetNumber(Param p_param, double p_value)
	{
		if (m_feature == nullptr)
		{
			return;
		}
		switch (p_param)
		{
		case Length1:
			m_feature->lengthForward = p_value;
			m_handles->Forward()->setLength(static_cast<float>(p_value));
			break;
		case Angle1:
			m_feature->angleForward = p_value;
			m_handles->Forward()->setAngle(static_cast<float>(p_value));
			break;
		case Length2:
			m_feature->lengthRev = p_value;
			m_handles->Reverse()->setLength(static_cast<float>(p_value));
			break;
		case Angle2:
			m_feature->angleRev = p_value;
			m_handles->Reverse()->setAngle(static_cast<float>(p_value));
			break;
		default:
			return;
		}
		previewShape();
	}

	void PadInteraction::SetChoice(Param p_param, int p_value)
	{
		if (m_feature == nullptr)
		{
			return;
		}
		if (p_param == Mode)
		{
			m_feature->extrudeType = p_value;
			previewShape();
		}
		else if (p_param == Direction)
		{
			m_feature->dirType = p_value;
			// Forward/Reverse swap which side of the profile the material grows towards, so the
			// drag handle has to follow.
			syncHandleDirection();
			m_handles->Reverse()->setActive(p_value == 2);
			previewShape();
		}
	}

}
