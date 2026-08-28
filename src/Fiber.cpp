#include "Fiber.h"
#include "Job.h"

#include <Windows.h>

void* fiber::create_fiber(
    size_t stack_size, 
    size_t reserve_stack_size,
    void(*function)(void*), 
    void* data
)
{
    return CreateFiberEx(stack_size, reserve_stack_size, FIBER_FLAG_FLOAT_SWITCH, function, data);
}

void fiber::delete_fiber(void* fiber)
{
    DeleteFiber(fiber);
}

void fiber::switch_to_fiber(void* fiber)
{
    SwitchToFiber(fiber);
}

bool fiber::convert_fiber_to_thread()
{
    return ConvertFiberToThread();
}

void* fiber::convert_thread_to_fiber(void* arg)
{
    return ConvertThreadToFiber(arg);
}

void* fiber::get_fiber_data()
{
    return GetFiberData();
}

void* fiber::current()
{
    return GetCurrentFiber();
}