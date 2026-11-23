#pragma once
#include <cstdint>

#include <Rendering/Data/ProgramDescriptorBindings.h>

namespace Rendering::HAL
{
	/** Declares the layout's slots to a linked program - the "make the program agree" half of the
	 * binding map, and the only half that is GL:
	 *
	 *  - a uniform block is found by name (glGetUniformBlockIndex) and told which binding point it
	 *    uses (glUniformBlockBinding);
	 *  - a sampler is found by name (glGetUniformLocation) and told which texture unit it reads
	 *    (glUniform1i).
	 *
	 * After this, a draw only looks up the slot in the binding map and binds the descriptor there;
	 * nothing sets a per-draw uniform any more (filament's binding map, generated once per program).
	 *
	 * Runs where the context is, with the program linked.
	 */
	void DeclareProgramBindings(
		uint32_t p_programId,
		const Rendering::Data::DescriptorSetInfo& p_bindings);
}
