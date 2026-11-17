/*
MIT License

Copyright (c) 2024 Joshua Nelson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#pragma once
#include <cstddef>
#include <utility>
namespace job
{
    /*

    Simple delegate/function with local storage (no heap allocations).

    Delegates can be copied and are trivially destructable. They can only 
    hold trivially destructable and copiable types (the includes capturing)
    lambdas provided they don't capture any types with a non-trivial copy or destructor.

    You can adjust the storage of the delegate by providing a size parameter
    this should be a multiple of 8 so as to not waste bytes on padding.

    Example below of how to use:

    struct SomeStruct
    {
        int state = 60;

        int Member(double x)
        {
            printf("State: %d, x: %lf\n", state, x);
            return state;
        }
    };

    int FreeFunction(int capture, double parameter)
    {
        printf("Capture: %d, Parameter: %lf\n", capture, parameter);
        return capture;
    }

    void TestFunction()
    {
        job::delegate_t<int(double)> delegate;

        int capture = 24;
        delegate.make([capture](double x){
            printf("Capture: %d, Param: %lf\n", capture, x);
            return capture;
        });

        int lambda_return = delegate.call(1.0); // Capture: 24, Param: 1.0

        delegate.make<FreeFunction>(capture);

        int free_return = delegate.call(2.0); // Capture: 24, Param: 2.0

        SomeStruct s;
        delegate.make<&SomeStruct::Member>(&s);

        int member_return = delegate.call(3.0); // State: 60, x: 3.0
    }
    */


    template<typename T, size_t N = 8>
    struct delegate_t;

    template<typename R, typename... Ts, size_t N>
    struct delegate_t<R(Ts...), N>
    {
        static_assert(N % 8 == 0, "Storage should be a multiple of 8 to not waste space on padding");

        static constexpr size_t storage_size = N;
        alignas(std::max_align_t) std::byte storage[storage_size];
        R (*function)(void*, Ts&&...) = nullptr;

        
        R call(Ts&&... args) { return function(storage, std::forward<Ts>(args)...); }

        template<typename T>
        delegate_t& operator=(T&& callable)
        {
            *this = delegate_t(std::forward<T>(callable));
            return *this;
        }

        // Member function
        template<auto function_pointer, typename T>
        requires requires(T data, Ts&&... args) { 
            {(data->*function_pointer)(std::forward<Ts>(args)...)} -> std::template same_as<R>; 
        }
        void make(T data)
        {
            static_assert(sizeof(T) <= storage_size, "Storage size too small for this type");
            static_assert(alignof(T) <= alignof(std::max_align_t));
            static_assert(std::is_trivially_destructible_v<T>);
            static_assert(std::is_trivially_copyable_v<T>);

            T* obj = ::new(static_cast<void*>(storage))T(data);
            function = [](void* data, Ts&&... args) -> R {
                return ((*static_cast<T*>(data))->*function_pointer)(std::forward<Ts>(args)...);
            };
        } 

        // Free function with first parameter as data
        template<auto function_pointer, typename T>
        requires requires(T data, Ts&&... args) { 
            {function_pointer(data, std::forward<Ts>(args)...)} -> std::template same_as<R>; 
        }
        void make(T data)
        {
            static_assert(sizeof(T) <= storage_size, "Storage size too small for this type");
            static_assert(alignof(T) <= alignof(std::max_align_t));
            static_assert(std::is_trivially_destructible_v<T>);
            static_assert(std::is_trivially_copyable_v<T>);

            T* obj = ::new(static_cast<void*>(storage))T(data);
            function = [](void* data, Ts&&... args) -> R {
                return function_pointer(*static_cast<T*>(data), std::forward<Ts>(args)...);
            };
        } 

        // Capturing Lambda
        template<typename T>
        requires requires(T callable, Ts&&... args) { 
            {callable(std::forward<Ts>(args)...)} -> std::template same_as<R>; 
        }
        void make(T&& callable)
        {
            static_assert(sizeof(T) <= storage_size, "Storage size too small for this type");
            static_assert(alignof(T) <= alignof(std::max_align_t));
            static_assert(std::is_trivially_destructible_v<T>);
            static_assert(std::is_trivially_copyable_v<T>);

            T* obj = ::new(static_cast<void*>(storage))T(std::forward<T>(callable));
            
            function = [](void* data, Ts&&... args) -> R {
                return (*static_cast<T*>(data))(std::forward<Ts>(args)...);
            };
        }
    };
}