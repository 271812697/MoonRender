#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Rendering::Backend
{
	/** A reference to a render resource, in the shape Filament uses
	 * (filament/backend/include/backend/Handle.h).
	 *
	 * The id is a slot in a resource table; the default handle is nullid, which is what makes it
	 * falsy. A handle is trivially copyable and never dereferences anything: the object it names
	 * lives in the table, on the side that owns the context, which is why work queued for that side
	 * can name resources without holding a pointer that could dangle.
	 */
	class ResourceHandleBase
	{
	public:
		using HandleId = uint32_t;
		static constexpr HandleId nullid = HandleId{ UINT32_MAX };

		constexpr ResourceHandleBase() noexcept : m_id(nullid) {}
		explicit constexpr ResourceHandleBase(HandleId p_id) noexcept : m_id(p_id) {}

		/** Whether this handle names a resource. */
		explicit operator bool() const noexcept { return m_id != nullid; }

		/** Clears the handle; does not free the resource. */
		void clear() noexcept { m_id = nullid; }

		HandleId GetId() const noexcept { return m_id; }

		/** Slot in the resource table. Only meaningful for a valid handle. */
		int32_t Index() const noexcept { return static_cast<int32_t>(m_id); }

	protected:
		HandleId m_id;
	};

	template <typename T>
	struct ResourceHandle : public ResourceHandleBase
	{
		ResourceHandle() noexcept = default;
		explicit constexpr ResourceHandle(HandleId p_id) noexcept : ResourceHandleBase(p_id) {}

		bool operator==(ResourceHandle const& p_other) const noexcept { return GetId() == p_other.GetId(); }
		bool operator!=(ResourceHandle const& p_other) const noexcept { return GetId() != p_other.GetId(); }
		bool operator<(ResourceHandle const& p_other) const noexcept { return GetId() < p_other.GetId(); }

		/** Type-safe cast from a derived type's handle. */
		template <typename B, typename = std::enable_if_t<std::is_base_of_v<T, B>>>
		ResourceHandle(ResourceHandle<B> const& p_base) noexcept : ResourceHandleBase(p_base.GetId()) {}
	};

	/** The record a handle names - Filament's `Handle<T>`'s T. What the driver API needs to reserve
	 * a slot for a call that returns one (see DECL_DRIVER_API_RETURN in DriverAPI.inc): the caller
	 * names the handle type, this recovers the record to construct in it. */
	template <typename T>
	struct HandleRecordOf
	{
		using Type = void;
	};

	template <typename T>
	struct HandleRecordOf<ResourceHandle<T>>
	{
		using Type = T;
	};

	template <typename T>
	using HandleRecord = typename HandleRecordOf<T>::Type;
}
