
#include <hpx/hpx_main.hpp>
#include <hpx/include/async.hpp>
#include <hpx/include/lcos.hpp>

#include <hpx/kokkos.hpp>

#include <Kokkos_Core.hpp>
#include <cstdio>
#include <typeinfo>

// scoped_timer -- stolen from Mikael
#include "../include/buffer_manager.hpp"
#include <hpx/timing/high_resolution_timer.hpp>
#include <memory>

using kokkos_array = Kokkos::View<float[1000], Kokkos::HostSpace, Kokkos::MemoryUnmanaged>;
// using kokkos_pinned_array = Kokkos::View<type_in_view, Kokkos::CudaHostPinnedSpace>;
// using kokkos_cuda_array = Kokkos::View<type_in_view, Kokkos::CudaSpace>;

template <class kokkos_type, class alloc_type, class element_type>
class recycled_view : public kokkos_type {
    private:
        static alloc_type allocator;
        size_t total_elements;
    public:
        template <class... Args>
        recycled_view(Args... args) :
          kokkos_type(allocator.allocate(kokkos_type::required_allocation_size(args...) / sizeof(element_type)),args...),
          total_elements(kokkos_type::required_allocation_size(args...) / sizeof(element_type)) {
            //std::cout << "Got buffer for " << total_elements << std::endl;
        }
        recycled_view(const recycled_view<kokkos_type, alloc_type, element_type> &other) : 
          kokkos_type(other) {
          total_elements = other.total_elements;
          std::cerr << "copy" << std::endl;
          allocator.increase_usage_counter(other.data(), other.total_elements);
        }
        recycled_view<kokkos_type, alloc_type, element_type>& operator = (const recycled_view<kokkos_type, alloc_type, element_type> &other) {
          kokkos_type::operator = (other);
          total_elements = other.total_elements;
          allocator.increase_usage_counter(other.data(), other.total_elements);
          return *this;
        }
        recycled_view(recycled_view<kokkos_type, alloc_type, element_type> &&other) : 
          kokkos_type(other) {
          total_elements = other.total_elements;
          // so that is doesn't matter if deallocate is called in the moved-from object
          allocator.increase_usage_counter(other.data(), other.total_elements);
        }
        recycled_view<kokkos_type, alloc_type, element_type>& operator = (recycled_view<kokkos_type, alloc_type, element_type> &&other) {
          kokkos_type::operator = (other);
          total_elements = other.total_elements;
          // so that is doesn't matter if deallocate is called in the moved-from object
          allocator.increase_usage_counter(other.data(), other.total_elements);
          return *this;
        }
        ~recycled_view(void) {
            allocator.deallocate(this->data(), total_elements);
        }
};
template <class kokkos_type, class alloc_type, class element_type>
alloc_type recycled_view<kokkos_type, alloc_type, element_type>::allocator;

// Just some 2D views used for testing
template <class T>
using kokkos_um_array = Kokkos::View<T*, Kokkos::HostSpace, Kokkos::MemoryUnmanaged>;
template <class T>
using recycled_host_view = recycled_view<kokkos_um_array<T>, recycle_std<T>, T>;

template <class T>
using kokkos_um_device_array = Kokkos::View<T*, Kokkos::CudaSpace, Kokkos::MemoryUnmanaged>;
template <class T>
using recycled_device_view = recycled_view<kokkos_um_device_array<T>, recycle_allocator_cuda_device<T>, T>;


// #pragma nv_exec_check_disable
int main(int argc, char *argv[])
{
    hpx::kokkos::ScopeGuard scopeGuard(argc, argv);
    Kokkos::print_configuration(std::cout);

    // Way 1 to recycle heap buffer as well (manually)
    recycle_std<float> alli;
    float *my_recycled_data_buffer = alli.allocate(1000); // allocate memory
    {
        kokkos_um_array<float> test_buffered(my_recycled_data_buffer, 1000);
        for (size_t i = 0; i < 1000; i++) {
            test_buffered.data()[i] = i * 2.0;
        }
    }
    alli.deallocate(my_recycled_data_buffer, 1000); 
    size_t to_alloc = kokkos_um_array<float>::required_allocation_size(1000);
    std::cout << "Actual required size: "  << to_alloc << std::endl; // Still a heap allocation!

    // Way 2 for recycling 
    using test_view = recycled_host_view<float>;
    using test_double_view = recycled_host_view<double>;
    test_view my_wrapper_test0(1000);
    for (size_t i = 0; i < 1000; i++) {
        my_wrapper_test0.data()[i] = i * 2.0;
    }

    test_view my_wrapper_test1(1000);
    test_view my_wrapper_test2(1000);
    double t = 2.6;
    Kokkos::parallel_for(
      Kokkos::RangePolicy<Kokkos::Experimental::HPX>(0, 1000), KOKKOS_LAMBDA(const int n) {
          my_wrapper_test1.access(n) = t;
          my_wrapper_test2.access(n) = my_wrapper_test1.access(n);
        });

    // for some views on cuda data
    using test_device_view = recycled_device_view<float>;
    using test_device_double_view = recycled_device_view<double>;



}
