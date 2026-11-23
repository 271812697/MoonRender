#pragma once
#include <functional>
#include <memory>
#include <string>

#include <gp_Pln.hxx>

#include "editor/UI/TaskPanel/FeatureInteraction.h"
#include "Interactive/Interactive/EventObject.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"

namespace MOON
{
	class ExtrudeFeature;
	class PadHandleSet;
	class ExecuteCommand;

	enum ExtrudeType
	{
		Additive,
		Subtractive
	};

	/** A pad: the operation a planar face or a sketch can be grown with.
	 *
	 * It owns the whole of it - the two handles, the feature, the preview - and exposes the
	 * parameters the panel shows. The handles are the interaction's own: a panel opened on a pick
	 * works with the handles that are already on screen, so nothing is placed twice.
	 *
	 * Two ways in:
	 *   PadInteraction(ExtrudeType)              offered on a picked face (Attach) or opened from
	 *                                            the task bar (Begin straight away).
	 *   PadInteraction(ExtrudeFeature*)          editing a pad that already exists.
	 */
	class PadInteraction : public ShapeHelper, public FeatureInteraction
	{
	public:
		explicit PadInteraction(ExtrudeType p_type);
		explicit PadInteraction(ExtrudeFeature* p_feature);
		~PadInteraction() override;

		PadInteraction(const PadInteraction&) = delete;
		PadInteraction& operator=(const PadInteraction&) = delete;

		void Attach(const Part::TopoShape& p_face) override;
		void Begin() override;
		bool HasBegun() const override;
		void Commit() override;
		void Abort() override;
		Feature* GetFeature() override;
		void SetVisible(bool p_visible) override;

		ExtrudeFeature* GetPad() const { return m_feature; }

		/** The parameters the panel edits. Numbers and choices are kept apart only because the
		 * panel's widgets are: there is no QVariant here, so the interaction stays free of Qt. */
		enum Param
		{
			Mode,       // 0 length, 1 through all, 2 up to face
			Direction,  // 0 forward, 1 reverse, 2 double, 3 symmetric
			Length1,
			Angle1,
			Length2,
			Angle2
		};
		double GetNumber(Param p_param) const;
		int GetChoice(Param p_param) const;
		void SetNumber(Param p_param, double p_value);
		void SetChoice(Param p_param, int p_value);

		/** The user started dragging a handle: the task opens now. Set by whoever offers this. */
		void SetBeginCallback(std::function<void()> p_callback) override;
		/** A handle moved and the interaction already took the value: the panel follows. */
		void SetParamChangedCallback(std::function<void(Param)> p_callback);

	private:
		/** A handle was dragged. Before the pad exists this is what starts it. */
		void onHandleChanged();
		/** Keeps the primary handle pointing the way the material actually grows. */
		void syncHandleDirection();
		/** Shows the handles of a pad that already exists, without building one. */
		void startOnExisting();

		ExtrudeFeature* m_feature = nullptr;
		/** Whether Commit/Abort own the feature (one built here) or it is somebody else's (one
		 * that already existed and is only being edited). */
		bool m_createdFeature = false;
		ExtrudeType m_type = Additive;
		std::unique_ptr<PadHandleSet> m_handles;
		/** The plane the operation stands on, once it is known. */
		gp_Pln m_plane;
		bool m_hasPlane = false;
		/** Whether the handles were placed by an offer (a pick) rather than by Begin(). */
		bool m_offeredFromPick = false;
		/** Whether Begin() has run: the pad is built and its handles are on screen. */
		bool m_started = false;
		std::function<void()> m_onBegin;
		std::function<void(Param)> m_onParamChanged;
		/** The forward handle's events, kept whole so the command AddObserver allocated can be
		 * released here (see the note on SubjectHelper). Declared after the handles, so they are
		 * let go while the handle they belong to is still there. */
		ExecuteCommandPair m_lengthObserver{ 0, nullptr };
		ExecuteCommandPair m_angleObserver{ 0, nullptr };
	};
}
