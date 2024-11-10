#pragma once
// Ref: ThreadPool, https://github.com/progschj/ThreadPool/blob/master/ThreadPool.h
#include <functional>
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
        ThreadPool(uint32_t numThreads = std::thread::hardware_concurrency());
        ~ThreadPool();

        static ThreadPool& Get();
        static void Destroy();

        static void ParallelFor(uint32_t num, const std::function<void(uint32_t)> &func);

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        template <typename F, typename... Args>
        auto Enqueue(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...>>;

        size_t GetNumThreads() const { return threads.size(); }

    private:
        static ThreadPool *s_ThreadPool;
        
        // Need to keep track of threads so we can join them
        std::vector<std::jthread> threads;
        // The task queue
        std::queue<std::function<void()>> tasks;

        // Sync
        std::mutex mtx;
        std::condition_variable cv;
        // No need to use atomic here, see comments in Destructor
        // std::atomic<bool> stop;
        bool stop = false;
    };

    // The constructor just launches some amount of threads
    inline ThreadPool::ThreadPool(uint32_t numThreads)
    {
        numThreads = std::max(2u, std::min(numThreads, std::thread::hardware_concurrency()));
        
        for (uint32_t i = 0; i < numThreads; i++)
        {
            threads.emplace_back([this]()
            {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(mtx);
                        cv.wait(lock, [this](){ return !tasks.empty() || stop; }); 
                        if (stop) break;
                        task = tasks.front();
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    // Enqueue
    template <typename F, typename... Args>
    auto ThreadPool::Enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>; // decltype(f(std::forward<Args>(args)...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            // std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            [f, args...]() -> return_type
            {
                return std::invoke(f, args...);
                // f(args...);
            } );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock lock(mtx);
            // Don't allow enqueueing after stopping the pool
            if (stop)
                throw std::runtime_error("Enqueue on stopped ThreadPool");

            tasks.emplace([task](){ (*task)(); });
        }
        cv.notify_one();
        return res;
    }

    inline ThreadPool::~ThreadPool()
    {
        /**
         * Condvars and atomics do not mix
         * https://zeux.io/2024/03/23/condvars-atomic/
         *  The boolean is read under the mutex, but it was written without the mutex being held. It may feel like an overkill
         * to grab a mutex to toggle a boolean, and using 'std::atomic'. But it doesn't fix the race.
         * cvar.wait(pred); == while (!pred()) cvar.wait();
         *  What can happen in case above is that the thread checks the kill flag, which has't been set to true yet, but
         * before it gets the chance to park the thread, the main threads set the flag to true and calls 'notify_all'.
         * 'notify_all' will not wake threads that aren't currently waiting on the condition variable.
         */
        // stop = true;
        // =>
        // The correct way to go here is to ditch the atomic and grab the mutex in the destructor. This ensures that
        // the state of kill flag can't change between checking the predicate state. 
        {
            std::unique_lock lock(mtx);
            stop = true;
        }
        cv.notify_all();
        
        // foreach (thread) thread.join();
        threads.clear();
    }
}
