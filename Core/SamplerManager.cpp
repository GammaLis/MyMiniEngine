#include "SamplerManager.h"
#include "Graphics.h"
#include "Hash.h"

using namespace MyDirectX;

// 这个map作用？？？没有看到添加？？？ -20-1-18
std::map<size_t, D3D12_CPU_DESCRIPTOR_HANDLE> SamplerDesc::s_SamplerCache;

D3D12_CPU_DESCRIPTOR_HANDLE SamplerDesc::CreateDescriptor(ID3D12Device* pDevice)
{
	size_t hashValue = Utility::HashState(this);
	auto iter = s_SamplerCache.find(hashValue);
	if (iter != s_SamplerCache.end())
	{
		return iter->second;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE handle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	pDevice->CreateSampler(this, handle);

	return handle;
}

void SamplerDesc::CreateDescriptor(ID3D12Device* pDevice, D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
	pDevice->CreateSampler(this, handle);
}
