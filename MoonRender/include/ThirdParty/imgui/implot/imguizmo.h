#pragma once
#include <ThirdParty/imgui/imgui/imgui.h>
namespace ImGizmo {

	void SetRect(const float x, const float y, const float size);
	void SetDrawList(ImDrawList* drawlist = nullptr);

	void BeginFrame(const bool background = false);

	bool DrawGizmo(float* const viewMatrix, const float* const projectionMatrix, const float pivotDistance = 0.0f);
}