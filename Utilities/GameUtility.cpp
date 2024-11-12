#include "GameUtility.h"

namespace MyDirectX
{
    void ParallelFor(uint32_t total, std::function<void(uint32_t)> func)
    {
        constexpr uint32_t GroupSize = 64;
        uint32_t numGroups = DivideAndRoundUp(total, GroupSize);
        std::vector<std::future<void>> futures(numGroups-1);

        // TODO: capture 'func' by ref, its lifetime is within this function
        auto forEachGroup = [f=std::move(func), total](uint32_t groupIndex)
        {
            const uint32_t beginIndex = groupIndex * GroupSize;
            const uint32_t endIndex = std::min(beginIndex + GroupSize, total);
            for (uint32_t i = beginIndex; i < endIndex; ++i)
            {
                f(i);
            }
        };
        
        if (numGroups > 1)
        {
            for (uint32_t groupIndex = 1; groupIndex < numGroups; ++groupIndex)
            {
                futures[groupIndex-1] = Async(EAsyncExecution::stdAsync, forEachGroup, groupIndex);
            }
        }
        
        forEachGroup(0);
        // Wait
        for (auto it = std::cbegin(futures); it != std::cend(futures); ++it)
        {
            it->wait();
        }
    }

    void Dispatch(uint32_t numGroups, uint32_t groupSize, std::function<void(uint32_t)> func)
    {
        std::vector<std::future<void>> futures(numGroups-1);
        if (numGroups > 1)
        {
            for (uint32_t groupIndex = 1; groupIndex < numGroups; ++groupIndex)
            {
                futures[groupIndex-1] = Async(EAsyncExecution::stdAsync, func, groupIndex);
            }
        }
        func(0);
        // Wait
        for (auto it = std::cbegin(futures); it != std::cend(futures); ++it)
        {
            it->wait();
        }
    }

}
