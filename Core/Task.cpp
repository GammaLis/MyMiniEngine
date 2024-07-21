#include "Task.h"
#include "Utility.h"

using namespace Timo;

Context Timo::g_TaskContext{};

Context::~Context() 
{
	Destroy();
}

void Context::Init(uint32_t numThreads)
{
	Destroy();

	numThreads = std::max(numThreads, 1u);

	// Retrieve the number of hardware threads in this sytem
	uint32_t numHardwareThreads = std::thread::hardware_concurrency();

	// -1 (main threads)
	numThreads = std::min(numThreads, numHardwareThreads-1);

	for (uint32_t i = 0; i < numThreads; i++) 
	{
		auto &thread = m_Threads.emplace_back( &Context::WorkerEntry, this );

#ifdef _WIN32
		// Do Windows-specific thread setup
		HANDLE handle = (HANDLE)thread.native_handle();

		// Put each thread on to dedicated core
		DWORD_PTR affinityMask = 1ull << i;
		SetThreadAffinityMask(handle, affinityMask);

		BOOL priority_return = SetThreadPriority(handle, THREAD_PRIORITY_NORMAL);

		std::wstring threadName = L"Task::Thread_" + std::to_wstring(i);
		SetThreadDescription(handle, threadName.c_str());
#endif
	}
}

void Context::Destroy()
{
	if (m_Threads.empty())
		return;

	uint32_t numThreads = static_cast<uint32_t>(m_Threads.size());
	for (uint32_t i = 0; i < numThreads; i++)
	{
		m_TaskQueue.Push( { .params = { .id = kInvalidTaskId }});
	}
	Wait();
	m_Threads.clear();
}

void Context::WorkerEntry() 
{
	static thread_local uint32_t ThreadId = m_ThreadId++;

	while (true)
	{
		auto task = m_TaskQueue.Pop();
		if (task.params.id == kInvalidTaskId)
			break;

		task.func(task.params);

		TaskResult result{};
		m_ResponseQueue.Push(result);
	}
}

void Context::Execute(const std::function<void(TaskParams)>& taskImpl)
{
	uint32_t taskId = m_Counter.fetch_add(1);
	Task task =
	{
		.params = { .id = taskId },
		.func = taskImpl
	};
	m_TaskQueue.Push(task);
}

void Context::Dispatch(const std::function<void(TaskParams)>& taskImpl, uint32_t dispatchSize, uint32_t groupSize) 
{
	uint32_t groupCount = Math::DivideByMultiple(dispatchSize, groupSize);
	uint32_t taskId = m_Counter.fetch_add( groupCount );
	for (uint32_t groupId = 0; groupId < groupCount; ++groupId)
	{
		Task task =
		{
			.params = { .id = taskId, .groupId = groupId, .groupSize = groupSize },
			.func = taskImpl
		};
		m_TaskQueue.Push(task);
	}
	
}

void Context::Wait()
{
	while (m_Counter > 0) {
		auto response = m_ResponseQueue.TryPop();
		if (response) {
			--m_Counter;
		}
		// _mm_pause();
		/**
		* std::this_thead::yield
		* Provides a hint to implementation to reschedule the execution of threads, allowing other threads to run.
		* Notes: The exact behavior of this function depends on the implementation, in particular on the mechanism of
		* the OS scheduler in use and the state of the system.
		*/
		std::this_thread::yield();
	}
}
