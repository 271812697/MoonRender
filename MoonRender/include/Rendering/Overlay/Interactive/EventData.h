#pragma once
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>

namespace MOON {

	class GizmoEventData
	{
	public:
		int GetType() const { return this->Type; }
		void SetType(int val) { this->Type = val; }
		// are two events equivalent
		bool operator==(const GizmoEventData& a) const
		{
			return this->Type == a.Type && this->Equivalent(&a);
		}
	protected:
		GizmoEventData() = default;
		~GizmoEventData() = default;
		// subclasses override this to define their
		// definition of equivalent
		virtual bool Equivalent(const GizmoEventData* ed) const = 0;
		int Type;
	private:
		GizmoEventData(const GizmoEventData& c) = delete;
	};




}