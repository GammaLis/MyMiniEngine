#include "ParticleEffectManager.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "TextureManager.h"

#include "Camera.h"

// compiled shader bytecode
#include "ParticleSpawnCS.h"
#include "ParticleUpdateCS.h"
#include "ParticleDispatchIndirectArgsCS.h"

#include "ParticleFinalDispatchIndirectArgsCS.h"

// non tiled rendering
#include "ParticleVS.h"
#include "ParticleNoSortVS.h"
#include "ParticlePS.h"

#define EFFECTS_ERROR uint32_t(0xFFFFFFFF)

#define MAX_TOTAL_PARTICLES 0x40000		// 256k (18-bit indices)
#define MAX_PARTICLES_PER_BIN 1024
#define BIN_SIZE_X 128
#define BIN_SIZE_Y 64
#define TILE_SIZE 16

// it's good to have 32 tiles per bin to maximize the tile culling phase
#define TILES_PER_BIN_X (BIN_SIZE_X / TILE_SIZE)
#define TILES_PER_BIN_Y (BIN_SIZE_Y / TILE_SIZE)
#define TILES_PER_BIN (TILES_PER_BIN_X * TILES_PER_BIN_Y)

using namespace MyDirectX::ParticleEffects;
using namespace MyDirectX;

struct CBChangesPerView
{
	Matrix4 _InvViewMat;
	Matrix4 _ViewProjMat;

	float _VertCotangent;
	float _AspectRatio;
	float _RcpFarZ;
	float _InvertZ;

	float _BufferWidth;
	float _BufferHeight;
	float _RcpBufferWidth;
	float _RcpBufferHeight;

	uint32_t _BinsPerRow;
	uint32_t _TileRowPitch;
	uint32_t _TilesPerRow;
	uint32_t _TilesPerCol;
};

RandomNumberGenerator ParticleEffects::s_RNG;

