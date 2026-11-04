#pragma once
#include <Rendering/Data/Material.h>

/** Compatibility name for the old Shader asset: a package is now a Material built from its
 * MaterialDefinition, and the programs are cached on the Material. No Shader class remains. */
namespace Rendering::Resources
{
	using Shader = Rendering::Data::Material;
}
