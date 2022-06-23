#include "BindlessDeferred.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "Scenes/Scene.h"
#include "TextureManager.h"

using namespace MyDirectX;
using namespace MFalcor;

static const DXGI_FORMAT rtFormats[] =
{
	DXGI_FORMAT_R10G10B10A2_UNORM,
	DXGI_FORMAT_R16G16B16A16_SNORM,
	DXGI_FORMAT_R16G16B16A16_SNORM,
	DXGI_FORMAT_R8_UINT
};

static const std::string DecalTexturePath[] =
{
	"Decal_01_Albedo", "Decal_01_Normal",
};

// https://www.cbloom.com/3d/techdocs/culling.txt
// https://www.gamedev.net/forums/topic/555628-sphere-cone-test-with-no-sqrt-for-frustum-culling/
bool Math::SphereConeIntersection(const Vector3& coneTip, const Vector3& coneDir, float coneHeight, float coneAngle, const Vector3& sphereCenter, float sphereRadius)
{
	if (Dot(sphereCenter - coneTip, coneDir) > coneHeight + sphereRadius)
		return false;

	float cosHalfAngle = std::cos(coneAngle * 0.5f);
	float sinHalfAngle = std::sin(coneAngle * 0.5f);

	Vector3 v = sphereCenter - coneTip;
	float a = Dot(v, coneDir);
	float b = a * sinHalfAngle / cosHalfAngle;
	float c = std::sqrt(Dot(v, v) - a * a);
	float d = c - b;
	float e = d * cosHalfAngle;

	return e < sphereRadius;
}

BindlessDeferred::BindlessDeferred()
{
	m_Decals.resize(s_NumDecals);
}

void BindlessDeferred::Init(ID3D12Device* pDevice, MFalcor::Scene* pScene)
{
	// ÒÆµ½MFalcor::SceneÀï
	//// root signature
	//{
	//	m_GBufferRS.Reset(6);
	//	m_GBufferRS[0].InitAsConstantBuffer(0);
	//	m_GBufferRS[1].InitAsConstantBuffer(1);
	//	m_GBufferRS[2].InitAsBufferSRV(0, 1);
	//	m_GBufferRS[3].InitAsBufferSRV(1, 1);
	//	m_GBufferRS[4].InitAsBufferSRV(2, 1);
	//	m_GBufferRS[5].InitAsBufferSRV(3, 1);
	//	m_GBufferRS.Finalize(pDevice, L"GBufferRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	//}

	CreateRenderTargets(pDevice);

	CreateDecals(pDevice);
}

void BindlessDeferred::Clean()
{
	// rts
	m_TangentFrame.Destroy();
	m_UVTarget.Destroy();
	m_UVGradientsTarget.Destroy();
	m_MaterialIDTarget.Destroy();

	// decals
	m_DecalBuffer.Destroy();
}

void BindlessDeferred::CreateRenderTargets(ID3D12Device* pDevice)
{
	const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

	m_TangentFrame.Create(pDevice, L"TangentFrame", width, height, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
	m_UVTarget.Create(pDevice, L"UV Target", width, height, 1, DXGI_FORMAT_R16G16B16A16_SNORM);
	m_UVGradientsTarget.Create(pDevice, L"UV Gradient Target", width, height, 1, DXGI_FORMAT_R16G16B16A16_SNORM);
	m_MaterialIDTarget.Create(pDevice, L"Material ID Target", width, height, 1, DXGI_FORMAT_R8_UINT);
}

void BindlessDeferred::CreateDecals(ID3D12Device* pDevice)
{
	// decal texture heap
	{
		m_DecalTextureHeap.Create(pDevice, L"DecalTextureHeap");
	}

	// decal texture
	{
		Graphics::s_TextureManager.Init(L"Textures/");	// Decal/

		// 0.
		UINT numDestDescriptorRanges = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[16];
		UINT pDestDescriptorRangeSizes[16] = {0};

		UINT numSrcDescriptorRanges = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE pSrcDescriptorRangeStarts[16];
		UINT pSrcDescriptorRangeSizes[16] = {0};

		uint32_t numTextures = _countof(DecalTexturePath);
		std::vector<std::string> texturePath;
		for (uint32_t i = 0; i < numTextures; ++i)
		{
			texturePath.push_back(std::string("Decals/") + DecalTexturePath[i]);
		}
		texturePath.push_back("Default_Anim");
		++numTextures;

		for (uint32_t i = 0; i < numTextures; ++i)
		{
			auto managedTex = Graphics::s_TextureManager.LoadFromFile(pDevice, texturePath[i]);
			if (managedTex->IsValid())
			{
				m_DecalSRVs[m_NumDecalTextures] = managedTex->GetSRV();

				pSrcDescriptorRangeStarts[numSrcDescriptorRanges] = m_DecalSRVs[m_NumDecalTextures];
				pSrcDescriptorRangeSizes[numSrcDescriptorRanges] = 1;
				++numSrcDescriptorRanges;

				++pDestDescriptorRangeSizes[0];

				++m_NumDecalTextures;
			}
		}

		numDestDescriptorRanges = 1;
		pDestDescriptorRangeStarts[0] = m_DecalTextureHeap.GetHandleAtOffset(0).GetCpuHandle();
		pDevice->CopyDescriptors(numDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
			numSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// decal data
	{
		using MMATH::translate;
		using MMATH::scale;
		using MMATH::inverse;

		DecalData newDecal;
		//newDecal.position = Vector3(-1200.0f, 185.0f, -445.0f);
		//newDecal.size = Vector3(2.0f, 2.0f, 2.0f);
		//newDecal.albedoTexIdx = 2 * m_NumDecal++;
		//newDecal.normalTexIdx = newDecal.albedoTexIdx + 1;
		// newDecal._WorldMat = translate(Vector3(-1200.0f - 10.0f, 185.0f + 10, -445.0f)) * scale(Vector3(400.0f, 200.0f, 400.0f));
		newDecal._WorldMat = translate(Vector3(-400.0f, 85.0f + 10, 0.0f)) * scale(Vector3(400.0f, 200.0f, 400.0f));
		newDecal._WorldMat = MMATH::transpose(newDecal._WorldMat);
		newDecal._InvWorldMat = inverse(newDecal._WorldMat);
		newDecal.albedoTexIdx = 2 * m_NumDecal;
		newDecal.normalTexIdx = newDecal.albedoTexIdx + 1;
		m_Decals[m_NumDecal++] = newDecal;

		newDecal._WorldMat = translate(Vector3(400.0f, 85.0f, 0.0f)) * scale(Vector3(200.0f));	// (1300.0f, 185.0f, 445.0f)
		newDecal._WorldMat = MMATH::transpose(newDecal._WorldMat);
		newDecal._InvWorldMat = inverse(newDecal._WorldMat);
		newDecal.albedoTexIdx = m_NumDecalTextures-1;
		newDecal.normalTexIdx = -1;
		// newDecal.normalTexIdx
		m_Decals[m_NumDecal++] = newDecal;
	}

	// decal buffer
	{
		m_DecalBuffer.Create(pDevice, L"DecalBuffer", m_NumDecal, sizeof(DecalData), m_Decals.data());
	}

}
