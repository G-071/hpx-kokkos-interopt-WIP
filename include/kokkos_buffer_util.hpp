// Copyright (c) 2020-2021 Gregor Daiß
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef KOKKOS_BUFFER_UTIL_HPP
#define KOKKOS_BUFFER_UTIL_HPP
#include <Kokkos_Core.hpp>

namespace recycler {

template <typename kokkos_type, typename alloc_type, typename element_type>
class recycled_view : public kokkos_type {
private:
  static alloc_type allocator;
  size_t total_elements{0};

public:
  template <class... Args>
  explicit recycled_view(Args... args)
      : kokkos_type(
            allocator.allocate(kokkos_type::required_allocation_size(args...) /
                               sizeof(element_type)),
            args...),
        total_elements(kokkos_type::required_allocation_size(args...) /
                       sizeof(element_type)) {}

  recycled_view(
      const recycled_view<kokkos_type, alloc_type, element_type> &other)
      : kokkos_type(other) {
    total_elements = other.total_elements;

    allocator.increase_usage_counter(this->data(), this->total_elements);
  }

  recycled_view<kokkos_type, alloc_type, element_type> &
  operator=(const recycled_view<kokkos_type, alloc_type, element_type> &other) {
    allocator.deallocate(this->data(), total_elements);
    kokkos_type::operator=(other);
    total_elements = other.total_elements;
    allocator.increase_usage_counter(other.data(), other.total_elements);
    return *this;
  }

  recycled_view(
      recycled_view<kokkos_type, alloc_type, element_type> &&other) noexcept
      : kokkos_type(other) {
    total_elements = other.total_elements;
    allocator.increase_usage_counter(other.data(), other.total_elements);
  }

  recycled_view<kokkos_type, alloc_type, element_type> &operator=(
      recycled_view<kokkos_type, alloc_type, element_type> &&other) noexcept {
    allocator.deallocate(this->data(), total_elements);
    kokkos_type::operator=(other);
    total_elements = other.total_elements;
    allocator.increase_usage_counter(other.data(), other.total_elements);
    return *this;
  }

  ~recycled_view() { allocator.deallocate(this->data(), total_elements); }
};

template <class kokkos_type, class alloc_type, class element_type>
alloc_type recycled_view<kokkos_type, alloc_type, element_type>::allocator;

} // end namespace recycler

/**
 * get an MDRangePolicy suitable for iterating the views
 *
 * @param executor          a kokkos ExecutionSpace, e.g.
 * hpx::kokkos::make_execution_space<Kokkos::Cuda>()
 * @param view_to_iterate   the view that needs to be iterated
 */
template <typename Executor, typename ViewType>
auto get_iteration_policy(const Executor &executor,
                          const ViewType &view_to_iterate) {
  constexpr auto rank = ViewType::ViewTraits::rank;
  const Kokkos::Array<int64_t, rank> zeros{};
  Kokkos::Array<int64_t, rank> extents;
  for (int i = 0; i < rank; ++i) {
    extents[i] = view_to_iterate.extent(i);
  }

  // TODO(pollinta) what exactly does HintLightWeight do? cf.
  // https://github.com/kokkos/kokkos/issues/1723
  return Kokkos::Experimental::require(
      Kokkos::MDRangePolicy<Executor, Kokkos::Rank<rank>>(executor, zeros,
                                                          extents),
      Kokkos::Experimental::WorkItemProperty::HintLightWeight);
  // return Kokkos::MDRangePolicy<Executor, Kokkos::Rank<rank>>(executor, zeros,
  // extents);
}

#endif
