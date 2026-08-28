#include "Job.h"
#include "Thread.h"
#include "SpinLock.h"
#include "Fiber.h"
#include <atomic>
#include <mutex>

#include <chrono>
#include <cassert>

#include <etl/vector.h>
#include <etl/pool.h>
#include <rigtorp/MPMCQueue.h>
#include <tracy/TracyC.h>
#include <unordered_map>

#include <tracy/Tracy.hpp>

namespace job
{
    struct system_t;

    struct counter_t
    {
        std::atomic<int> value;
    };

    struct context_t
    {
        system_t* system;
        //job_t job;
        void* fiber;
    };

    struct wait_entry_t
    {
        void* fiber;
        counter_t* wait_counter;
        int wait_value;
    };

    struct wait_list_t // replace this
    {
        spinlock_t lock;
        std::vector<void*> fibers;
        std::vector<counter_t*> counters;
        std::vector<int> values;
    };

    struct fiber_pool_t
    {
        spinlock_t lock;
        std::vector<void*> fibers;
    };

    struct thread_context_t
    {
        void* fiber;
    };

    struct system_t
    {
        static constexpr size_t max_jobs = 2048;

        int n_fibers = 0;
        rigtorp::mpmc::Queue<job_t> jobs{max_jobs};
        rigtorp::mpmc::Queue<void*> fiber_pool{max_jobs};

        std::vector<std::jthread> threads;
        
        counter_t root_task{0};
        
        std::vector<context_t> fibers;
        wait_list_t wait_list;

        std::mutex lock;
        //rigtorp::mpmc::Queue<job_t> thread_to_fiber{24};
        std::unordered_map<std::thread::id, void*> thread_to_fiber;
    };

    static inline void run(context_t* context, job_t job);
    static inline void push_fiber_pool(fiber_pool_t* fiber_pool, void* fiber);
    static inline void* try_pop_fiber(system_t* system);

    static inline void push_wait_list(wait_list_t* wait_list, const context_t* job, counter_t* counter, int counter_wait_value);
    static inline void* check_wait_list(wait_list_t* wait_list);
    static inline bool check_job_pool(system_t* system, job_t& job);

    static inline void run_existing_counter(const context_t* context, job_t job, counter_t* counter);
    static inline void run_existing_counter(const context_t* context, std::span<job_t> jobs, counter_t* counter);
    static inline void run_system(void* data);
    static inline void run_system_thread(system_t* system);
    static inline void run_system_entry_point(system_t* system, job_t entry_point);

    static inline void wait_for_counter_no_release(const context_t* context, counter_t* counter, int value);
}

static inline void job::run(context_t* context, job_t job)
{
    job.function(context, &job.data);
    job.counter->value--;
}

static inline void job::push_fiber_pool(fiber_pool_t* fiber_pool, void* fiber)
{
    ZoneScoped
    std::scoped_lock lock(fiber_pool->lock);
    fiber_pool->fibers.push_back(fiber);
}

static inline void job::push_wait_list(wait_list_t* wait_list, const context_t* job, counter_t* counter, int counter_wait_value)
{
    ZoneScoped
    std::scoped_lock lock(wait_list->lock);

    wait_list->values.push_back(counter_wait_value);
    wait_list->counters.push_back(counter);
    wait_list->fibers.push_back(job->fiber);
}

static inline void* job::check_wait_list(wait_list_t* wait_list)
{
    ZoneScoped

    void* next_fiber = nullptr;
    if ( ! wait_list->lock.try_lock() )
        return next_fiber;

    std::lock_guard lock(wait_list->lock, std::adopt_lock);

    //printf("wait length: %llu\n", wait_list->jobs.size());

    int index = -1;

    assert(wait_list->counters.size() == wait_list->values.size());
    assert(wait_list->fibers.size() == wait_list->values.size());

    const int size = int(wait_list->counters.size());

    //for ( int i = int(wait_list->jobs.size())-1; i >= 0; i-- )
    for ( int i = 0; i < size; i++ )
    {
        ZoneScoped
        if ( wait_list->values[i] == wait_list->counters[i]->value )
        {
            index = i;
            break;
        }
    }

    if ( index < 0 )
        return next_fiber;
 

    {
        ZoneScoped
        const int back = size - 1;

        next_fiber = wait_list->fibers[index];
        std::swap(wait_list->values[index], wait_list->values[back]);
        std::swap(wait_list->counters[index], wait_list->counters[back]);
        std::swap(wait_list->fibers[index], wait_list->fibers[back]);

        wait_list->values.pop_back();
        wait_list->counters.pop_back();
        wait_list->fibers.pop_back();
    }
    
    //printf("Popping: %p\n", job.fiber);
    return next_fiber;
}

static inline bool job::check_job_pool(system_t* system, job_t& job)
{
    ZoneScoped
    return system->jobs.try_pop(job);
}

