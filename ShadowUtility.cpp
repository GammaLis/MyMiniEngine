#include "ShadowUtility.h"
#include "Camera.h"

namespace MyDirectX
{
	using namespace Math;
	using namespace DirectX;

	static const XMMATRIX TextureSpaceMat =
	{
		0.5f,  0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f,  0.0f, 1.0f, 0.0f,
		0.5f,  0.5f, 0.0f, 1.0f
	};

	void CascadedShadowMap::Init(ID3D12Device* pDevice, const Math::Vector3& lightDir, const Math::Camera& camera)
	{
		PrepareCascades(lightDir, camera);

		// resources
		{
			m_LightShadowArray.CreateArray(pDevice, L"CascadedShadowArray", s_ShadowMapSize, s_ShadowMapSize, s_NumCascades, s_ShadowMapFormat);
			m_LightShadowTempBuffer.Create(pDevice, L"ShadowTempBuffer", s_ShadowMapSize, s_ShadowMapSize);
		}
	}

	void CascadedShadowMap::PrepareCascades(const Math::Vector3& lightDir, const Math::Camera& camera)
	{
		// frustum
		// lambda 可以调整，试试效果
		float lambda = 0.5f;

		float nearClip = camera.GetNearClip();
		float farClip = camera.GetFarClip();

		float cascadeSplits[s_NumCascades + 1] = { 0.0f };
		cascadeSplits[0] = nearClip;
		cascadeSplits[s_NumCascades] = farClip;

		float ratio = farClip / nearClip;
		float range = farClip - nearClip;

		// zi = lambda * n * (f/n)^(i/N) + (1-lambda) * (n + (i/N) * (f-n))
		for (uint32_t slice = 1; slice < s_NumCascades; ++slice)
		{
			float v = float(slice) / float(s_NumCascades);
			float p0 = nearClip * std::powf(ratio, v);
			float p1 = nearClip + v * range;
			float zi = lambda * p0 + (1 - lambda) * p1;

			cascadeSplits[slice] = zi;
		}
		m_CascadeSplits = Vector4(cascadeSplits[1], cascadeSplits[2], cascadeSplits[3], cascadeSplits[4]);

		const auto& viewProjMat = camera.GetViewProjMatrix();
		const auto& invViewProj = Math::Invert(viewProjMat);
		// frustum corners
		Vector3 frustumCorners[8] = 
		{
			Vector3(-1.0f,  1.0f, 1.0f),
			Vector3(-1.0f, -1.0f, 1.0f),
			Vector3( 1.0f, -1.0f, 1.0f),
			Vector3( 1.0f,  1.0f, 1.0f),

			Vector3(-1.0f,  1.0f, 0.0f),
			Vector3(-1.0f, -1.0f, 0.0f),
			Vector3( 1.0f, -1.0f, 0.0f),
			Vector3( 1.0f,  1.0f, 0.0f)
		};
		for (uint32_t i = 0; i < 8; ++i)
		{
			frustumCorners[i] = Vector3(invViewProj * frustumCorners[i]);
		}
#pragma region Frustum Camera
		static uint32_t s_Times = 0;
		if (s_Times++ > 0)
			goto End;

		static XMMATRIX FrustumViewProj;
		static XMMATRIX FrustumViewMat;
		{
			XMVECTOR TotalCamPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
			XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMMATRIX TotalViewMat = XMMatrixLookToLH(TotalCamPos, lightDir, worldUp);
			XMMATRIX TotalInvViewMat = XMMatrixInverse(nullptr, TotalViewMat);
			const float MinBound = -100000.0f;
			const float MaxBound = +100000.0f;
			XMVECTOR minBound = XMVectorSet(MaxBound, MaxBound, MaxBound, MaxBound);
			XMVECTOR maxBound = XMVectorSet(MinBound, MinBound, MinBound, MinBound);
			for (uint32_t i = 0; i < 6; ++i)
			{
				XMVECTOR corner = XMVector3TransformCoord(XMVECTOR(frustumCorners[i]), TotalViewMat);
				minBound = XMVectorMin(minBound, corner);
				maxBound = XMVectorMax(maxBound, corner);
			}

			XMVECTOR center = (minBound + maxBound) * 0.5f;
			XMVECTOR extent = (maxBound - minBound) * 0.5f;
			
			Vector3 offset = Vector3(center);
			offset.SetZ(Vector3(minBound).GetZ());

			Vector3 fExtent = Vector3(extent);
			// 调整宽度高度一致
			if ((float)fExtent.GetX() > (float)fExtent.GetY())
				fExtent.SetY(fExtent.GetX());
			else
				fExtent.SetX(fExtent.GetY());
			
			TotalCamPos = XMVector4Transform(XMVectorSet(offset.GetX(), offset.GetY(), offset.GetZ(), 1.0f), TotalInvViewMat);
			XMMATRIX TotalProjMat = XMMatrixOrthographicLH(2.0f * fExtent.GetX(), 2.0f * fExtent.GetY(), 0.0f, 2.0f * fExtent.GetZ());
			
			// 测试数值，效果较好
			TotalProjMat = XMMatrixOrthographicLH(3000.0f, 3000.0f, 0.0f, 6000.0f);
			TotalCamPos = 2000.0f * -lightDir;

			TotalViewMat = XMMatrixLookToLH(TotalCamPos, lightDir, worldUp);
			XMMATRIX newViewProjMat = TotalViewMat * TotalProjMat;
			newViewProjMat *= TextureSpaceMat;

			FrustumViewProj = newViewProjMat;
			FrustumViewMat = TotalViewMat;
		}
		End:
#pragma endregion

 		Vector3 frustumRays[4];
		for (uint32_t i = 0; i < 4; ++i)
		{
			frustumRays[i] = frustumCorners[i + 4] - frustumCorners[i];
		}

		Vector3 worldCorners[8];
		for (uint32_t i = 0; i < 4; ++i)
			worldCorners[i] = frustumCorners[i];
		uint32_t nearIndex = 0, farIndex = 4;
		float invRange = 1.0f / range;
		for (uint32_t cascade = 0; cascade < s_NumCascades; ++cascade)
		{
			float s = (cascadeSplits[cascade + 1] - nearClip) * invRange;
			for (uint32_t i = 0; i < 4; ++i)
			{				
				worldCorners[farIndex + i] = frustumCorners[i] + frustumRays[i] * s;
			}

			std::swap(nearIndex, farIndex);

			Vector3 frustumCenter = Vector3(0.0f);
			for (uint32_t i = 0; i < 8; ++i)
			{
				frustumCenter += worldCorners[i];
			}
			frustumCenter = frustumCenter * (1.0f / 8.0f);

			// light view matrix
			Vector3 upDir = Vector3(0.0f, 1.0f, 0.0f);

			XMVECTOR lightCamPos = XMVECTOR(frustumCenter);
			XMVECTOR upVec = XMVECTOR(upDir);

			XMMATRIX lightViewMat = XMMatrixLookToLH(lightCamPos, lightDir, upVec);	// XMMatrixLookToLH
			XMMATRIX lightInvViewMat = XMMatrixInverse(nullptr, lightViewMat);
			const float MinBound = -100000.0f;
			const float MaxBound = +100000.0f;
			XMVECTOR minBound = XMVectorSet(MaxBound, MaxBound, MaxBound, MaxBound);
			XMVECTOR maxBound = XMVectorSet(MinBound, MinBound, MinBound, MinBound);
			for (uint32_t i = 0; i < 8; ++i)
			{
				XMVECTOR corner = XMVector3TransformCoord(XMVECTOR(worldCorners[i]), lightViewMat);	// lightViewMat FrustumViewMat
				minBound = XMVectorMin(minBound, corner);
				maxBound = XMVectorMax(maxBound, corner);
			}

			Vector3 fMinBound = Vector3(minBound);
			Vector3 fMaxBound = Vector3(maxBound);

			XMVECTOR center = (minBound + maxBound) * 0.5f;
			XMVECTOR extent = (maxBound - minBound) * 0.5f;
			
			Vector3 offset = Vector3(center);
			offset.SetZ(Vector3(minBound).GetZ());

			Vector3 fCenter = Vector3(center);
			Vector3 fExtent = Vector3(extent);
			// 调整宽度高度一致
			if ((float)fExtent.GetX() > (float)fExtent.GetY())
				fExtent.SetY(fExtent.GetX());
			else
				fExtent.SetX(fExtent.GetY());

			Vector3 minExtent = -fExtent;
			minExtent.SetZ(0);
			Vector3 maxExtent = fExtent;
			maxExtent.SetZ(2.0f * fExtent.GetZ());

			// quantized position
			uint32_t bufferPrecision = 16;
			Vector3 rcpDimensions = Recip(fExtent * 2.0f);
			Vector3 scale = Vector3( (float)s_ShadowMapSize, (float)s_ShadowMapSize, (float)((1 << bufferPrecision) - 1) ) * rcpDimensions;
			offset = Floor(offset * scale) / scale;

			// lightCamPos += XMVector4Transform(XMVectorSet(offset.GetX(), offset.GetY(), offset.GetZ(), 0.0f), lightInvViewMat);
			lightCamPos  = XMVector4Transform(XMVectorSet(offset.GetX(), offset.GetY(), offset.GetZ(), 1.0f), lightInvViewMat);

			XMMATRIX orthoProjMat = XMMatrixOrthographicLH(2.0f * fExtent.GetX(), 2.0f * fExtent.GetY(), 0.0f, 2.0f * fExtent.GetZ());
			//XMMATRIX orthoProjMat = XMMatrixOrthographicOffCenterLH(
			//	fMinBound.GetX(), fMaxBound.GetX(), fMinBound.GetY(), fMaxBound.GetY(), fMinBound.GetZ(), fMaxBound.GetZ());

			XMMATRIX newViewMat = XMMatrixLookToLH(lightCamPos, lightDir, upVec);	// XMMatrixLookToLH
			// this is better
			// XMMATRIX newViewMat = lightViewMat * XMMatrixTranslationFromVector(-XMVECTOR(offset));

			XMMATRIX newViewProjMat = newViewMat * orthoProjMat;
			// XMMATRIX newViewProjMat = FrustumViewMat * orthoProjMat;	// 统一ViewSpace
			
			//~ Begin debug
		#if 0
			Vector3 localCorner[8];
			for (uint32_t i = 0; i < 8; ++i)
			{
				XMVECTOR corner = XMVector3TransformCoord(XMVECTOR(worldCorners[i]), newViewProjMat);
				localCorner[i] = Vector3(corner);
			}
		#endif
			//~ End

			newViewProjMat = newViewProjMat * TextureSpaceMat;

			// 缓存投影矩阵
			m_ViewProjMat[cascade] = Matrix4(newViewProjMat);
			m_CamPos[cascade] = Vector3(lightCamPos);
		}

		// 目前，只渲染一级阴影
		m_ViewProjMat[0] = Matrix4(FrustumViewProj);

	}
	
	void CascadedShadowMap::Clean()
	{
		m_LightShadowArray.Destroy();
		m_LightShadowTempBuffer.Destroy();
	}
}
