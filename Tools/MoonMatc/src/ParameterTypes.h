#pragma once

// The enum itself lives in MoonRender so the runtime reads the same uint8 the package stores.
// These using-declarations keep the existing Moon::Matc code spelling unchanged.
#include <Rendering/Resources/MaterialParameterType.h>

namespace Moon::Matc
{
	using Moon::Material::ParameterType;
	using Moon::Material::SamplerFallback;
	using Moon::Material::IsSamplerType;
	using Moon::Material::ParameterTypeName;
	using Moon::Material::ParameterTypeGlsl;
	using Moon::Material::ParseParameterType;
	using Moon::Material::ParseSamplerFallback;
	using Moon::Material::SamplerFallbackName;
}