static inline void* job::try_pop_fiber(system_t* system)
{
    ZoneScoped
    // Check Wait List first.
    void* next_fiber = check_wait_list(&system->wait_list);
    //printf("check wait list\n");
    if ( next_fiber != nullptr )
      return next_fiber;

    if ( system->fiber_pool.try_pop(next_fiber) )
        return next_fiber;

    return nullptr;
}

static inline void job::run_system(void* data)
{
    context_t context = *static_cast<context_t*>(data);
    system_t* system = context.system;
    while ( system->root_task.value != 0 )
    {
        job_t job;

        void* next_fiber = check_wait_list(&system->wait_list);
        if ( next_fiber != nullptr )
        {
            system->fiber_pool.push(context.fiber);// this could race condition and get yoinked before we switch
            assert(context.fiber != next_fiber);
            //context.system->lock.lock();
            fiber::switch_to_fiber(next_fiber); // switch back to the original fiber
            //context.system->lock.unlock();
        }
        else if ( check_job_pool(system, job) )
        {
            run(&context, job);
        }
    }

    //std::scoped_lock lock();

    system->lock.lock();

    //printf("(%u) 1\n", std::this_thread::get_id()._Get_underlying_id());
    void* return_fiber = nullptr;
    
    {
        //printf("(%u) 2\n", std::this_thread::get_id()._Get_underlying_id());
        {
            
            auto it = context.system->thread_to_fiber.find(std::this_thread::get_id());
            if ( it != context.system->thread_to_fiber.end() )
            {
                return_fiber = it->second;
                //context.system->thread_to_fiber.erase(it);
                //printf("found (%u) %p\n", std::this_thread::get_id()._Get_underlying_id(), return_fiber);
            }
            else
            {
                //printf("not found (%u)\n", std::this_thread::get_id()._Get_underlying_id());
            }
        }

       //printf("(%u) 3\n", std::this_thread::get_id()._Get_underlying_id());
    }
    
    //printf("return fiber %p\n", return_fiber);
    assert(return_fiber);
    
    //printf("(%u) 4\n", std::this_thread::get_id()._Get_underlying_id());
    if ( return_fiber != context.fiber )
    {
        assert(context.fiber == fiber::current());
        assert(return_fiber != fiber::current());

        //printf("(%u) 5\n", std::this_thread::get_id()._Get_underlying_id());
        //printf("(%u) switching to -> %p\n", std::this_thread::get_id()._Get_underlying_id(), return_fiber);
        context.system->lock.unlock();
        fiber::switch_to_fiber(return_fiber);
        //printf("(%u) 6\n", std::this_thread::get_id()._Get_underlying_id());
    }
    else
    {
        context.system->lock.unlock();
        //printf("(%u) 7\n", std::this_thread::get_id()._Get_underlying_id());
    }


    //printf("Terminate\n");
    //std::terminate();
    
    //printf("Thread (%u) Switching to %p\n", std::this_thread::get_id()._Get_underlying_id(), return_fiber);

    //fiber::switch_to_fiber(context.thread_fiber);

    // const auto* fiber_data = static_cast<thread_context_t*>(fiber::get_fiber_data());
    // if ( fiber_data != nullptr ) // if we are on a thread we must complete 
    // {
    //     printf("Switching to Exit: %p\n", fiber_data->fiber);
    //     fiber::switch_to_fiber(fiber_data->fiber);
    // }

    // assert(fiber::get_fiber_data());
    //fiber::convert_fiber_to_thread();

    // //std::scoped_lock lock(context.system->wait_list.lock);
    
}

static inline void job::run_system_entry_point(system_t* system, job_t entry_point)
{
    thread_context_t thread_context{};
    void* fiber = fiber::convert_thread_to_fiber(&thread_context);

    printf("(Thread -> Fiber) (%u) -> %p\n", std::this_thread::get_id()._Get_underlying_id(), fiber);

    thread_context.fiber = fiber;

    context_t context = {
        .system = system,
        .fiber = fiber
    };

    

    {
        std::scoped_lock lock(system->lock);
        system->thread_to_fiber[std::this_thread::get_id()] = fiber;
    }


    run_existing_counter(&context, entry_point, &system->root_task);
    //wait_for_counter(&context, counter, 0);

    void* next_fiber = nullptr;
    context.system->fiber_pool.pop(next_fiber);

    fiber::switch_to_fiber(next_fiber);
    // while ( counter->value != 0 )
    // {
    //     using namespace std::chrono_literals;
    //     std::this_thread::sleep_for(100ms);
    // }


//    run_system(&context);

    //printf("(Thread <- Fiber) (%u) <- %p (root)\n", std::this_thread::get_id()._Get_underlying_id(), fiber);
    fiber::convert_fiber_to_thread();

    //printf("Thread Exiting: %u\n", std::this_thread::get_id()._Get_underlying_id());
}

