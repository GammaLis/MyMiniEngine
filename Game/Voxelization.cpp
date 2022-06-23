#include "Voxelization.h"

using namespace MyDirectX;

void Voxelization::Init(ID3D12Device* pDevice)
{
	m_VoxelCount = s_VoxelGridResolution * s_VoxelGridResolution * s_VoxelGridResolution;

	m_VoxelBuffer.Create(pDevice, L"VoxelBuffer", m_VoxelCount, sizeof(VoxelData));

	// m_VoxelTexture.Create(pDevice, L"VoxelTexture", )
}

void Voxelization::Clean()
{
	m_VoxelBuffer.Destroy();
}
