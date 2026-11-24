#pragma once
#include <Tools/Utils/OptRef.h>

#include "Rendering/Data/Framebuffer.h"
#include "Rendering/Entities/Camera.h"

namespace Rendering::Data
{
	/**
	* Describe how a given frame should be rendered
	*/
	struct FrameDescriptor
	{
		uint16_t renderWidth = 0;
		uint16_t renderHeight = 0;
		Tools::Utils::OptRef<Rendering::Entities::Camera> camera;
		Tools::Utils::OptRef<Data::Framebuffer> outputMsaaBuffer;
		Tools::Utils::OptRef<Data::Framebuffer> presentBuffer;

		/**
		* Ensures that the data provided in the frame descriptor is valid
		*/
		bool IsValid() const
		{
			return camera.has_value();
		}
	};
}