void ParticleEffectManager::Init(ID3D12Device* pDevice, uint32_t maxDisplayWidth, uint32_t maxDisplayHeight)
{
	m_Device = pDevice;

	// root signature
	{
		D3D12_SAMPLER_DESC SamplerBilinearBorderDesc = Graphics::s_CommonStates.SamplerPointBorderDesc;
		SamplerBilinearBorderDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		// D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT 区别？-20-2-28
		m_ParticleRS.Reset(5, 3);
		m_ParticleRS[0].InitAsConstants(0, 3);
		m_ParticleRS[1].InitAsConstantBuffer(1);
		m_ParticleRS[2].InitAsConstantBuffer(2);
		m_ParticleRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10);
		m_ParticleRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 8);
		m_ParticleRS.InitStaticSampler(0, SamplerBilinearBorderDesc);
		m_ParticleRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointBorderDesc);
		m_ParticleRS.InitStaticSampler(2, Graphics::s_CommonStates.SamplerPointClampDesc);
			// Graphics::s_CommonStates.SamplerLinearClampDesc
		m_ParticleRS.Finalize(pDevice, L"Particle Effects");
	}

	// PSOs
	{
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(m_ParticleRS); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		// 每个ParticleEffect相关
		CreatePSO(m_ParticleSpawnCS, ParticleSpawnCS);
		CreatePSO(m_ParticleUpdateCS, ParticleUpdateCS);
		CreatePSO(m_ParticleDispatchIndirectArgsCS, ParticleDispatchIndirectArgsCS);

		// 粒子总数
		CreatePSO(m_ParticleFinalDispatchIndirectArgsCS, ParticleFinalDispatchIndirectArgsCS);


#undef CreatePSO

		// vs ps render, no tiles
		{
			m_NoTileRasterizationPSO[0].SetRootSignature(m_ParticleRS);
			m_NoTileRasterizationPSO[0].SetInputLayout(0, nullptr);
			m_NoTileRasterizationPSO[0].SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
			m_NoTileRasterizationPSO[0].SetVertexShader(ParticleVS, sizeof(ParticleVS));
			m_NoTileRasterizationPSO[0].SetPixelShader(ParticlePS, sizeof(ParticlePS));
			m_NoTileRasterizationPSO[0].SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
			m_NoTileRasterizationPSO[0].SetBlendState(Graphics::s_CommonStates.BlendPreMultiplied);
			m_NoTileRasterizationPSO[0].SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadOnly);	// ZWrite off, ZTest Greater
			m_NoTileRasterizationPSO[0].SetRenderTargetFormat(Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat(),
				Graphics::s_BufferManager.m_SceneDepthBuffer.GetFormat());
			m_NoTileRasterizationPSO[0].Finalize(pDevice);

			m_NoTileRasterizationPSO[1] = m_NoTileRasterizationPSO[0];
			m_NoTileRasterizationPSO[1].SetVertexShader(ParticleNoSortVS, sizeof(ParticleNoSortVS));
			m_NoTileRasterizationPSO[1].Finalize(pDevice);
		}
	}

	// buffers
	{
		m_SpriteVertexBuffer.Create(pDevice, L"ParticleEffects::SpriteVertexBuffer", MAX_TOTAL_PARTICLES, sizeof(ParticleVertex));

		m_SpriteIndexBuffer.Create(pDevice, L"ParticleEffects::SpriteIndexBuffer", MAX_TOTAL_PARTICLES, sizeof(UINT));

		// draw indirect args
		alignas(16) UINT initialDrawIndirectArgs[4] = { 4, 0, 0 , 0 };
		m_DrawIndirectArgs.Create(pDevice, L"ParticleEffects::DrawIndirectArgs", 1, sizeof(D3D12_DRAW_ARGUMENTS), initialDrawIndirectArgs);
		// dispatch indirect args
		alignas(16) UINT initialDispatchIndirectArgs[6] = { 0, 1, 1, 0, 1, 1 };
		m_FinalDispatchIndirectArgs.Create(pDevice, L"ParticleEffects::FinalDispatchIndirectArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS), initialDispatchIndirectArgs);

	}

	// textures
	{
		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Width = 64;
		texDesc.Height = 64;
		texDesc.DepthOrArraySize = 16;
		texDesc.MipLevels = 4;
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Alignment = 0x10000;	// 64K, alignment may be one of 0, 4KB, 64KB, or 4MB
			// if alignmnet is set to 0, the runtime will use 4MB for MSAA textures and 64KB for everything else.
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		ID3D12Resource* tex = nullptr;
		ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
			&texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)));
		tex->SetName(L"Particle TexArray");
		m_TextureArray = GpuResource(tex, D3D12_RESOURCE_STATE_COPY_DEST);
		// IUnknown::Release - Decrements the reference count for an interface on a COM object.
		// ??? -20-2-28
		tex->Release();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2DArray.MipLevels = 4;
		srvDesc.Texture2DArray.ArraySize = 16;

		m_TexArraySRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		pDevice->CreateShaderResourceView(m_TextureArray.GetResource(), &srvDesc, m_TexArraySRV);
	}

	if (m_ReproFrame > 0)
		s_RNG.SetSeed(1);

	m_TotalElapsedFrames = 0;
	m_InitCompleted = true;
}

void ParticleEffectManager::Shutdown()
{
	ClearAll();

	m_SpriteVertexBuffer.Destroy();
	m_SpriteIndexBuffer.Destroy();

	m_DrawIndirectArgs.Destroy();
	m_FinalDispatchIndirectArgs.Destroy();

	m_TextureArray.Destroy();
}

void ParticleEffectManager::ClearAll()
{
	m_ActiveParticleEffects.clear();
	m_ParticleEffectsPool.clear();
	m_TextureNameArray.clear();
}

// returns index into pool
ParticleEffectManager::EffectHandle ParticleEffectManager::PreLoadEffectResources(ParticleEffectProperties& effectProperties)
{
	if (!m_InitCompleted)
		return EFFECTS_ERROR;

	std::lock_guard<std::mutex> lockGuard(m_TextureMutex);
	MaintainTextureList(effectProperties);
	m_ParticleEffectsPool.emplace_back(new ParticleEffect(effectProperties));

	EffectHandle index = (EffectHandle)m_ParticleEffectsPool.size() - 1;
	m_ParticleEffectsPool[index]->LoadDeviceResources(m_Device);

	return index;
}

