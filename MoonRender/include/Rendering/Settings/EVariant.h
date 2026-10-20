#pragma once
#include <cstdint>

namespace Rendering::Settings
{
	/** The engine-side state a surface shader is compiled for - Filament's Variant bits.
	 *
	 * Desktop GL subset only. Material features are deliberately NOT part of this enum: a
	 * feature is fixed for the whole material package and becomes a MATERIAL_HAS_* define,
	 * while these bits are enumerated at build time and selected per draw.
	 */
	enum class EVariantBits : uint32_t
	{
		NONE = 0,
		DIRECTIONAL_LIGHTING = 1u << 0,
		DYNAMIC_LIGHTING = 1u << 1,
		SHADOWING = 1u << 2,
		FOG = 1u << 3,
		PICKING = 1u << 4,
		SSR = 1u << 5,
		DEPTH = 1u << 6,
	};

	using VariantKey = uint32_t;

	constexpr uint32_t kAllVariantBits =
		static_cast<uint32_t>(EVariantBits::DIRECTIONAL_LIGHTING) |
		static_cast<uint32_t>(EVariantBits::DYNAMIC_LIGHTING) |
		static_cast<uint32_t>(EVariantBits::SHADOWING) |
		static_cast<uint32_t>(EVariantBits::FOG) |
		static_cast<uint32_t>(EVariantBits::PICKING) |
		static_cast<uint32_t>(EVariantBits::SSR) |
		static_cast<uint32_t>(EVariantBits::DEPTH);

	constexpr bool HasVariantBit(VariantKey p_key, EVariantBits p_bit)
	{
		return (p_key & static_cast<uint32_t>(p_bit)) != 0;
	}

	constexpr bool IsDirectionalLightingVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::DIRECTIONAL_LIGHTING);
	}

	constexpr bool IsDynamicLightingVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::DYNAMIC_LIGHTING);
	}

	constexpr bool IsShadowReceiverVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::SHADOWING);
	}

	constexpr bool IsFogVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::FOG);
	}

	constexpr bool IsPickingVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::PICKING);
	}

	constexpr bool IsSSRVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::SSR);
	}

	constexpr bool IsDepthVariant(VariantKey p_key)
	{
		return HasVariantBit(p_key, EVariantBits::DEPTH);
	}

	/** The variant a vertex stage needs. The surface vertex stages in this engine do not branch
	 * on lighting, shadow, fog or SSR, so those bits collapse to none; the depth variant (and
	 * later picking) is the only one that changes vertex code. */
	constexpr VariantKey FilterVariantVertex(VariantKey p_key)
	{
		return p_key & static_cast<uint32_t>(EVariantBits::DEPTH);
	}

	constexpr VariantKey FilterVariantFragment(VariantKey p_key)
	{
		return p_key;
	}
}
