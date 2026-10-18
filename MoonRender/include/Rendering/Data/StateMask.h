#pragma once
#include <cstdint>
#include <Rendering/Settings/EBlendMode.h>
namespace Rendering::Data
{
	/**
	* State mask used by materials to override some Pipeline State settings.
	*/
	struct StateMask
	{
		union
		{
			struct
			{
				uint8_t depthWriting : 1;
				uint8_t colorWriting : 1;
				uint8_t blendable : 1;
				uint8_t depthTest : 1;
				uint8_t backfaceCulling : 1;
				uint8_t frontfaceCulling : 1;
				uint8_t polygonOffsetFill : 1;
			};

			uint8_t mask;
		};
		/** Which blend factors the renderer picks for this drawable. Kept next to the packed
		 * byte rather than inside it, so the packed flags keep their meaning. */
		Settings::EBlendMode blendMode = Settings::EBlendMode::BLEND_OPAQUE;
		float lineWidth = 1.0f;
	};
}
