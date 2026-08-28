#include <Windows.h>
#include "Thread.h"
#include <vector>
namespace thread
{
    
}


thread::cpu_info_t thread::performance_mask()
{
    DWORD length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, nullptr, 0);

    std::vector<std::byte> buffer(length);


    auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data());

    if ( !GetSystemCpuSetInformation(info, length, &length, nullptr, 0) )
        return {};


    int offset = 0;
    int cpu_bit = 1;

    cpu_info_t cpu_info;

    uint64_t performance_mask = 0;

    while ( offset < length )
    {
        auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data() + offset);
        
        uint64_t cpu_bit = 1 << info->CpuSet.LogicalProcessorIndex;
        printf("%d -> (%d)\n", info->CpuSet.LogicalProcessorIndex, info->CpuSet.EfficiencyClass);

        if ( info->CpuSet.EfficiencyClass > 0 )
        {
            cpu_info.high_performance_mask.set(info->CpuSet.CoreIndex, true);
        }

        //performance_mask |=  ? cpu_bit : 0;
        
        offset += info->Size;

    }

    return cpu_info;
}

void thread::set_thread_affinity(void* thread_handle, uint8_t index)
{
    SetThreadAffinityMask(thread_handle, 1 << index);
}