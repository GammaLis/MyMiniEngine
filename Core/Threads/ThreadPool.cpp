#include "ThreadPool.h"
#include "Utility.h"

namespace Timo
{
    ThreadPool *ThreadPool::s_ThreadPool = nullptr;
    
    ThreadPool& ThreadPool::Get()
    {
        if (s_ThreadPool == nullptr)
        {
            s_ThreadPool = new ThreadPool();
        }
        return *s_ThreadPool;
    }

    void ThreadPool::Destroy()
    {
        delete s_ThreadPool;
    }
}