#include <iostream>
#include <mutex>
#include <memory>

class buffer_recycler {
  // Public interface
  public:
    template <class T, class Host_Allocator>
    static T* get(size_t number_elements) {
      std::lock_guard<std::mutex> guard(mut);
      if (!instance) {
        instance = new buffer_recycler();
        destroyer.set_singleton(instance);
      }
      return buffer_manager<T, Host_Allocator>::get(number_elements);
    }

    template <class T, class Host_Allocator>
    static void mark_unused(T *p, size_t number_elements) {
      std::lock_guard<std::mutex> guard(mut);
      if (!instance) {
        instance = new buffer_recycler();
        destroyer.set_singleton(instance);
      }
      return buffer_manager<T, Host_Allocator>::mark_unused(p,number_elements);
    }
    template <class T, class Host_Allocator>
    static void increase_usage_counter(T *p, size_t number_elements) {
      std::lock_guard<std::mutex> guard(mut);
      assert(instance != nullptr);
      return buffer_manager<T, Host_Allocator>::increase_usage_counter(p,number_elements);
    }
    static void clean_all(void) {
      std::lock_guard<std::mutex> guard(mut);
      if (instance) {
        delete instance;
        instance = nullptr;
        destroyer.set_singleton(nullptr);
      }
    }
    static void clean_unused_buffers(void) {
      std::lock_guard<std::mutex> guard(mut);
      if (instance) {
        for (auto clean_function : instance->partial_cleanup_callbacks)
          clean_function();
      }
    }

  // Member variables and methods
  private: 
    /// Singleton instance pointer
    static buffer_recycler *instance;
    /// Callbacks for buffer_manager cleanups - each callback complete destroys one buffer_manager
    std::list<std::function<void()>> total_cleanup_callbacks;
    /// Callbacks for partial buffer_manager cleanups - each callback deallocates all unsued buffers of a manager
    std::list<std::function<void()>> partial_cleanup_callbacks;
    /// One Mutex to control concurrent access - Since we do not actually ever return the singleton instance anywhere, this should hopefully suffice
    /// We want more fine-grained concurrent access eventually
    static std::mutex mut;

    buffer_recycler(void) {
    }
    ~buffer_recycler(void) {
      std::cout << "\n====================================================" << std::endl;
      std::cout << "Buffer recycler cleanup started!" << std::endl; 
      for (auto clean_function : total_cleanup_callbacks)
        clean_function();
      std::cout << "\nBuffer recycler cleanup finished!" << std::endl;
      std::cout << "====================================================" << std::endl;
    }

    static void add_total_cleanup_callback(std::function<void()> func) {
        // This methods assumes instance is initialized since it is a private method and all static public methods have guards
        instance->total_cleanup_callbacks.push_back(func);
    }

    static void add_partial_cleanup_callback(std::function<void()> func) {
        // This methods assumes instance is initialized since it is a private method and all static public methods have guards
        instance->partial_cleanup_callbacks.push_back(func);
    }


  // Subclasses
  private: 
    /// Memory Manager subclass to handle buffers a specific type 
    template<class T, class Host_Allocator>
    class buffer_manager {
      public:
        static void clean(void) {
          if (!instance)
            return;
          delete instance;
          instance = nullptr;
        }
        static void clean_unused_buffers_only(void) {
          if (!instance)
            return;
          for (auto buffer_tuple : instance->unused_buffer_list) {
            delete [] std::get<0>(buffer_tuple);
          }
          instance->unused_buffer_list.clear();
        }

