#include <hpx/hpx_main.hpp> // we don't need an hpx_main that way?
#include <hpx/include/async.hpp>
#include <hpx/include/lcos.hpp>

#include "../include/buffer_manager.hpp"
#include <cstdio>
#include <typeinfo>
#include <chrono>


constexpr size_t number_futures = 64;
constexpr size_t array_size = 10000;
constexpr size_t passes = 10;

// #pragma nv_exec_check_disable
int main(int argc, char *argv[])
{
/** Stress test for safe concurrency and performance:
 *  Hopefully this will trigger any existing race conditions and allow us to
 *  determine bottlelegs by evaluating the performance of different allocator 
 *  implementations.
 * */
  static_assert(passes >= 0);
  static_assert(array_size >= 1);
  assert(number_futures >= hpx::get_num_worker_threads());

  auto begin = std::chrono::high_resolution_clock::now();
  std::array<hpx::future<void>, number_futures> futs;
  for (size_t i = 0; i < number_futures; i++) {
    futs[i]= hpx::make_ready_future<void>();
  }
  for (size_t pass = 0; pass < passes; pass++) {
    for (size_t i = 0; i < number_futures; i++) {
      futs[i] = futs[i].then([&](hpx::future<void> &&predecessor) {
        std::vector<float, recycle_std<float>> test0(array_size);
        for (auto &elem : test0)
          elem = elem + 1.0;
        std::vector<float, recycle_std<float>> test1(array_size);
        for (auto &elem : test1)
          elem = elem + 2.0;
        // how about some int allocations
        std::vector<int, recycle_std<int>> test2(array_size);
        for (auto &elem : test2)
          elem = elem + 3;
        std::vector<float, recycle_std<float>> test3(array_size);
        for (auto &elem : test3)
          elem = elem + 4.0;
        // using some double recycle allocator
        std::vector<double, recycle_std<double>> test4(array_size);
        for (auto &elem : test4)
          elem = elem + 5.0;
        std::vector<double, recycle_std<double>> test5(array_size);
        for (auto &elem : test5)
          elem = elem + 6.0;
        std::vector<double, recycle_std<double>> test6(array_size);
        for (auto &elem : test6)
          elem = elem + 7.0;
        // using cuda host recycle allocator for a change!
        std::vector<double, recycle_allocator_cuda_host<double>> test7(array_size);
        for (auto &elem : test7)
          elem = elem + 8.0;
      });
    }
  }
  auto when = hpx::when_all(futs);
  when.wait();
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "\n==>Allocation test took " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms" << std::endl;

}
