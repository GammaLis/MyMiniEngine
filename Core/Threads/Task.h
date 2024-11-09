#pragma once
#include "mtqueue.h"
#include <functional>

namespace Timo 
{
	struct TaskParams 
	{
		uint32_t id{0};
		uint32_t groupId{0};	// or uint3
		uint32_t groupSize{1};

		// TODO...	
	};

	struct TaskResult
	{
		// TODO...
	};

	struct Task 
	{
		TaskParams params;
		std::function<void(TaskParams)> func;
	};

	class Context 
	{
	public:
		static constexpr uint32_t kDefaultGroupSize = 64;
		static constexpr uint32_t kInvalidTaskId = std::numeric_limits<uint32_t>::max();

		Context() = default;
		~Context();

		void Init(uint32_t numThreads);
		void Destroy();

		void Execute(const std::function<void(TaskParams)>& taskImpl);
		void Dispatch(const std::function<void(TaskParams)>& taskImpl, uint32_t dispatchSize, uint32_t groupSize = kDefaultGroupSize);

		void ResetCounter() { m_Counter = 0; }
		bool IsBusy() const { return m_Counter > 0; }
		void Wait();

	private:
		void WorkerEntry();

		MtQueue<Task> m_TaskQueue;
		MtQueue<TaskResult> m_ResponseQueue;

		std::vector<std::jthread> m_Threads;

		std::atomic_uint32_t m_Counter{ 0 };
		std::atomic_uint32_t m_ThreadId{ 0 };
	};

	extern Context g_TaskContext;
}
