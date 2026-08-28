#pragma once
#include <cstdint>

namespace fiber
{
    struct fiber_t
    {
        void* fiber;
        void* return_fiber;
    };

    void* create_fiber(size_t stack_size, size_t reserve_stack_size, void(*function)(void*), void* data);
    void delete_fiber(void* fiber);
    void switch_to_fiber(void* fiber);

    bool convert_fiber_to_thread();
    void* convert_thread_to_fiber(void* arg);

    void* current();
    void* get_fiber_data();
}

