#pragma once

#include <ozo/concept.h>

namespace ozo {
namespace detail {

template <typename T>
union typed_buffer {
    constexpr static auto size = sizeof(T);
    T typed;
    char raw[size];
};

template <typename T>
constexpr char* data(typed_buffer<T>& buf) { return buf.raw;}

template <typename T>
constexpr const char* data(const typed_buffer<T>& buf) { return buf.raw;}

template <typename T>
constexpr auto size(const typed_buffer<T>& buf) { return buf.size;}
} // namespace detail

template <typename T>
struct is_raw_data_writable<detail::typed_buffer<T>> : std::true_type {};

} // namespace ozo

