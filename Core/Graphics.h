#pragma once
#include "pch.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "DescriptorHeap.h"

namespace MyDirectX
{
	class CommandContext;
	class CommandListManager;
	class CommandSignature;
	class ContextManager;

	class Graphics
	{
	public:
		static ID3D12Device* s_Device;
		static CommandListManager s_CommandManager;
		static ContextManager s_ContextManager;

		inline static UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			return s_Device->GetDescriptorHandleIncrementSize(type);
		}

		// 
		static DescriptorAllocator s_DescriptorAllocator[];

		inline static D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1)
		{
			return s_DescriptorAllocator[type].Allocate(s_Device, count);
		}

		

	};

}
