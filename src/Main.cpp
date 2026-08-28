#include <cstdio>
#include <thread>
#include <rigtorp/MPMCQueue.h>
#include <span>

#include <tracy/Tracy.hpp>

#include "Job.h"
#include "Fiber.h"


struct DoubleTask
{
    int parent;
    double value;
};

void PrintDouble(job::context_t* context, DoubleTask* value)
{
    ZoneScoped
    int count = 10;
    for ( int i = 0; i < 10000000; i++ )
    {
        count++;
    }

    // printf("\t(%u) (%d) Double: %lf\n", 
    //     std::this_thread::get_id()._Get_underlying_id(), 
    //     value->parent,
    //     value->value
    // );
}

void PrintInt(job::context_t* context, int* value)
{
    ZoneScoped
    DoubleTask x = {
        .parent = *value,
        .value = 1.0
    };

    DoubleTask y = {
        .parent = *value,
        .value = 2.0
    };

    DoubleTask z = {
        .parent = *value,
        .value = 3.0
    };

    job::job_t jobs[3] = {
        job::make<PrintDouble>(&x),
        job::make<PrintDouble>(&y),
        job::make<PrintDouble>(&z),
    };

    job::counter_t* counter = nullptr;
    job::run(context, jobs, &counter);
    job::wait_for_counter(context, counter, 0);

    //printf("(%u) Int: %d\n", std::this_thread::get_id()._Get_underlying_id(), *value);
}

// void spawn_threads(job_execution_t* context, int* n)
// {
//     std::vector<std::jthread> threads;
//     std::mutex m;
//     threads.reserve(*n);
//     for ( int i = 0; i < *n; i++ )   
//     {
//         threads.emplace_back([context, &m](){
//             void* fiber = fiber::convert_thread_to_fiber(nullptr);
//             job_execution_t new_context = {
//                 .system = context->system,
//                 .fiber = fiber,
//             };
//             run_job_system(&new_context);
//             printf("Thread Exiting: %u\n", std::this_thread::get_id()._Get_underlying_id());
//             context->system->fiber_pool.push(fiber);
//             fiber::convert_fiber_to_thread();
//             std::scoped_lock lock(m);
//             //printf("Thread Exiting: %u\n", std::this_thread::get_id()._Get_underlying_id());
//         });
//     }
    
// }

void begin_jobs(job::context_t* context, void*)
{
    ZoneScoped

    
    job::job_t jobs[32];
    int v[32];

    for ( int i = 0; i < std::size(v); i++ )
    {
        v[i] = i;
        jobs[i] = job::make<PrintInt>(&v[i]);
    }

    //job_system.jobs.push();
    //job_system.jobs.push(make_job<PrintInt>(&y));
    //job_system.jobs.push(make_job<PrintInt>(&z));
    job::counter_t* counter = nullptr;
    job::run(context, jobs, &counter);
    job::wait_for_counter(context, counter, 0);
}

void Test(void* p)
{
    printf("Hello %p\n", p);
    fiber::switch_to_fiber(p);
    std::terminate();
}

int main(int argc, char** argv)
{
    ZoneScoped

    // void* fiber = fiber::convert_thread_to_fiber(nullptr);
    // void* new_fiber = fiber::create_fiber(16'000, 16'000, Test, fiber);

    // fiber::switch_to_fiber(new_fiber);
    // printf("World\n");


    // fiber::convert_fiber_to_thread();


    printf("Main Thread: %u\n", std::this_thread::get_id()._Get_underlying_id());

    job::job_t entry_point = job::make<begin_jobs>((void*)nullptr);

    job::system_t* job_system = job::create_job_system(entry_point, job::system_info_t{
        .n_fibers = 128,
        .n_threads = 3,
        .stack_size = 64'000,
        .reserve_stack_size = 1'000'000
    });


    job::destroy_job_system(job_system);
    printf("Done, Exited Threads\n");

    // while ( true )
    // {
    //     ZoneScopedN("Test");

    //     FrameMark;
    // }



    return 0;
    //run_job(job);
}