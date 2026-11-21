#pragma once
#include <cstdint>
#include <memory>
#include <optional>

#include <Rendering/Data/PipelineState.h>
#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Entities/Drawable.h>
#include <Rendering/HAL/Texture.h>

namespace Rendering::Data
{
	class DescriptorSet;
}

namespace Rendering::Backend
{
	/** Everything a draw needs from its material, as one command.
	 *
	 * The per-material binding - program selection and bind, the per-renderable descriptor set,
	 * the sampler/texture bindings and the loose uniform uploads - was the last part of a draw
	 * still issued immediately by the renderer. It is a command now: the renderer records this
	 * request, and the executor runs the existing MaterialInstance::Bind / UploadProperties
	 * against the state carried here. That is where those GL calls belong once the frame has a
	 * thread of its own, and it is what lets a draw be one batch with no immediate GL in it.
	 *
	 * Every member is a pointer to something that outlives the frame (a material, a texture, a
	 * descriptor set), so the request is trivially copyable and can live in the command ring. The
	 * per-material cache of "this program and these properties are already bound" is *not* here:
	 * it describes the GL state, so the executor keeps it (see OpenGLDriver).
	 */
	struct DrawMaterialRequest
	{
		const ::Rendering::Entities::Drawable* drawable = nullptr;
		/** Keeps `drawable` alive while the command waits in the ring. The passes record a drawable
		 * they iterate by value, so the pointer would name a dead stack object by the time the batch
		 * runs - a whole frame later with a render thread. The request owns a copy instead (a
		 * Drawable is a light value: its material is a reference to the instance). */
		std::shared_ptr<const ::Rendering::Entities::Drawable> ownedDrawable;

		::Rendering::Data::PipelineState pipelineState{};

		::Rendering::HAL::Texture* emptyTexture2D = nullptr;
		::Rendering::HAL::Texture* emptyTextureCube = nullptr;
		::Rendering::HAL::Texture* flatNormalTexture = nullptr;

		::Rendering::Data::DescriptorSet* perRenderableSet = nullptr;
		uint32_t perRenderableBinding = 0;
	};
}
