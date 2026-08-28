#pragma once
#include <span>
namespace job
{
    struct counter_t;
    struct context_t;
    struct system_t;

    struct system_info_t
    {
        int max_jobs = 2048;
        int n_fibers = 160;
        int n_threads = 8;
        int stack_size = 16'000;
        int reserve_stack_size = 64'000;
    };

    struct job_t
    {
        void* data;
        void (*function)(context_t*, void*);
        counter_t* counter;
    };

    template<auto function, typename T>
    requires requires(context_t* context, T* data) { function(context,data); }
    job_t make(T* data)
    {
        job_t job = {
            .data = data,
            .function = [](context_t* execution, void* data){
                function(execution, static_cast<T*>(data));
            }
        };

        return job;
    }

    system_t* create_job_system(job_t entry_point, const system_info_t& info);
    void destroy_job_system(system_t* job_system);

    void run(const context_t* context, job_t job, counter_t** counter);
    void run(const context_t* context, std::span<job_t> jobs, counter_t** counter);
    void wait_for_counter(const context_t* context, counter_t* counter, int value);
}

