#pragma once
#include "pch.h"
#include "Camera.h"
#include "ColorBuffer.h"
#include "ShadowBuffer.h"

namespace MyDirectX
{
	class CascadedShadowMap
	{
	public:
		static constexpr uint32_t s_NumCascades = 4;
		static constexpr uint32_t s_ShadowMapSize = 1024;
		static constexpr DXGI_FORMAT s_ShadowMapFormat = DXGI_FORMAT_R16_UNORM;

		void Init(ID3D12Device* pDevice, const Math::Vector3& lightDir, const Math::Camera& camera);

		void PrepareCascades(const Math::Vector3& lightDir, const Math::Camera& camera);

		void Clean();

		Math::Vector3 m_LightDir;
		Math::Vector4 m_CascadeSplits;
		Math::Vector3 m_CamPos[s_NumCascades];		// 其实没用，还是记录一下	-2020-4-24
		Math::Matrix4 m_ViewProjMat[s_NumCascades];
		ColorBuffer m_LightShadowArray;
		ShadowBuffer m_LightShadowTempBuffer;
	};	

}
