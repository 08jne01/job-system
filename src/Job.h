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
        static constexpr size_t storage_size = 8;
        alignas(std::max_align_t) std::byte data[storage_size];
        void (*function)(context_t*, void*);
        counter_t* counter;
    };

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

    void run(const context_t* context, job_t job, counter_t** counter);
    void run(const context_t* context, std::span<job_t> jobs, counter_t** counter);
    void wait_for_counter(const context_t* context, counter_t* counter, int value);

    void* debug_fiber(const context_t* context);
}

