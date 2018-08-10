#pragma once

#include <boost/fusion/adapted.hpp>
#include <boost/fusion/sequence.hpp>
#include <boost/fusion/support/is_sequence.hpp>
#include <boost/fusion/include/is_sequence.hpp>
#include <boost/hana/core/is_a.hpp>
#include <boost/hana/tuple.hpp>
#include <boost/hana/string.hpp>
#include <typeinfo>
#include <type_traits>
#include <iterator>

namespace ozo {

/**
* This is requirement simulation type, which is the alias to std::enable_if_t
* It is pretty simple to use it with pseudo-concepts such as OperatorNot below.
*/
template <bool Condition, typename Type = void>
using Require = std::enable_if_t<Condition, Type>;


template <typename T, typename = std::void_t<>>
struct has_operator_not : std::false_type {};
template <typename T>
struct has_operator_not<T, std::void_t<decltype(!std::declval<T>())>>
    : std::true_type {};

template <typename T>
constexpr auto OperatorNot = has_operator_not<std::decay_t<T>>::value;

/**
* This trait determines whether T is tagged
* as a output iterator
*/
template <typename T, typename Enable = void>
struct is_output_iterator : std::false_type {};

template <typename T>
struct is_output_iterator<T, typename std::enable_if<
    std::is_base_of<
        std::output_iterator_tag,
        typename std::iterator_traits<T>::iterator_category
    >::value
>::type>
: std::true_type {};

template <typename T>
constexpr auto OutputIterator = is_output_iterator<T>::value;

/**
* This trait determines whether T is tagged
* as a forward iterator
*/
template <typename T, typename Enable = void>
struct is_forward_iterator : std::false_type {};

template <typename T>
struct is_forward_iterator<T, typename std::enable_if<
    std::is_base_of<
        std::forward_iterator_tag,
        typename std::iterator_traits<T>::iterator_category
    >::value
>::type>
: std::true_type {};

template <typename T>
constexpr auto ForwardIterator = is_forward_iterator<T>::value;

/**
 * This trait determines whether T can be iterated through
 * via begin() end() functions
 */
template <typename T, typename Enable = void>
struct is_iterable : std::false_type {};

template <typename T>
struct is_iterable<T, typename std::enable_if<
    is_forward_iterator<decltype(std::declval<T>().begin())>::value &&
        is_forward_iterator<decltype(std::declval<T>().end())>::value
>::type>
: std::true_type {};

template <typename T>
constexpr auto Iterable = is_iterable<T>::value;

/**
* This trait determines whether T is
* an insert iterator bound with some container
*/
template <typename T, typename Enable = void>
struct is_insert_iterator : std::false_type {};

template <typename T>
struct is_insert_iterator<T, typename std::enable_if<
    is_output_iterator<T>::value && std::is_class<typename T::container_type>::value
>::type>
: std::true_type {};

template <typename T>
constexpr auto InsertIterator = is_insert_iterator<T>::value;

template <typename T>
constexpr auto FusionSequence = boost::fusion::traits::is_sequence<std::decay_t<T>>::value;

template <typename T>
constexpr auto HanaSequence = boost::hana::Sequence<std::decay_t<T>>::value;

template <typename T>
constexpr auto HanaStruct = boost::hana::Struct<std::decay_t<T>>::value;

template <typename T>
constexpr auto HanaString = decltype(boost::hana::is_a<boost::hana::string_tag>(std::declval<T>()))::value;

template <typename T>
constexpr auto HanaTuple = decltype(boost::hana::is_a<boost::hana::tuple_tag>(std::declval<T>()))::value;

template <typename T, typename = std::void_t<>>
struct is_fusion_adapted_struct : std::false_type {};

template <typename T>
struct is_fusion_adapted_struct<T,
    std::void_t<decltype(boost::fusion::extension::struct_member_name<T, 0>::call())>
> : std::true_type {};

template <typename T>
constexpr auto FusionAdaptedStruct = is_fusion_adapted_struct<std::decay_t<T>>::value;

template <typename T>
constexpr auto Integral = std::is_integral_v<std::decay_t<T>>;

template <typename T>
constexpr auto FloatingPoint = std::is_floating_point_v<std::decay_t<T>>;

template <typename T, typename = std::void_t<>>
struct is_raw_data_writable : std::false_type {};

template <typename T>
struct is_raw_data_writable<T, std::void_t<
    decltype(std::declval<T&>().data()),
    decltype(std::declval<T&>().size())
>> : std::integral_constant<bool, std::is_same_v<decltype(std::declval<T&>().data()), char*>> {};

template <typename T>
constexpr auto RawDataWritable = is_raw_data_writable<std::decay_t<T>>::value;

template <typename T, typename = std::void_t<>>
struct is_emplaceable : std::false_type {};

template <typename T>
struct is_emplaceable<T, std::void_t<decltype(std::declval<T&>().emplace())>> : std::true_type {};

template <typename T>
constexpr auto Emplaceable = is_emplaceable<std::decay_t<T>>::value;

} // namespace ozo