static inline void job::run_system_thread(system_t* system)
{
    thread_context_t thread_context{};
    void* fiber = fiber::convert_thread_to_fiber(&thread_context);


    //printf("(Thread -> Fiber) (%u) -> %p\n", std::this_thread::get_id()._Get_underlying_id(), fiber);

    thread_context.fiber = fiber;

    context_t context = {
        .system = system,
        .fiber = fiber
    };

    {
        std::scoped_lock lock(system->lock);
        system->thread_to_fiber[std::this_thread::get_id()] = fiber;
    }


    void* next_fiber = nullptr;
    context.system->fiber_pool.pop(next_fiber);
    fiber::switch_to_fiber(next_fiber);
//    run_system(&context);

    printf("(Thread <- Fiber) (%u) <- %p\n", std::this_thread::get_id()._Get_underlying_id(), fiber);

    fiber::convert_fiber_to_thread();

    //printf("Thread Exiting: %u\n", std::this_thread::get_id()._Get_underlying_id());
}

job::system_t* job::create_job_system(job_t entry_point, const system_info_t& info)
{
    thread::cpu_info_t cpu_info = thread::performance_mask();

    const int cpu_threads = int(cpu_info.high_performance_mask.count());
    const int n_threads = std::min(cpu_threads, info.max_threads);



    assert(info.n_fibers >= n_threads);
    auto* job_system = new system_t;
    job_system->n_fibers = info.n_fibers;

    const int n_real_fibers = info.n_fibers;
    job_system->fibers.resize(n_real_fibers);
    for ( context_t& execution : job_system->fibers )
    {
        execution.system = job_system;
        execution.fiber = fiber::create_fiber(64'000, 1'000'000, run_system, &execution);
        job_system->fiber_pool.push(execution.fiber);
    }

    job_system->root_task.value = 1;
    job_system->threads.emplace_back(run_system_entry_point, job_system, entry_point);

    // create other threads after main otherwise they will exit early
    for ( int i = 1; i < n_threads; i++ )
    {
        std::jthread thread(run_system_thread, job_system);
        job_system->threads.push_back(std::move(thread));
    }

    int n_thread = 0;
    for ( int i = 0; i < cpu_info.high_performance_mask.size(); i++ )
    {
        if ( ! cpu_info.high_performance_mask.test(i) )
            continue;

        if ( n_thread >= n_threads )
            break;
        
        thread::set_thread_affinity(job_system->threads[n_thread].native_handle(), i);
        n_thread++;
    }

    

    return job_system;
}

void job::destroy_job_system(system_t* job_system)
{
    job_system->threads.clear();
    for ( context_t& execution : job_system->fibers )
    {
        fiber::delete_fiber(execution.fiber);
    }

    delete job_system;
}

static inline void job::run_existing_counter(
    const context_t* context, 
    std::span<job_t> jobs, 
    counter_t* counter
)
{
    ZoneScoped
    counter->value = int(jobs.size());

    for ( auto& job : jobs )
    {
        ZoneScopedN("Counter")
        job.counter = counter;
    }

    for ( auto& job : jobs )
    {
        ZoneScopedN("Push")
        context->system->jobs.push(job);
    }
}

static inline void job::run_existing_counter(
    const context_t* context, 
    job_t job, 
    counter_t* counter
)
{
    ZoneScoped
    counter->value = 1;
    job.counter = counter;
    context->system->jobs.push(job);
}

void job::run(const context_t* context, job_t job, counter_t** counter)
{
    *counter = new counter_t;
    run_existing_counter(context, job, *counter);
}

void job::run(const context_t* context, std::span<job_t> jobs, counter_t** counter)
{
    *counter = new counter_t;// context->system->counters.create();
    run_existing_counter(context, jobs, *counter);
}

void job::wait_for_counter_no_release(const context_t* context, counter_t* counter, int value)
{
    //assert(!context->system->fiber_pool.fibers.empty()); // out of fibers

    push_wait_list(
        &context->system->wait_list, 
        context, 
        counter, value
    );

    void* fiber = nullptr;
    do
    {
        fiber = try_pop_fiber(context->system);
    } while (fiber == nullptr);

    assert(context->system->wait_list.jobs.size() < context->system->n_fibers);

    if ( context->fiber != fiber )
    {
        // yield
        //printf("Yield to %p\n", fiber);
        fiber::switch_to_fiber(fiber);
    }
    else
    {   // This is already us, so we better have completed our tasks
        //assert(context->job.counter->value == value);
    }
}

void job::wait_for_counter(const context_t* context, counter_t* counter, int value)
{
    wait_for_counter_no_release(context, counter, value);

    delete counter;

    //context->system->counters.destroy(counter);
}

void* job::debug_fiber(const context_t* context)
{
    return context->fiber;
}