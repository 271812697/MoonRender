#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/ProgramRequests.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/ShaderLinkingResult.h>

namespace Rendering::HAL
{
	/**
	* Represents a shader program, the compiled stages a draw runs.
	*
	* This is the *backend* object, like TBuffer: it is constructed in the slot a handle names (see
	* Backend::Records::CreateProgram), it owns the GL program object, and its methods are the GL
	* operations the executor runs (build, bind, declare the descriptor slots). The reflection the
	* build finds is stored in the HwProgram base next to it. What the recording side holds is a
	* handle, wrapped by Rendering::Data::Program, which collects the stages and records the build.
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TShaderProgram : public Rendering::Backend::HwProgram
	{
	public:
		/**
		* Creates the backend object: the GL program is made here, so this runs where the context is.
		*/
		TShaderProgram(BackendConstructionTag);

		/** Does not touch GL: the program is deleted by Backend::Records::DestroyProgram. */
		~TShaderProgram() = default;

		/** Compiles and links the stages, then reflects the program's uniforms.
		 *
		 * The stages' shader objects are attached, linked and thrown away - the program keeps the
		 * compiled stages - so nothing but the program object is left afterwards. */
		Settings::ShaderLinkingResult Build(
			const std::vector<Rendering::Backend::ShaderSourceSpec>& p_stages);

		void Bind() const;
		void Unbind() const;

		/** glUniformBlockBinding / glUniform1i for every block and sampler the layout declares:
		 * the "make the program agree" half of the binding map. Needs the program linked. */
		void DeclareBindings(const Rendering::Data::DescriptorSetInfo& p_bindings) const;

		/** Returns the GL program name. */
		uint32_t GetID() const;

	protected:
		Context m_context;
	};
}
