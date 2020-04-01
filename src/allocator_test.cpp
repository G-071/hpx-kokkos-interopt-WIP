#include <hpx/hpx_main.hpp> // we don't need an hpx_main that way?
#include <hpx/include/async.hpp>
#include <hpx/include/lcos.hpp>

#include "../include/buffer_manager.hpp"
#include "../include/cuda_buffer_util.hpp"
#include <cstdio>
#include <typeinfo>
#include <chrono>


constexpr size_t number_futures = 64;
constexpr size_t array_size = 10000;
constexpr size_t passes = 10;

// #pragma nv_exec_check_disable
int main(int argc, char *argv[])
{
  // Test whether it works at all:
  std::vector<float, recycle_std<float>> test0(array_size);
  for (auto &elem : test0)
    elem = elem + 1.0;

  /** Stress test for safe concurrency and performance:
   *  Hopefully this will trigger any existing race conditions and allow us to
   *  determine bottlelegs by evaluating the performance of different allocator 
   *  implementations.
   * */
  static_assert(passes >= 0);
  static_assert(array_size >= 1);
  assert(number_futures >= hpx::get_num_worker_threads());

  auto begin = std::chrono::high_resolution_clock::now();
  std::array<hpx::shared_future<void>, number_futures> futs;
  for (size_t i = 0; i < number_futures; i++) {
    futs[i]= hpx::make_ready_future<void>();
  }
  for (size_t pass = 0; pass < passes; pass++) {
    for (size_t i = 0; i < number_futures; i++) {
      futs[i] = futs[i].then([&](hpx::shared_future<void> &&predecessor) {
        std::vector<float, recycle_std<float>> test0(array_size);
        std::vector<float, recycle_std<float>> test1(array_size);
        // how about some int allocations
        std::vector<int, recycle_std<int>> test2(array_size);
        std::vector<float, recycle_std<float>> test3(array_size);
        // using some double recycle allocator
        std::vector<double, recycle_std<double>> test4(array_size);
        std::vector<double, recycle_std<double>> test5(array_size);
        std::vector<double, recycle_std<double>> test6(array_size);
        // using cuda host recycle allocator for a change!
        std::vector<double, recycle_allocator_cuda_host<double>> test7(array_size);
      });
    }
  }
  auto when = hpx::when_all(futs);
  when.wait();
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "\n==>Allocation test took " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms" << std::endl;

}
