#pragma once
#include <cstdint>
#include <bitset>
namespace thread
{
    struct cpu_info_t
    {
        std::bitset<64> high_performance_mask = 0;
    };

    cpu_info_t performance_mask();
    void set_thread_affinity(void* thread_handle, uint8_t index);
}