// returns index into active
ParticleEffectManager::EffectHandle ParticleEffectManager::InstantiateEffect(EffectHandle effectHandle)
{
	if (!m_InitCompleted || effectHandle >= m_ParticleEffectsPool.size() || effectHandle < 0)
		return EFFECTS_ERROR;

	ParticleEffect* effect = m_ParticleEffectsPool[effectHandle].get();
	if (effect != nullptr)
	{
		std::lock_guard<std::mutex> lockGuard(m_InstantiateEffectFromPoolMutex);
		m_ActiveParticleEffects.push_back(effect);
	}
	else
		return EFFECTS_ERROR;

	EffectHandle index = (EffectHandle)m_ActiveParticleEffects.size() - 1;
	return index;
}

// returns index into active
ParticleEffectManager::EffectHandle ParticleEffectManager::InstantiateEffect(ParticleEffectProperties& effectProperties)
{
	if (!m_InitCompleted)
		return EFFECTS_ERROR;

	std::lock_guard<std::mutex> lockGuard(m_InstantiateNewEffectMutex);
	MaintainTextureList(effectProperties);
	ParticleEffect* newEffect = new ParticleEffect(effectProperties);
	m_ParticleEffectsPool.emplace_back(newEffect);	// emplace_back 移动构造
	m_ActiveParticleEffects.push_back(newEffect);

	EffectHandle index = (EffectHandle)m_ActiveParticleEffects.size() - 1;
	m_ActiveParticleEffects[index]->LoadDeviceResources(m_Device);

	return index;
}

