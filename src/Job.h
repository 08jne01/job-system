#pragma once
#include <span>
#include <tracy/Tracy.hpp>
#include <bit>
namespace job
{
    struct counter_t;
    struct context_t;
    struct system_t;

    struct system_info_t
    {
        int max_jobs = 2048;
        int n_fibers = 160;
        int max_threads = 8;
        int stack_size = 16'000;
        int reserve_stack_size = 64'000;
    };

    struct job_t
    {
        static constexpr size_t storage_size = 48; // make it exactly 1 cache line
        alignas(std::max_align_t) std::byte data[storage_size];
        void (*function)(context_t*, void*);
        counter_t* counter;
    };

    


    // template<typename R, typename... Ts, size_t N>
    // struct delegate_t<R(Ts...), N>
    // {
    //     static constexpr size_t storage_size = N; // make it exactly 1 cache line
    //     alignas(std::max_align_t) std::byte storage[storage_size];
    //     R (*function)(void*, Ts...);

        
    //     R call(Ts... args) { function(storage, args...); }

    //     template<auto function_pointer, typename T>
    //     void make(T data)
    //     {
    //         static_assert(sizeof(T) <= std::size(storage));
    //         static_assert(alignof(T) <= alignof(std::max_align_t));
    //         static_assert(std::is_trivially_constructible_v<T>);
    //         static_assert(std::is_trivially_destructible_v<T>);
    //         static_assert(std::is_trivially_copyable_v<T>);

    //         T* obj = ::new(static_cast<void*>(storage))T(data);
    //         function = [](void* data, Ts... args) -> R {
    //             return function_pointer(*static_cast<T*>(data), args...);
    //         };
    //     }
    // };


    // template<auto function, typename T, typename R, typename... Ts>
    // void make_delegate(delegate_t<R(Ts...)>& delegate, T data)
    // {
    //     static_assert(sizeof(T) <= std::size(delegate.storage));
    //     static_assert(alignof(T) <= alignof(std::max_align_t));
    //     static_assert(std::is_trivially_constructible_v<T>);
    //     static_assert(std::is_trivially_destructible_v<T>);
    //     static_assert(std::is_trivially_copyable_v<T>);

    //     T* obj = ::new(static_cast<void*>(delegate.storage))T(data);
    //     delegate.function = [](void* data, Ts... args) -> R {
    //         function(*static_cast<T*>(data), args...);
    //     };

    // }

    // template<auto function, typename R, typename... Ts, typename T>
    // //requires requires(T data) { function(data); }
    // delegate_t<R(T...)> make_delegate(T data)
    // {
    //     delegate_t<R(T...)> delegate;

    //     static_assert(sizeof(T) <= std::size(delegate.data));
    //     static_assert(alignof(T) <= alignof(std::max_align_t));
    //     static_assert(std::is_trivially_constructible_v<T>);
    //     static_assert(std::is_trivially_destructible_v<T>);
    //     static_assert(std::is_trivially_copyable_v<T>);

    //     T* obj = ::new(static_cast<void*>(delegate.data))T(data);
    //     delegate.function = [](void* data, Ts... args) -> R {
    //         function(*static_cast<T*>(data), args...);
    //     };

    //     return delegate;
    // }

    template<auto function, typename T>
    requires requires(context_t* context, T data) { function(context,data); }
    job_t make(T data)
    {
        ZoneScoped;
        job_t job;

        static_assert(sizeof(T) <= std::size(job.data));
        static_assert(alignof(T) <= alignof(std::max_align_t));
        static_assert(std::is_trivially_constructible_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(std::is_trivially_copyable_v<T>);

        T* obj = ::new(static_cast<void*>(job.data))T(data);

        job.function = [](context_t* context, void* data){
            function(context, *static_cast<T*>(data));
        };
        job.counter = nullptr;

        // job_t job = {
        //     .data = data,
        //     .function = [](context_t* execution, std::byte* data){
        //         function(execution, std::bit_cast<T>(data));
        //     }
        // };

        return job;
    }

    system_t* create_job_system(job_t entry_point, const system_info_t& info);
    void destroy_job_system(system_t* job_system);

    void run(context_t* context, job_t job, counter_t** counter);
    void run(context_t* context, std::span<job_t> jobs, counter_t** counter);
    void wait_for_counter(const context_t* context, counter_t* counter, int value);

    void* debug_fiber(const context_t* context);
}

