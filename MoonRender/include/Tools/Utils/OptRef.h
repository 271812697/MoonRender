#pragma once
#include <optional>
#include <type_traits>
namespace Tools::Utils
{
	/**
	* Wrapper for std::optional<std::reference_wrapper<T>> with some utility functions
	*/
    template <typename T>
    class OptRef;

    template <typename T>
    class OptRef
    {
    public:
        // Constructors
        OptRef() : m_storage() {}
        OptRef(T& value) : m_storage(std::ref(value)) {}
        OptRef(T* ptr) { if (ptr) { m_storage(std::ref(*ptr)); }}
        OptRef(std::nullopt_t) : m_storage() {}
        OptRef(const OptRef& other) : m_storage(other.m_storage) {}

        template <typename U>
        friend class OptRef;

        /** An OptRef to a derived type converts to one on its base: a Drawable holds a
         * MaterialInstance, and a Material (the definition, which is its own default instance)
         * has to be assignable to it without a cast at every call site. */
        template <typename U>
            requires std::is_base_of_v<T, U> && std::is_convertible_v<U*, T*>
        OptRef(const OptRef<U>& other) : m_storage(other.m_storage) {}

        template <typename U>
            requires std::is_base_of_v<T, U> && std::is_convertible_v<U*, T*>
        OptRef& operator=(const OptRef<U>& other)
        {
            m_storage = other.m_storage;
            return *this;
        }

        OptRef& operator=(const OptRef& other)
        {
            m_storage = other.m_storage;
            return *this;
        }
        OptRef& operator=(T* ptr)
        {
            m_storage = ptr ? std::optional(std::ref(*ptr)) : std::nullopt;
            return *this;
        }

        T& value() const
        {
            return m_storage.value().get();
        }

        T& value_or(const T& other) const
        {
            return has_value() ? value() : other;
        }

        bool has_value() const
        {
            return m_storage.has_value();
        }

        void reset()
        {
            m_storage = std::nullopt;
        }

        T* operator->() const
        {
            return &m_storage.value().get();
        }

        explicit operator bool() const
        {
            return has_value();
        }

    private:
        std::optional<std::reference_wrapper<T>> m_storage;
    };
}