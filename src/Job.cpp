#include "Job.h"
#include "SpinLock.h"
#include "Fiber.h"
#include <atomic>
#include <mutex>

#include <cassert>

#include <etl/vector.h>
#include <etl/pool.h>
#include <rigtorp/MPMCQueue.h>
#include <unordered_map>

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
        job_t job;
        void* fiber;
        void** thread_fiber;
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
        std::vector<wait_entry_t> jobs;
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
        std::atomic<bool> running = true;
        
        std::vector<context_t> fibers;
        wait_list_t wait_list;

        std::mutex lock;
        //rigtorp::mpmc::Queue<job_t> thread_to_fiber{24};
        std::unordered_map<std::thread::id, void*> thread_to_fiber;
    };

    static inline void run(context_t execution);
    static inline void push_fiber_pool(fiber_pool_t* fiber_pool, void* fiber);
    static inline void* try_pop_fiber(system_t* system);

    static inline void push_wait_list(wait_list_t* wait_list, const context_t* job, counter_t* counter, int counter_wait_value);
    static inline bool check_wait_list(wait_list_t* wait_list, void** next_fiber);

    static inline void run_system(void* data);
    static inline void run_system_thread(system_t* system);

    static inline void wait_for_counter_no_release(const context_t* context, counter_t* counter, int value);
}

static inline void job::run(context_t execution)
{
    execution.job.function(&execution, execution.job.data);
    execution.job.counter->value--;
}

static inline void job::push_fiber_pool(fiber_pool_t* fiber_pool, void* fiber)
{
    std::scoped_lock lock(fiber_pool->lock);
    fiber_pool->fibers.push_back(fiber);
}

static inline void job::push_wait_list(wait_list_t* wait_list, const context_t* job, counter_t* counter, int counter_wait_value)
{
    std::scoped_lock lock(wait_list->lock);
    wait_list->jobs.push_back(wait_entry_t{
        .fiber = job->fiber,
        .wait_counter = counter,
        .wait_value = counter_wait_value,
    });
}

static inline bool job::check_wait_list(wait_list_t* wait_list, void** next_fiber)
{
    std::scoped_lock lock(wait_list->lock);

    printf("wait length: %llu\n", wait_list->jobs.size());

    int index = -1;

    for ( int i = int(wait_list->jobs.size())-1; i >= 0; i-- )
    {
        if ( wait_list->jobs[i].wait_counter->value == wait_list->jobs[i].wait_value )
        {
            index = i;
            break;
        }
    }

    if ( index < 0 )
        return false;

    const int back = int(wait_list->jobs.size()) - 1;
    *next_fiber = wait_list->jobs[index].fiber;
    std::swap(wait_list->jobs[index], wait_list->jobs[back]);

    wait_list->jobs.pop_back();
    //printf("Popping: %p\n", job.fiber);
    return true;
}

static inline void* job::try_pop_fiber(system_t* system)
{
    // Check Wait List first.
    void* next_fiber = nullptr;
    printf("check wait list\n");
    if ( check_wait_list(&system->wait_list, &next_fiber) )
        return next_fiber;

    if ( system->fiber_pool.try_pop(next_fiber) )
        return next_fiber;

    return nullptr;
}

