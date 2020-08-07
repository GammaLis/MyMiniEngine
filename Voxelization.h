#pragma once
#include "pch.h"
#include "GpuBuffer.h"
#include "ColorBuffer.h"

namespace MyDirectX
{
	struct VoxelData
	{
		uint32_t colorOcclusionMask;
		DirectX::XMUINT4 normalMasks;	// encoded normals
	};

	class Voxelization
	{
	public:
		static const uint32_t s_VoxelGridResolution = 128;	// 256, 512, ...
		static const uint32_t s_GridExtent = 2000;

		void Init(ID3D12Device* pDevice);
		void Clean();

		void ClearVoxelBuffer();
		void SceneVoxelization();

	private:
		StructuredBuffer m_VoxelBuffer;
		ColorBuffer m_VoxelTexture;		// 3D texture, GridResolution * GridResolution * GridResolution 
		
		uint32_t m_VoxelCount = 0;
		Math::Vector3 m_VoxelGridCenter = Math::Vector3(0.0f);
	};

}