/// particle update
void ParticleEffectManager::Update(ComputeContext& context, float deltaTime)
{
	if (!m_Enabled || !m_InitCompleted || m_ActiveParticleEffects.empty())
		return;

	if (++m_TotalElapsedFrames == m_ReproFrame)
		m_PauseSim = true;

	if (m_PauseSim)
		return;

	// fill CounterBuffer with 0
	// 重置SpriteVertexBuffer
	context.ResetCounter(m_SpriteVertexBuffer);

	context.SetRootSignature(m_ParticleRS);
	context.SetConstants(0, deltaTime);
	context.TransitionResource(m_SpriteVertexBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.SetDynamicDescriptor(4, 0, m_SpriteVertexBuffer.GetUAV());

	// update active effects
	for (size_t i = 0, imax = m_ActiveParticleEffects.size(); i < imax; ++i)
	{
		m_ActiveParticleEffects[i]->Update(context, deltaTime);

		if (m_ActiveParticleEffects[i]->GetElapsedTime() >= m_ActiveParticleEffects[i]->GetLifeTime())
		{
			// erase from vector
			std::lock_guard<std::mutex> lockGuard(m_EraseEffectMutex);
			auto iter = m_ActiveParticleEffects.begin() + i;
			m_ActiveParticleEffects.erase(iter);
		}
	}

	SetFinalBuffers(context);
}

void ParticleEffectManager::Render(CommandContext& context, const Math::Camera& camera, ColorBuffer& colorTarget, DepthBuffer& depthBuffer, ColorBuffer& linearDepth)
{
	if (!m_Enabled || !m_InitCompleted || m_ActiveParticleEffects.empty())
		return;

	uint32_t width = colorTarget.GetWidth();
	uint32_t height = colorTarget.GetHeight();

	ASSERT(width == depthBuffer.GetWidth() && height == depthBuffer.GetHeight() &&
		width == linearDepth.GetWidth() && height == linearDepth.GetHeight(),
		"There is a mismatch in buffer dimensions for rendering particles"
	);

	uint32_t binsPerRow = 4 * DivideByMultiple(width, 4 * BIN_SIZE_X);

	CBChangesPerView cbChangesPerView;
	cbChangesPerView._ViewProjMat = Transpose(camera.GetViewProjMatrix());
	cbChangesPerView._InvViewMat = Transpose(Invert(camera.GetViewMatrix()));
	float hCot = camera.GetProjMatrix().GetX().GetX();	// m00
	float vCot = camera.GetProjMatrix().GetY().GetY();	// m11
	cbChangesPerView._VertCotangent = vCot;
	cbChangesPerView._AspectRatio = hCot / vCot;	// aspect ratio - H / W, 高宽比
	cbChangesPerView._RcpFarZ = 1.0f / camera.GetFarClip();
	cbChangesPerView._InvertZ = camera.GetNearClip() / (camera.GetFarClip() - camera.GetNearClip());
	cbChangesPerView._BufferWidth = (float)width;
	cbChangesPerView._BufferHeight = (float)height;
	cbChangesPerView._RcpBufferWidth = 1.0f / width;
	cbChangesPerView._RcpBufferHeight = 1.0f / height;
	cbChangesPerView._BinsPerRow = binsPerRow;
	cbChangesPerView._TileRowPitch = binsPerRow * TILES_PER_BIN_X;
	cbChangesPerView._TilesPerRow = DivideByMultiple(width, TILE_SIZE);
	cbChangesPerView._TilesPerCol = DivideByMultiple(height, TILE_SIZE);

	// for now, UAV load support for R11G11B10 is required to read-modify-write the color buffer, 
	// but the compositing could be deferred
	WARN_ONCE_IF(m_EnableTiledRendering && !GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT,
		"Unable to composite tiled particles without support for R11G11B10F UAV loads");
	m_EnableTiledRendering = m_EnableTiledRendering && GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT;

	if (m_EnableTiledRendering)
	{

	}
	else
	{
		GraphicsContext& gfxContext = context.GetGraphicsContext();
		gfxContext.SetRootSignature(m_ParticleRS);
		gfxContext.SetDynamicConstantBufferView(1, sizeof(CBChangesPerView), &cbChangesPerView);

		RenderSprites(gfxContext, colorTarget, depthBuffer, linearDepth);
	}

}

void ParticleEffectManager::ResetEffect(EffectHandle effectId)
{
	if (!m_InitCompleted || m_ActiveParticleEffects.empty() || m_PauseSim || effectId >= m_ActiveParticleEffects.size())
		return;

	m_ActiveParticleEffects[effectId]->Reset();
}

float ParticleEffectManager::GetCurrentLife(EffectHandle effectId)
{
	if (!m_InitCompleted || m_ActiveParticleEffects.empty() || m_PauseSim || effectId >= m_ActiveParticleEffects.size())
		return -1.0f;

	return m_ActiveParticleEffects[effectId]->GetElapsedTime();
}

void ParticleEffectManager::MaintainTextureList(ParticleEffectProperties& effectProperties)
{
	std::wstring name = effectProperties.TexturePath;

	for (size_t i = 0, imax = m_TextureNameArray.size(); i < imax; ++i)
	{
		if (name.compare(m_TextureNameArray[i]) == 0)
		{
			effectProperties.EmitProperties.TextureID = static_cast<UINT>(i);
			return;
		}
	}

	m_TextureNameArray.push_back(name);
	UINT textureId = (UINT)(m_TextureNameArray.size() - 1);
	effectProperties.EmitProperties.TextureID = textureId;

	ASSERT(m_Device != nullptr);
	const ManagedTexture* managedTex = Graphics::s_TextureManager.LoadDDSFromFile(m_Device, name, true);
	managedTex->WaitForLoad();	// 注：感觉不需要Wait	-20-2-26

	GpuResource& particleTexture = *const_cast<ManagedTexture*>(managedTex);
	CommandContext::InitializeTextureArraySlice(m_TextureArray, textureId, particleTexture);

}

void ParticleEffectManager::SetFinalBuffers(ComputeContext& computeContext)
{
	computeContext.TransitionResource(m_SpriteVertexBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	computeContext.TransitionResource(m_FinalDispatchIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.TransitionResource(m_DrawIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	computeContext.SetPipelineState(m_ParticleFinalDispatchIndirectArgsCS);

	computeContext.SetDynamicDescriptor(3, 0, m_SpriteVertexBuffer.GetCounterSRV(computeContext));
	computeContext.SetDynamicDescriptor(4, 0, m_FinalDispatchIndirectArgs.GetUAV());
	computeContext.SetDynamicDescriptor(4, 1, m_DrawIndirectArgs.GetUAV());

	computeContext.Dispatch(1, 1, 1);
	
}

void ParticleEffectManager::RenderSprites(GraphicsContext& gfxContext, ColorBuffer& colorTarget, DepthBuffer& depthTarget, ColorBuffer& linearDepth)
{
	if (m_EnableSpriteSort)
	{
		// TO DO
		// ... 
	}

	uint32_t width = colorTarget.GetWidth(), height = colorTarget.GetHeight();
	
	D3D12_RECT scissor;
	scissor.left = 0;
	scissor.top = 0;
	scissor.right = (LONG)width;
	scissor.bottom = (LONG)height;

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)width;
	viewport.Height = (float)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	gfxContext.TransitionResource(m_SpriteVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(m_SpriteIndexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(m_TextureArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(m_DrawIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	
	gfxContext.SetRootSignature(m_ParticleRS);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	gfxContext.SetPipelineState(m_NoTileRasterizationPSO[m_EnableSpriteSort ? 0 : 1]);
	
	gfxContext.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.TransitionResource(depthTarget, D3D12_RESOURCE_STATE_DEPTH_READ);
	gfxContext.SetRenderTarget(colorTarget.GetRTV(), depthTarget.GetDSV_ReadOnly());
	gfxContext.SetViewportAndScissor(viewport, scissor);

	gfxContext.SetDynamicDescriptor(3, 0, m_SpriteVertexBuffer.GetSRV());
	gfxContext.SetDynamicDescriptor(3, 1, m_SpriteIndexBuffer.GetSRV());
	gfxContext.SetDynamicDescriptor(3, 2, m_TexArraySRV);
	gfxContext.SetDynamicDescriptor(3, 3, linearDepth.GetSRV());

	gfxContext.DrawIndirect(m_DrawIndirectArgs);
}

void ParticleEffectManager::RenderTiles(ComputeContext& computeContext, ColorBuffer& colorTarget, ColorBuffer& linearDepth)
{

}

/**
	https://docs.microsoft.com/zh-cn/windows/win32/api/d3d12/ns-d3d12-d3d12_resource_desc
	D3D12_RESOURCE_DESC
	2 common resources are buffers and textures.
	>> Buffers
	buffers are a contiguous memory region. Width may be between 1 and either the MaxGpuVirtualAddressBitsPerResource
field of D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT for reserved resources or the MaxGpuVirtualAddressBitsPerProcess
field for committed resources.
	Alignment must be 64kB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
	Hight, DepthOrArraySize, and MipLevels must be 1
	Format must be DXGI_FORAMT_UNKNOWN
	SampleDesc.Count must be 1 and Qualtity must be 0
	Layout must be D3D12_TEXTURE_LAYOUT_ROW_MAJOR, as buffer memory layouts are understood by applications
and row-major texture data is commonly marshaled through buffers.
	Flags must still be accurately filled out by applications for buffers, with minor exceptions.
However, applications can use the most amount of capability support without concern about the efficiency 
impact on buffers. The flags field is meant to control properties related to textures.

	>> Textures
	textures are a multi-dimensional arrangement of texels in a contiguous region of memory, heavily optimized
to maximize bandwidth for rendering and sampling. Texture sizes are hard to predict and vary from adapter 
to adapter. Applications must use ID3D12Device::GetResourceAllocationInfo to accurately understand their size.
	Width, Height, and DepthOrArraySize must be between 1 and the maximum dimension supported for the particular
feature level and texture dimension.
	For TEXTURE1D:
		Width 
		Height must be 1
		DepthOrArraySize is interpreted as array size
	For TEXTURE2D:
		Width & Height
		DepthOrArraySize is interpreted as array size
	For TEXTURE3D:
		Width & Height & DepthOrArraySize
		DepthOrArraySize is interpreted as depth

	> Alignment
	Alignment may be one of 0, 4KB, 64KB or 4 MB
	if Alignment is set to 0, the runtime will use 4MB for MSAA textures and 64KB for everything else. 
The application may choose smaller alignments than these defaults for a couple of texture types 
when the texture is small. Textures with UNKNOWN layout and MSAA may be created with 64KB alignment
(if they pass the small size restriction detailed below).
	Textures with UNKNOWN layout without MSAA and without render-target nor depth-stencil flags may be
created with 4KB Alignment (again, passing the small size restriction).
	> MipLevels
	MipLevels may be 0, or 1 to the maximum mip levels supported by the Width, Height and DepthOrArraySize dimensions.
When 0 is used, the API will automatically calculate the maximum mip levels supported and use that.
*/