        /// Tries to recycle or create a buffer of type T and size number_elements. 
        static T* get(size_t number_of_elements) {
          if (!instance) {
            instance = new buffer_manager();
            buffer_recycler::add_total_cleanup_callback(clean);
            buffer_recycler::add_partial_cleanup_callback(clean_unused_buffers_only);
          }
          instance->number_allocation++;
          // Check for unused buffers we can recycle:
          for (auto iter = instance->unused_buffer_list.begin(); iter != instance->unused_buffer_list.end(); iter++) {
            if (std::get<1>(*iter) == number_of_elements) {
              instance->buffer_list.push_back(*iter);
              instance->unused_buffer_list.erase(iter);
              instance->number_recycling++;
              std::get<2>(instance->buffer_list.back())++;
              return std::get<0>(instance->buffer_list.back());
            }
          }

          // No unsued buffer found -> Create new one and return it
          try {
            //T *buffer = new T[number_of_elements];
            Host_Allocator alloc;
            T *buffer = alloc.allocate(number_of_elements);
            instance->buffer_list.push_back(std::make_tuple(buffer, number_of_elements, 1));
            instance->number_creation++;
            return std::get<0>(instance->buffer_list.back());
          }
          catch(std::bad_alloc &e) { 
            // not enough memory left! Cleanup and attempt again:
            buffer_recycler::clean_unused_buffers();

            // If there still isn't enough memory left, the caller has to handle it 
            // We've done all we can in here
            //T *buffer = new T[number_of_elements];
            Host_Allocator alloc;
            T *buffer = alloc.allocate(number_of_elements);
            instance->buffer_list.push_back(std::make_tuple(buffer, number_of_elements, 1));
            instance->number_creation++;
            instance->number_bad_alloc++;
            return std::get<0>(instance->buffer_list.back());
          }
        }

        static void mark_unused(T* memory_location, size_t number_of_elements) {
          // This will never be called without an instance since all access for this method comes from the buffer recycler 
          // We can forego the instance existence check here
          instance->number_dealloacation++;
          // Search for used buffer
          for (auto iter = instance->buffer_list.begin(); iter != instance->buffer_list.end(); iter++) {
            if (std::get<0>(*iter) == memory_location) {
              assert(std::get<1>(*iter) == number_of_elements);
              assert(std::get<2>(*iter) >= 1);

              std::get<2>(*iter)--;
              if (std::get<2>(*iter) == 0) {
                instance->unused_buffer_list.push_front(*iter);
                instance->buffer_list.erase(iter);
              }
              return;
            }
          }
          const char *error_message =R""""(
            Error! Deallocate was called on a memory location that is not known to the buffer_manager!\n
            This should never happen!
          )""""; 
          throw std::logic_error(error_message);
        }

        static void increase_usage_counter(T* memory_location, size_t number_of_elements) {
          // Search for used buffer
          for (auto iter = instance->buffer_list.begin(); iter != instance->buffer_list.end(); iter++) {
            if (std::get<0>(*iter) == memory_location) {
              assert(std::get<1>(*iter) == number_of_elements);
              assert(std::get<2>(*iter) >= 1);

              std::get<2>(*iter)++;
              return;
            }
          }
          const char *error_message =R""""(
            Error! increase_usage_counter was called on a memory location that is not known to the buffer_manager!\n
            This should never happen!
          )""""; 
          throw std::logic_error(error_message);
        }

      private:
        /// List with all buffers still in usage 
        std::list<std::tuple<T*, size_t, size_t>> buffer_list; 
        /// List with all buffers currently not used
        std::list<std::tuple<T*,size_t, size_t>> unused_buffer_list; 
        /// Performance counters
        size_t number_allocation, number_dealloacation, number_recycling, number_creation, number_bad_alloc;
        /// Singleton instance
        static buffer_manager<T, Host_Allocator> *instance; 

