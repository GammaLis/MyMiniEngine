#pragma once
#include <filesystem>

#include "CoreMinimal.h"
#include "Utility.h"
#include <future>
#include <iterator>
#include "Threads/Task.h"
#include "Threads/ThreadPool.h"


namespace MyDirectX
{
    static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t MaxFrameBufferCount = 3;

    INLINE Math::Vector4 GetSizeAndInvSize(float w, float h) 
    {
        return {w, h, 1.0f / w, 1.0f / h};
    }

    INLINE uint32_t DivideAndRoundUp(uint32_t x, uint32_t y)
    {
        ASSERT((y & (y-1)) == 0);
        return (x + y - 1) / y;
    }

    INLINE uint32_t DivideAndRoundDown(uint32_t x, uint32_t y)
    {
        return x / y;
    }

    INLINE void UpdateViewportAndScissor(D3D12_VIEWPORT &viewport, RECT &scissor,
        FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT d0 = 0.0f, FLOAT d1 = 1.0f)
    {
        viewport.TopLeftX = x; viewport.TopLeftY = y;
        viewport.Width = w; viewport.MaxDepth = h;
        viewport.MinDepth = d0; viewport.MaxDepth = d1;

        scissor.left = static_cast<LONG>(x); scissor.right = static_cast<LONG>(x + w);
        scissor.top = static_cast<LONG>(y); scissor.bottom = static_cast<LONG>(y + h);
    }

    INLINE D3D12_VIEWPORT GetViewport(float x, float y, float w, float h, float d0 = 0.0f, float d1 = 1.0f)
    {
        return { x, y, w, h, d0, d1 };
    }

    INLINE RECT GetScissor(int x, int y, int w, int h) 
    {
        RECT scissor;
        scissor.left = static_cast<LONG>(x); scissor.right = static_cast<LONG>(x + w);
        scissor.top = static_cast<LONG>(y); scissor.bottom = static_cast<LONG>(y + h);
        return scissor;
    }

    // Async
    enum class EAsyncExecution : uint8_t
    {
        stdAsync,
        stdDeferred,
        ThreadPool,
    };
    
    template <typename F, typename... Args>
    auto Async(EAsyncExecution execution, F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;
        std::future<return_type> future;

        auto task = [f=std::forward<F>(f), ...args=std::forward<Args>(args)]() mutable -> return_type
        {
            // return f(args...);
            // return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
            return std::invoke(std::move(f), std::move(args)...);
        };
        
        switch (execution)
        {
        case EAsyncExecution::stdAsync:
            // Ref: https://stackoverflow.com/questions/8640393/move-capture-in-lambda
            // Note if you need to move object from lambda to some other function you need to make lambda 'mutable'
            future = std::async(std::launch::async, std::move(task));
            break;
            
        case EAsyncExecution::stdDeferred:
            future = std::async(std::launch::deferred, std::move(task));
            break;
            
        case EAsyncExecution::ThreadPool:
            // future = Timo::ThreadPool::Get().Enqueue(std::forward<F>(f), std::forward<Args>(args)...);
            future = Timo::ThreadPool::Get().Enqueue(std::move(task));
            break;
            
        default:
            ASSERT(false);
        }
        return std::move(future);
    }

    void ParallelFor(uint32_t total, std::function<void(uint32_t)> func);
    void Dispatch(uint32_t numGroups, uint32_t groupSize, std::function<void(uint32_t)> func);
    
}
