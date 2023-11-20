#pragma once
#include "pch.h"
#include "Math/GLMath.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"	// RenderTexture
#include "GpuBuffer.h"
#include "DynamicUploadBuffer.h"
#include "DescriptorHeap.h"

namespace Math
{
	bool SphereConeIntersection(const Vector3 &coneTip, const Vector3& coneDir, float coneHeight, float coneAngle,
		const Vector3& sphereCenter, float sphereRadius);
}

namespace MFalcor
{
	class Scene;
}

namespace MyDirectX
{
	enum class DeferredRT
	{
		TangentFrame,
		UVTarget,
		UVGradientsTarget,
		MaterialID,

		Count
	};

	struct DecalData
	{
		//MFalcor::Quaternion orientation;
		//MFalcor::Vector3 position;
		//uint32_t albedoTexIdx = -1;
		//MFalcor::Vector3 size;
		//uint32_t normalTexIdx = -1;

		MFalcor::Matrix4x4 _WorldMat;
		MFalcor::Matrix4x4 _InvWorldMat;
		uint32_t albedoTexIdx = -1;
		uint32_t normalTexIdx = -1;
	};

	class BindlessDeferred
	{
		friend class MFalcor::Scene;

	public:
		BindlessDeferred();

		void Init(ID3D12Device *pDevice, MFalcor::Scene *pScene);
		void Clean();

		const D3D12_CPU_DESCRIPTOR_HANDLE* DecalSRVs() const { return m_DecalSRVs; }
		uint32_t NumDecalTextures() const { return m_NumDecalTextures; }

	private:

		void CreateRenderTargets(ID3D12Device *pDevice);
		void CreateDecals(ID3D12Device* pDevice);

		static const uint32_t s_NumDecals = 3;
		static const uint32_t s_NumDecalTextures = 2 * 2 + 1;
		static const uint32_t s_DeferredTileSize = 16;

		// decals
		UserDescriptorHeap m_DecalTextureHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE m_DecalSRVs[s_NumDecalTextures] = {};
		uint32_t m_NumDecalTextures = 0;
		std::vector<DecalData> m_Decals;
		uint32_t m_NumDecal = 0;

		StructuredBuffer m_DecalBuffer;
		DynamicUploadBuffer m_DecalInstanceBuffer;

		ColorBuffer m_TangentFrame;
		ColorBuffer m_UVTarget;
		ColorBuffer m_UVGradientsTarget;
		ColorBuffer m_MaterialIDTarget;

		ColorBuffer m_VisibilityBuffer;

		RootSignature m_GBufferRS;
		GraphicsPSO m_GBufferPSO;
	};

}