        buffer_manager(void) : number_allocation(0), number_dealloacation(0), number_recycling(0),
                               number_creation(0), number_bad_alloc(0) {
          std::cout << "Buffer mananger constructor for buffers of type " << typeid(T).name() << "!" << std::endl;
        }
        ~buffer_manager(void) {
          // for (auto buffer_tuple : unused_buffer_list) {
          //   std::cout << "Unused buffer at " << std::get<0>(buffer_tuple) << " with " 
          //             << std::get<1>(buffer_tuple) << " elements" << std::endl;
          // }
          // for (auto buffer_tuple : buffer_list) {
          //   std::cout << "Used buffer at " << std::get<0>(buffer_tuple) << " with " 
          //             << std::get<1>(buffer_tuple) << " elements" << std::endl;
          // }
          for (auto buffer_tuple : unused_buffer_list) {
            Host_Allocator alloc;
            alloc.deallocate(std::get<0>(buffer_tuple), std::get<1>(buffer_tuple));
            // delete [] std::get<0>(buffer_tuple);
          }
          for (auto buffer_tuple : buffer_list) {
            Host_Allocator alloc;
            alloc.deallocate(std::get<0>(buffer_tuple), std::get<1>(buffer_tuple));
            // delete [] std::get<0>(buffer_tuple);
          }
          // Print performance counters
          size_t number_cleaned = unused_buffer_list.size() + buffer_list.size();
          std::cout << "\nBuffer mananger destructor for buffers of type " << typeid(T).name() << ":" << std::endl
                    << "----------------------------------------------------" << std::endl
                    << "--> Number of bad_allocs that triggered garbage collection:       " << number_bad_alloc << std::endl
                    << "--> Number of buffers that got requested from this manager:       " << number_allocation << std::endl
                    << "--> Number of times an unused buffer got recycled for a request:  " << number_recycling << std::endl
                    << "--> Number of times a new buffer had to be created for a request: " << number_creation << std::endl 
                    << "--> Number cleaned up buffers:                                    " << number_cleaned << std::endl 
                    << "--> Number of buffers that were marked as used upon cleanup:      " << buffer_list.size() << std::endl
                    << "==> Recycle rate:                                                 " 
                    << static_cast<float>(number_recycling)/number_allocation * 100.0f << "%" << std::endl;
                    // << "!\n-->Deleted " << unused_buffer_list.size()  << " unused buffers! " << std::endl
                    // << "-->Deleted " << buffer_list.size()  << " still used buffers! " << std::endl;
          if (buffer_list.size() > 0) {
            const char *error_message =R""""(
              WARNING: Some buffers are still marked as used upon the destruction of the buffer_manager!
              Please check if you are using the buffer_recycler without the recycle_allocator.
              If yes, you can probably fix this by manually marking buffers as unused!
            )""""; 
            //throw std::logic_error(error_message);
            std::cerr << error_message << std::endl;
          }

          unused_buffer_list.clear();
          buffer_list.clear();
        }

      public: // Putting deleted constructors in public gives more useful error messages
        // Bunch of constructors we don't need
        buffer_manager<T, Host_Allocator>(buffer_manager<T, Host_Allocator> const &other) = delete;
        buffer_manager<T, Host_Allocator> operator=(buffer_manager<T, Host_Allocator> const &other) = delete;
        buffer_manager<T, Host_Allocator>(buffer_manager<T, Host_Allocator> const &&other) = delete;
        buffer_manager<T, Host_Allocator> operator=(buffer_manager<T, Host_Allocator> const &&other) = delete;
    };

    /// This class just makes sure the singleton is destroyed automatically UNLESS it has already been explictly destroyed
    /** A user might want to explictly destroy all buffers, for example before a Kokkos cleanup.
     * However, we also want to clean up all buffers when the static variables of the program are destroyed. 
     * Having a static instance of this in the buffer_recycler ensures the latter part whilst still maintaining
     * the possibiltiy for manual cleanup using buffer_recycler::clean_all
     */
    class memory_manager_destroyer {
      public:
        memory_manager_destroyer(buffer_recycler *instance = nullptr) {
          singleton = instance;
        }
        ~memory_manager_destroyer() {
          if (singleton != nullptr)
            delete singleton;
          singleton = nullptr;
        }
        void set_singleton(buffer_recycler *s) {
          singleton = s;
        }
      private:
        buffer_recycler *singleton;
    };
    /// Static instance of the destroyer - gets destroyed at the end of the program and kills any remaining buffer_recycler with it
    static memory_manager_destroyer destroyer;

  public: // Putting deleted constructors in public gives more useful error messages
    // Bunch of constructors we don't need
    buffer_recycler(buffer_recycler const &other) = delete;
    buffer_recycler operator=(buffer_recycler const &other) = delete;
    buffer_recycler(buffer_recycler const &&other) = delete;
    buffer_recycler operator=(buffer_recycler const &&other) = delete;
};

