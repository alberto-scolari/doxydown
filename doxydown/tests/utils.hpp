#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace detail {

struct to_vector_closure {
  template<std::ranges::input_range R>
  friend auto operator|(R&& range, to_vector_closure) {
    using value_t = std::ranges::range_value_t<R>;
    // Move the elements out only when the caller gave up ownership of the range,
    // i.e., an rvalue container (`std::move(vec) | to_vector`). Views and lvalue
    // containers are copied, so `vec | to_vector` does not silently empty `vec`.
    // Views that yield temporaries (e.g. std::views::transform) are moved anyway.
    constexpr bool move_elements = not std::is_lvalue_reference_v<R>
      and not std::ranges::view<std::remove_cvref_t<R>>;

    std::vector<value_t> result;
    if constexpr (std::ranges::sized_range<R>) {
      result.reserve(static_cast<std::size_t>(std::ranges::size(range)));
    }
    for (auto&& el: range) {
      if constexpr (move_elements) {
        result.push_back(std::move(el));
      } else {
        result.push_back(std::forward<decltype(el)>(el));
      }
    }
    return result;
  }
};

} // namespace detail

/**
 * Range adaptor collecting the elements of a range into a std::vector, like
 * C++23's std::ranges::to<std::vector>():
 *
 *     std::vector<T> v = range | to_vector;
 *
 * @ingroup developer
 */
inline constexpr detail::to_vector_closure to_vector{};
