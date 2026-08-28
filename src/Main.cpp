#include <cstdio>
#include <thread>
#include <rigtorp/MPMCQueue.h>
#include <span>

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include <vector>

#include "Job.h"
#include "Fiber.h"
#include <Windows.h>
#include "Thread.h"


struct DoubleTask
{
    int parent;
    double value;
};

void PrintDouble(job::context_t* context, DoubleTask* value)
{
    ZoneScoped
    int count = 10;
    for ( int i = 0; i < 100000000; i++ )
    {
        count++;
    }

    printf("\t(%u) (%d) Double: %lf\n", 
        std::this_thread::get_id()._Get_underlying_id(), 
        value->parent,
        value->value
    );
}

TracyCZoneCtx zone;

void PrintInt(job::context_t* context, int* value)
{
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

    //TracyFiberEnter("print int children");
    job::wait_for_counter(context, counter, 0);
    //TracyFiberLeave;

    printf("(%u) Int: %d\n", std::this_thread::get_id()._Get_underlying_id(), *value);
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

void long_job(job::context_t* context, uint64_t* n)
{
    ZoneScoped
    int size = *n;
    int count = 0;
    for ( int i = 0; i < size; i++ )
    {
        count++;
    }

    *n = count;

    printf("long job: %d\n", count);
}

void begin_jobs(job::context_t* context, void*)
{
    //TracyFiberEnter("begin jobs");
    //TracyCZone(ctx,1);

    constexpr size_t n_jobs = 100;
    job::job_t jobs[n_jobs];
    int v[n_jobs-1];

    for ( int i = 0; i < std::size(v); i++ )
    {
        v[i] = i;
        jobs[i] = job::make<PrintInt>(&v[i]);
    }

    uint64_t x = 10000000000;
    jobs[n_jobs-1] = job::make<long_job>(&x);

    job::counter_t* counter = nullptr;
    job::run(context, jobs, &counter);
    //TracyFiberLeave;
    job::wait_for_counter(context, counter, 0);
    //TracyCZoneEnd(ctx);
}

void Test(void* p)
{
    printf("Hello %p\n", p);
    fiber::switch_to_fiber(p);
    std::terminate();
}

static inline bool is_prime(int n)
{
    ZoneScoped

    int sqrt_n;
    {
        ZoneScopedN("Sqrt")
        sqrt_n = int(sqrt(double(n)));
    }

    for ( int i = 2; i <= sqrt_n; i++ )
    {
        if ( n % i == 0 )
            return false;
    }

    return true;
}

void print_if_prime(job::context_t* context, int* n)
{
    ZoneScoped
    if ( ! is_prime(*n) )
        *n = 0;
}

struct PrimeJob
{
    int start;
    int size;
    int counter = 0;

    char padding[52];
};

void CalculatePrimes(PrimeJob prime_job)
{
    ZoneScoped
    for ( int i = 0; i < prime_job.size; i++ )
    {
        if ( is_prime(prime_job.start + i) )
            prime_job.counter++; 
    }
    printf("serial: %d\n", prime_job.counter);
}



void calculate_primes_job(job::context_t* context, PrimeJob* job_ptr)
{
    ZoneScoped
    PrimeJob job = *job_ptr;
    for ( int i = 0; i < job.size; i++ )
    {
        int prime = job.start + i;

        if ( is_prime(prime) )
        {
            job_ptr->counter++;
            //printf("%d\n", prime);
        }

        //job_ptr->data[i] = is_prime(prime) ? prime : 0;
    }
}

void CalculatePrimesJob(job::context_t* context, PrimeJob* prime_job)
{
    const int n_jobs = 400;
    const int n_primes_per_job = prime_job->size / n_jobs;
    std::vector<job::job_t> jobs(n_jobs);
    std::vector<PrimeJob> jobs_data(n_jobs);
    //std::vector<int> primes(n);

    for ( int i = 0; i < n_jobs; i++ ) 
    {
        int start = prime_job->start + i*n_primes_per_job;
        jobs_data[i].size = n_primes_per_job;
        jobs_data[i].start = start;

        jobs[i] = job::make<calculate_primes_job>(&jobs_data[i]);
    }

    job::counter_t* counter = nullptr;
    job::run(context, jobs, &counter);
    job::wait_for_counter(context, counter, 0);

    int total_count = 0;
    for ( int i = 0; i < n_jobs; i++ )
    {
        total_count += jobs_data[i].counter;
    }

    printf("parallel: %d\n", total_count);

    // ZoneScoped
    // for ( int i = 0; i < n; i++ )
    // {
    //     if ( primes[i] )
    //         printf("%d\n", i);
    // }
}

typedef BOOL (WINAPI *LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, 
    PDWORD);


// Helper function to count set bits in the processor mask.
DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
    DWORD i;
    
    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}

int horrible_windows_function()
{
    
    DWORD length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, nullptr, 0);

    std::vector<std::byte> buffer(length);


    auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data());

    if ( !GetSystemCpuSetInformation(info, length, &length, nullptr, 0) )
        return 0;


    int offset = 0;

    while ( offset < length )
    {
        auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data() + offset);
        printf("%d\n", info->CpuSet.EfficiencyClass);
        
        offset += info->Size;

    }
    

}



int main(int argc, char** argv)
{
    ZoneScoped

    //horrible_windows_function();

    //uint64_t mask = thread::performance_mask();
    //printf("%llx\n", mask);

    // void* fiber = fiber::convert_thread_to_fiber(nullptr);
    // void* new_fiber = fiber::create_fiber(16'000, 16'000, Test, fiber);

    // fiber::switch_to_fiber(new_fiber);
    // printf("World\n");


    // fiber::convert_fiber_to_thread();

    

    printf("Main Thread: %u\n", std::this_thread::get_id()._Get_underlying_id());

    PrimeJob job;
    job.start = 100000000;
    job.size = 3000000;

    job::job_t entry_point = job::make<CalculatePrimesJob>(&job); //job::make<begin_jobs>((void*)nullptr);

    FrameMark;

    job::system_t* job_system = job::create_job_system(entry_point, job::system_info_t{
        .n_fibers = 128,
        .max_threads = 24,
        .stack_size = 64'000,
        .reserve_stack_size = 1'000'000
    });
    FrameMark;

    CalculatePrimes(job);

    FrameMark;

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