// Instance defintions
buffer_recycler* buffer_recycler::instance = nullptr;
buffer_recycler::memory_manager_destroyer buffer_recycler::destroyer;
std::mutex buffer_recycler::mut;

template<class T, class Host_Allocator>
buffer_recycler::buffer_manager<T, Host_Allocator>* buffer_recycler::buffer_manager<T, Host_Allocator>::instance = nullptr; 

template <class T, class Host_Allocator>
struct recycle_allocator {
  using value_type = T;
  recycle_allocator() noexcept {}
  template <class U>
  recycle_allocator(recycle_allocator<U, Host_Allocator> const&) noexcept {
  }
  T* allocate(std::size_t n) {
    T* data = buffer_recycler::get<T, Host_Allocator>(n);
    return data;
  }
  void deallocate(T *p, std::size_t n) {
    buffer_recycler::mark_unused<T, Host_Allocator>(p, n);
  }
  template<typename... Args>
  void construct(T *p, Args... args) {
    ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
  }
  void destroy(T *p) {
    p->~T();
  }
};

template <class T, class U, class Host_Allocator>
constexpr bool operator==(recycle_allocator<T, Host_Allocator> const&, recycle_allocator<U, Host_Allocator> const&) noexcept {
  return true;
}
template <class T, class U, class Host_Allocator>
constexpr bool operator!=(recycle_allocator<T, Host_Allocator> const&, recycle_allocator<U, Host_Allocator> const&) noexcept {
  return false;
}

template <class T>
struct cuda_pinned_allocator
{
    using value_type = T;
    cuda_pinned_allocator() noexcept {}
    template <class U>
    cuda_pinned_allocator(cuda_pinned_allocator<U> const&) noexcept {}
    T* allocate(std::size_t n) {
        T* data;
        cudaMallocHost(reinterpret_cast<void**>(&data), n * sizeof(T));
        return data;
    }
    void deallocate(T* p, std::size_t n) {
        cudaFreeHost(p);
    }
};
template <class T, class U>
constexpr bool operator==(cuda_pinned_allocator<T> const&,
    cuda_pinned_allocator<U> const&) noexcept {
    return true;
}
template <class T, class U>
constexpr bool operator!=(cuda_pinned_allocator<T> const&,
    cuda_pinned_allocator<U> const&) noexcept {
    return false;
}

template <class T>
struct cuda_device_allocator
{
    using value_type = T;
    cuda_device_allocator() noexcept {}
    template <class U>
    cuda_device_allocator(cuda_device_allocator<U> const&) noexcept {}
    T* allocate(std::size_t n) {
        T* data;
        cudaMalloc(&data, n * sizeof(T));
        return data;
    }
    void deallocate(T* p, std::size_t n) {
        cudaFree(p);
    }
};
template <class T, class U>
constexpr bool operator==(cuda_device_allocator<T> const&,
    cuda_device_allocator<U> const&) noexcept {
    return true;
}
template <class T, class U>
constexpr bool operator!=(cuda_device_allocator<T> const&,
    cuda_device_allocator<U> const&) noexcept {
    return false;
}

template<class T>
using recycle_std = recycle_allocator<T, std::allocator<T>>;
template<class T>
using recycle_allocator_cuda_host = recycle_allocator<T, cuda_pinned_allocator<T>>;
template<class T>
using recycle_allocator_cuda_device = recycle_allocator<T, cuda_device_allocator<T>>;

template<class T>
struct cuda_device_buffer {
  T *device_side_buffer;
  size_t number_of_elements;
  cuda_device_buffer(size_t number_of_elements) : number_of_elements(number_of_elements) {
    device_side_buffer = recycle_allocator_cuda_device<T>{}.allocate(number_of_elements);
  }
  ~cuda_device_buffer(void) {
    recycle_allocator_cuda_device<T>{}.deallocate(device_side_buffer, number_of_elements);
  }
  // not yet implemented
  cuda_device_buffer(cuda_device_buffer const &other) = delete;
  cuda_device_buffer operator=(cuda_device_buffer const &other) = delete;
  cuda_device_buffer(cuda_device_buffer const &&other) = delete;
  cuda_device_buffer operator=(cuda_device_buffer const &&other) = delete;
};
