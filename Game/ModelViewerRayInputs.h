#pragma once
#include "pch.h"
#include "GpuBuffer.h"
#include <atlbase.h>

namespace MyDirectX
{
	struct RaytracingDispatchRayInputs
	{
		RaytracingDispatchRayInputs() {  }
		RaytracingDispatchRayInputs(ID3D12Device* pDevice, ID3D12StateObject* pPSO,
			void* pHitGroupShaderTable, UINT HitGroupStride, UINT HitGroupTableSize,
			LPCWSTR rayGenExportName, LPCWSTR missExportName);

		void Cleanup();

		D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight);

		UINT m_HitGroupStride = 0;
		CComPtr<ID3D12StateObject> m_pPSO;
		ByteAddressBuffer m_RayGenShaderTable;
		ByteAddressBuffer m_MissShaderTable;
		ByteAddressBuffer m_HitShaderTable;

	};
}
