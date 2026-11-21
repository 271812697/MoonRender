#pragma once
#include <cstdint>
#include <functional>

#include <Maths/FMatrix4.h>
#include <Maths/FVector3.h>
#include <Rendering/Settings/EProjectionMode.h>
#include <Rendering/View/InputState.h>

namespace Rendering::Overlay
{
	/** Everything the 3D overlay needs to know about the frame it is drawing.
	 *
	 * The overlay used to hold a pointer to the editor's view and pull these out of it, which made
	 * the rendering core depend on the editor's view type. It needs the *facts*, not the object: the
	 * camera's matrices, the viewport size, this frame's input, and a way to ask for a framing - so
	 * the caller assembles them and hands them over. Filament's filagui draws the same line: the
	 * overlay gets the frame's data, never the window.
	 */
	struct FrameInfo
	{
		/** This frame's input. Owned by the caller and outlives the frame. */
		const ::Editor::Panels::InputState* input = nullptr;

		::Maths::FMatrix4 viewMatrix;
		::Maths::FMatrix4 projectionMatrix;
		/** The camera's world basis, for the parts of the overlay that orient themselves to it (the
		 * navigation cube's faces). */
		::Maths::FVector3 cameraForward{ 0.0f, 0.0f, -1.0f };
		::Maths::FVector3 cameraUp{ 0.0f, 1.0f, 0.0f };
		::Maths::FVector3 cameraPosition{ 0.0f, 0.0f, 0.0f };

		::Rendering::Settings::EProjectionMode projectionMode = ::Rendering::Settings::EProjectionMode::PERSPECTIVE;
		uint16_t width = 0;
		uint16_t height = 0;

		/** Frames whatever the view is focused on, looking along the direction - what the navigation
		 * cube asks for when a face is clicked. Supplied by the caller: framing is the view's
		 * business, and this is how the overlay makes the request without naming a view. */
		std::function<void(const ::Maths::FVector3&)> fitToFocus;
	};
}
