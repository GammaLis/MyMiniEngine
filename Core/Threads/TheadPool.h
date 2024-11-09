#pragma once
// Ref: ThreadPool
#include <thread>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <future>
#include <queue>
#include <vector>

namespace Timo
{
    class ThreadPool
    {
    public:
        ThreadPool(size_t numThreads);
        ~ThreadPool();

        template <typename F, typename... Args>
        auto Enqueue(F &&f, Args &&... args);

    private:
        // Need to keep track of threads so we can join them
        std::vector<std::jthread> threads;
        // The task queue
        std::queue<std::packaged_task<void()>> tasks;

        // Sync
        std::mutex mtx;
        std::condition_variable cv;
        std::atomic<bool> stop;
    };

    // The constructor just launches some amount of threads
    inline ThreadPool::ThreadPool(size_t numThreads)
    {
        for (size_t i = 0; i < numThreads; i++)
        {
            threads.emplace_back([this]()
            {
                while (true)
                {
                    std::unique_lock lock(mtx);
                    cv.wait(lock, [this](){ return !tasks.empty(); }); 

                    if (stop.load())
                        break;

                    
                }
            });
        }
    }

}