static inline void job::run_system(void* data)
{
    context_t context = *static_cast<context_t*>(data);

    while ( context.system->running )
    {
        job_t job;

        void* next_fiber = nullptr;
        if ( check_wait_list(&context.system->wait_list, &next_fiber) )
        {
            //context.system->fiber_pool.push(context.fiber);// this could race condition and get yoinked before we switch
            assert(context.fiber != next_fiber);
            //context.system->lock.lock();
            printf("yield\n");
            fiber::switch_to_fiber(next_fiber); // switch back to the original fiber
            printf("resume\n");
            //context.system->lock.unlock();
        }
        else if ( context.system->jobs.try_pop(job) )
        {
            context.job = job;
            run(context);
        }
        else
        {
            printf("no jobs and no wait\n");
        }
    }

    printf("(%u) 1\n", std::this_thread::get_id()._Get_underlying_id());
    void* return_fiber = nullptr;
    
    {
        printf("(%u) 2\n", std::this_thread::get_id()._Get_underlying_id());
        {
            std::scoped_lock lock(context.system->lock);
            auto it = context.system->thread_to_fiber.find(std::this_thread::get_id());
            if ( it != context.system->thread_to_fiber.end() )
            {
                return_fiber = it->second;
                context.system->thread_to_fiber.erase(it);
                printf("found (%u) %p\n", std::this_thread::get_id()._Get_underlying_id(), return_fiber);
            }
            else
            {
                printf("not found (%u)\n", std::this_thread::get_id()._Get_underlying_id());
            }
        }

        printf("(%u) 3\n", std::this_thread::get_id()._Get_underlying_id());
    }
    assert(return_fiber);
    
    printf("(%u) 4\n", std::this_thread::get_id()._Get_underlying_id());
    if ( return_fiber != context.fiber )
    {
        printf("(%u) 5\n", std::this_thread::get_id()._Get_underlying_id());
        printf("switching to -> %p\n", return_fiber);
        fiber::switch_to_fiber(return_fiber);
        printf("(%u) 6\n", std::this_thread::get_id()._Get_underlying_id());
    }

    printf("(%u) 7\n", std::this_thread::get_id()._Get_underlying_id());

    std::terminate();
    
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

static inline void job::run_system_thread(system_t* system)
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

    assert(info.n_fibers >= info.n_threads);

    auto* job_system = new system_t;

    job_system->n_fibers = info.n_fibers;

    job_system->threads.emplace_back([job_system, entry_point](){
        void* fiber = fiber::convert_thread_to_fiber(nullptr);
    
        {
            std::scoped_lock lock(job_system->lock);
            job_system->thread_to_fiber[std::this_thread::get_id()] = fiber;
            printf("(Thread -> Fiber) (%u) -> %p (root)\n", std::this_thread::get_id()._Get_underlying_id(), fiber);
        }

        context_t context = {
            .system = job_system,
            .fiber = fiber
        };

        counter_t* counter = nullptr;
        run(&context, entry_point, &counter);
        wait_for_counter(&context, counter, 0);

        printf("(Thread <- Fiber) (%u) <- %p (root)\n", std::this_thread::get_id()._Get_underlying_id(), fiber);
        fiber::convert_fiber_to_thread();

        job_system->running = false; // this is the root task, so we are done.
    });

    for ( int i = 1; i < info.n_threads; i++ )
    {
        std::jthread thread(run_system_thread, job_system);
        job_system->threads.push_back(std::move(thread));
    }

    const int n_real_fibers = info.n_fibers - info.n_threads;
    job_system->fibers.resize(n_real_fibers);

    for ( context_t& execution : job_system->fibers )
    {
        execution.system = job_system;
        execution.fiber = fiber::create_fiber(64'000, 1'000'000, run_system, &execution);
        job_system->fiber_pool.push(execution.fiber);
        //push_fiber_pool(&job_system->fiber_pool, execution.fiber);
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

void job::run(const context_t* context, job_t job, counter_t** counter)
{
    *counter = new counter_t;
    (*counter)->value = 1;
    job.counter = *counter;
    context->system->jobs.push(job);
}

void job::run(const context_t* context, std::span<job_t> jobs, counter_t** counter)
{
    *counter = new counter_t;// context->system->counters.create();

    (*counter)->value = int(jobs.size());
    for ( auto& job : jobs )
    {
        job.counter = *counter;
        context->system->jobs.push(job);
    }
}

void job::wait_for_counter_no_release(const context_t* context, counter_t* counter, int value)
{
    //assert(!context->system->fiber_pool.fibers.empty()); // out of fibers

    void* fiber = nullptr;

    while ( fiber == nullptr )
    {
        fiber = try_pop_fiber(context->system);
    }

    push_wait_list(
        &context->system->wait_list, 
        context, 
        counter, value
    );

    assert(context->system->wait_list.jobs.size() < context->system->n_fibers);

    if ( context->fiber != fiber )
    {
        // yield
        printf("Yield to %p\n", fiber);
        fiber::switch_to_fiber(fiber);
    }
    else
    {   // This is already us, so we better have completed our tasks
        assert(context->job.counter->value == value);
    }
}

void job::wait_for_counter(const context_t* context, counter_t* counter, int value)
{
    wait_for_counter_no_release(context, counter, value);

    delete counter;

    //context->system->counters.destroy(counter);
}