#include "MyBaseApp.h"
#include "DeviceResources.h"
#include "GameTimer.h"
#include <d3dcompiler.h>

using namespace MyDirectX;
using namespace DirectX;

using Microsoft::WRL::ComPtr;

struct MyVertex
{
	XMFLOAT3 position;
	XMFLOAT2 uv;
	XMFLOAT4 color;
};

struct CBPerFrame
{
	XMFLOAT4 baseColor;
};

MyBaseApp::MyBaseApp(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	:MyApp(hInstance, title, width, height)
{
}

void MyBaseApp::Update()
{
	// TODO
	float totalTime = m_Timer->TotalTime();
	float val = 0.5f * sin(totalTime) + 0.5f;

	m_BaseColor = XMFLOAT4(val, 1.0f, 1.0f, 1.0f);

	CBPerFrame cbPerFrame = {};
	cbPerFrame.baseColor = m_BaseColor;

	memcpy(m_pCBVDataBegin, &cbPerFrame, sizeof(CBPerFrame));
}

void MyBaseApp::Render()
{
	m_DeviceResources->Prepare(m_PipelineState.Get());

	// TODO
	//  set necessary state
	{
		pCmdList->SetGraphicsRootSignature(m_RootSignature.Get());		// m_EmptyRootSignature		m_RootSignature

		ID3D12DescriptorHeap* pDescHeaps[] =
		{
			m_CBVDescHeap.Get(),
			// m_SRVDescHeap.Get(),			
		};
		pCmdList->SetDescriptorHeaps(_countof(pDescHeaps), pDescHeaps);
		// ID3D12DescriptorHeap* pDescHeaps[] ={ m_CBVDescHeap.Get(), m_SRVDescHeap.Get() };
		// ID3D12CommandList::SetDescriptorHeaps: pDescriptorHeaps[1] sets a descriptor heap type that appears earlier in the pDescriptorHeaps array.
		// Only one of any given descriptor heap type can be set at a time.[EXECUTION ERROR #554: SET_DESCRIPTOR_HEAP_INVALID]
		// 
		// SetDescriptorHeaps can set either:
		//	1 CBV/SRV/UAV heap
		//	1 Sampler heap
		//	1 CBV/SRV/UAV heap and 1 sampler heap

		pCmdList->SetGraphicsRootDescriptorTable(0, m_CBVDescHeap->GetGPUDescriptorHandleForHeapStart());

		ID3D12DescriptorHeap* pDescHeaps2[] =
		{
			m_SRVDescHeap.Get(),			
		};
		pCmdList->SetDescriptorHeaps(_countof(pDescHeaps2), pDescHeaps2);
		pCmdList->SetGraphicsRootDescriptorTable(1, m_SRVDescHeap->GetGPUDescriptorHandleForHeapStart());

		pCmdList->RSSetViewports(1, &m_DeviceResources->GetScreenViewport());
		pCmdList->RSSetScissorRects(1, &m_DeviceResources->GetScissorRect());
	}

	// clear
	{
		auto rtv = m_DeviceResources->GetRenderTargetView();
		auto dsv = m_DeviceResources->GetDepthStencilView();

		const float* clearColor = reinterpret_cast<const float*>(&Colors::LightSeaGreen);
		pCmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		pCmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		pCmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	}

	// draw
	{
		pCmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
		pCmdList->IASetIndexBuffer(&indexBufferView);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		pCmdList->DrawIndexedInstanced(m_IndexCount, 1, m_IndexOffset, m_VertexOffset, 0);
	}

	m_DeviceResources->Present();
}

void MyBaseApp::InitAssets()
{
	m_CSUV_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	ID3D12GraphicsCommandList* pCmdList = m_DeviceResources->GetCommandList();
	ID3D12CommandAllocator* pCmdAllocator = m_DeviceResources->GetCommandAllocator();
	ID3D12CommandQueue* pCmdQueue = m_DeviceResources->GetCommandQueue();

	pCmdAllocator->Reset();
	pCmdList->Reset(pCmdAllocator, nullptr);

	InitRootSignatures();
	InitShadersAndInputElements();
	InitPipelineStates();

	InitGeometryBuffers();
	InitConstantBuffers();
	InitTextures();

	// 
	CustomInit();

	ThrowIfFailed(pCmdList->Close());

	ID3D12CommandList* cmdLists[] = { pCmdList };
	pCmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	m_DeviceResources->WaitForGpu();

	DisposeUploaders();
}

void MyBaseApp::InitRootSignatures()
{
#pragma region EmptyRootSignature
	// DirectX-Graphics-Samples/D3D12HelloTriangle.cpp
	// create an empty root signature
	CD3DX12_ROOT_SIGNATURE_DESC emptyRootSignatureDesc = {};
	emptyRootSignatureDesc.Init(
		0,
		nullptr,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	ThrowIfFailed(D3D12SerializeRootSignature(
		&emptyRootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signature,
		&error
	));

	ThrowIfFailed(pDevice->CreateRootSignature(
		0,
		signature->GetBufferPointer(),
		signature->GetBufferSize(),
		IID_PPV_ARGS(m_EmptyRootSignature.ReleaseAndGetAddressOf())
	));
#pragma endregion

#pragma region NormalRootSignature
	// DirectX-Graphics-Samples/D3D12HelloConstantBuffers.cpp, D3D12HelloTexture.cpp
	// create a root signature consisting of 2 descriptor table2 with a CBV and a SRV
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = { };

		// this is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion 
		// returned will not be greater than this
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];		// descriptor tables
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];

		ranges[0].Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			1,
			0,
			0,
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
		);
		ranges[1].Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			1,
			0,
			0,
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
		);
		rootParameters[0].InitAsDescriptorTable(
			1,
			&ranges[0],
			D3D12_SHADER_VISIBILITY_PIXEL
		);
		rootParameters[1].InitAsDescriptorTable(
			1,
			&ranges[1],
			D3D12_SHADER_VISIBILITY_PIXEL
		);

		// samplers
		D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MipLODBias = 0;
		samplerDesc.MaxAnisotropy = 0;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.ShaderRegister = 0;
		samplerDesc.RegisterSpace = 0;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// allow input layout and deny uneccessary access to certain pipeline stages
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		// D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;


		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			_countof(rootParameters),
			rootParameters,
			1,
			&samplerDesc,
			rootSignatureFlags
		);
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
			&rootSignatureDesc,
			featureData.HighestVersion,
			&signature,
			&error
		));
		ThrowIfFailed(pDevice->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(m_RootSignature.ReleaseAndGetAddressOf())
		));
	}
#pragma endregion


}

void MyBaseApp::InitShadersAndInputElements()
{
	// shaders
	{
#ifdef _DEBUG
		// enable better shader debugging with the graphics debugging tools
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif
		ThrowIfFailed(D3DCompileFromFile(
			L"Shaders/SimpleTriangle.hlsl",
			nullptr,
			nullptr,
			"vert",
			"vs_5_0",
			compileFlags,
			0,
			&m_VertexShader,
			nullptr
		));

		ThrowIfFailed(D3DCompileFromFile(
			L"Shaders/SimpleTriangle.hlsl",
			nullptr,
			nullptr,
			"frag",
			"ps_5_0",
			compileFlags,
			0,
			&m_PixelShader,
			nullptr
		));
	}

	// input elements
	{
		m_InputElements =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
	}
}

// create the pipeline state object, which includes compling and loading shaders
void MyBaseApp::InitPipelineStates()
{

	// describe and create the graphics pipeline state object (PSO)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = {m_InputElements.data(), static_cast<UINT>(m_InputElements.size())};	// { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.pRootSignature = m_RootSignature.Get();	// m_EmptyRootSignature	m_RootSignature

	psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_VertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_PixelShader.Get());

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = m_DeviceResources->GetBackBufferFormat();
	psoDesc.DSVFormat = m_DeviceResources->GetDepthBufferFormat();

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(pDevice->CreateGraphicsPipelineState(
		&psoDesc,
		IID_PPV_ARGS(m_PipelineState.ReleaseAndGetAddressOf())
	));
}

void MyBaseApp::InitGeometryBuffers()
{
	
	InitVertexBuffers();

	InitIndexBuffers();	
}

void MyBaseApp::InitVertexBuffers()
{
	MyVertex vertices[] =
	{
		{XMFLOAT3(-0.6f, -0.6f, 0.0f), XMFLOAT2(0.0f, 1.0f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)},
		{XMFLOAT3( 0.0f,  0.6f, 0.0f), XMFLOAT2(0.5f, 0.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f)},
		{XMFLOAT3( 0.6f, -0.6f, 0.0f), XMFLOAT2(1.0f, 1.0f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f)}
	};

	m_VertexCount = _countof(vertices);
	m_VertexOffset = 0;
	UINT bufferSize = sizeof(vertices);

	// the ID3D12Device::CreateCommittedResource method is used to create a resource and an implicit heap
	// that is large enough to store the resource. The resource is also mapped to the implicit heap.
	// 1. 
	// the default resource is created but the buffer data is not yet uploaded to that resource
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_VertexBuffer.ReleaseAndGetAddressOf())
	));

	// 2.
	// create a resorce that is used to transfer the CPU buffer data into GPU memory. To perform the memory transfer,
	// an intermediate buffer resource is created using an upload heap.
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_UploadVB.ReleaseAndGetAddressOf())
	));
	// D3DBook <<d3dUtil.cpp>>
	// Note: upload buffer has to be kept alive after the above function calls because
	// the command list has not been executed yet that performs the actual copy.
	// The caller can release the upload buffer after it knows the copy has been executed

	//
	D3D12_SUBRESOURCE_DATA subresourceData = { };
	subresourceData.pData = vertices;
	subresourceData.RowPitch = bufferSize;
	subresourceData.SlicePitch = subresourceData.RowPitch;

	UpdateSubresources(
		pCmdList,
		m_VertexBuffer.Get(),
		m_UploadVB.Get(),
		0,
		0,
		1,
		&subresourceData
	);
	// for buffer resources, the FirstSubresource is always 0 and the NumSubresources is always 1(or 0, but then
	// this function does nothing) because buffers only have a single subresource at index 0.

	pCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_VertexBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
	));

	vertexBufferView = { };
	vertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = sizeof(vertices);
	vertexBufferView.StrideInBytes = sizeof(MyVertex);
}

void MyBaseApp::InitIndexBuffers()
{
	int indices[] =
	{
		0, 1, 2
	};
	m_IndexCount = _countof(indices);
	m_IndexOffset = 0;

	UINT bufferSize = sizeof(indices);

	// the ID3D12Device::CreateCommittedResource method is used to create a resource and an implicit heap
	// that is large enough to store the resource. The resource is also mapped to the implicit heap.
	// 1. 
	// the default resource is created but the buffer data is not yet uploaded to that resource
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_IndexBuffer.ReleaseAndGetAddressOf())
	));

	// 2.
	// create a resorce that is used to transfer the CPU buffer data into GPU memory. To perform the memory transfer,
	// an intermediate buffer resource is created using an upload heap.
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_UploadIB.ReleaseAndGetAddressOf())
	));

	//
	D3D12_SUBRESOURCE_DATA subresourceData = { };
	subresourceData.pData = indices;
	subresourceData.RowPitch = bufferSize;
	subresourceData.SlicePitch = subresourceData.RowPitch;

	UpdateSubresources(
		pCmdList,
		m_IndexBuffer.Get(),
		m_UploadIB.Get(),
		0,
		0,
		1,
		&subresourceData
	);
	// for buffer resources, the FirstSubresource is always 0 and the NumSubresources is always 1(or 0, but then
	// this function does nothing) because buffers only have a single subresource at index 0.

	pCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_IndexBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_INDEX_BUFFER
	));

	indexBufferView = { };
	indexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = sizeof(indices);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

void MyBaseApp::InitConstantBuffers()
{
	// describe and create a constant buffer view (CBV) descriptor heap.
	// flags indicate that this descriptor heap can be bound to the pipeline
	// and that descriptors contained in it can be referenced by a root table
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(pDevice->CreateDescriptorHeap(
		&cbvHeapDesc,
		IID_PPV_ARGS(m_CBVDescHeap.ReleaseAndGetAddressOf())
	));

	// create the constant buffer
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ConstantBuffer.ReleaseAndGetAddressOf())
	));

	// describe and create a constant buffer view
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = { };
	cbvDesc.BufferLocation = m_ConstantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(CBPerFrame) + 255) & ~255;	// CB size of required to be 256-byte aligned
	pDevice->CreateConstantBufferView(&cbvDesc, m_CBVDescHeap->GetCPUDescriptorHandleForHeapStart());

	CBPerFrame cbPerFrame = {};
	cbPerFrame.baseColor = m_BaseColor;

	// map and initialize the constant buffer. we don't unmap this until the app closes.
	// keeping things mapped for the lifetime of the resource is okay
	CD3DX12_RANGE readRange(0, 0);	// we do not intend to read from this resource on the CPU
	ThrowIfFailed(m_ConstantBuffer->Map(0, &readRange, (void**)(&m_pCBVDataBegin)));
	memcpy(m_pCBVDataBegin, &cbPerFrame, sizeof(cbPerFrame));
}

void MyBaseApp::InitTextures()
{
	// describe and create a shader resource view (SRV) heap for the texture
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(pDevice->CreateDescriptorHeap(
		&srvHeapDesc,
		IID_PPV_ARGS(m_SRVDescHeap.ReleaseAndGetAddressOf())
	));
	// m_SRVDescHeap->SetName(L"SRVDescriptorHeap");

	// DirectX-Graphics-Samples/.../D3D12HelloTexture.cpp
	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	ComPtr<ID3D12Resource> textureUploadHeap;

	// create the texture
	{
		// describe and create a Texture2D
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Width = m_TexWidth;
		textureDesc.Height = m_TexHeight;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.MipLevels = 1;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		ThrowIfFailed(pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(m_Tex.ReleaseAndGetAddressOf())
		));
		m_Tex->SetName(L"Texture");

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_Tex.Get(), 0, 1);

		// create the GPU upload buffer
		ThrowIfFailed(pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)
		));

		// copy data to the intermediate upload heap and then schedule a copy
		// from the upload heap to the Texture2D
		std::vector<UINT8> texture = GenerateTextureData();

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = &texture[0];
		subresourceData.RowPitch = m_TexWidth * m_TexturePixelSize;
		subresourceData.SlicePitch = subresourceData.RowPitch * m_TexHeight;

		UpdateSubresources(
			pCmdList,
			m_Tex.Get(),
			textureUploadHeap.Get(),
			0,
			0,
			1,
			&subresourceData
		);
		pCmdList->ResourceBarrier(
			1, 
			&CD3DX12_RESOURCE_BARRIER::Transition(m_Tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		);

		// describe and create a SRV for the texture
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		pDevice->CreateShaderResourceView(
			m_Tex.Get(),
			&srvDesc,
			m_SRVDescHeap->GetCPUDescriptorHandleForHeapStart()
		);

		m_UploadTex = textureUploadHeap;
	}
}

void MyBaseApp::CustomInit()
{
	// TO DO
}

// Generate a simple black and white checkerboard texture.
std::vector<UINT8> MyBaseApp::GenerateTextureData(bool bRandColor)
{
	const UINT rowPitch = m_TexWidth * m_TexHeight;
	const UINT cellPitch = rowPitch >> 3;		// The width of a cell in the checkboard texture.
	const UINT cellHeight = m_TexWidth >> 3;	// The height of a cell in the checkerboard texture.
	const UINT textureSize = rowPitch * m_TexHeight;

	std::vector<UINT8> data(textureSize);
	UINT8* pData = &data[0];
	UINT8 r = 0xff;
	UINT8 g = 0xff;
	UINT8 b = 0xff;

	if (bRandColor)
	{
		r = rand() & 0xff;
		g = rand() & 0xff;
		b = rand() & 0xff;
	}

	for (UINT n = 0; n < textureSize; n += m_TexturePixelSize)
	{
		UINT x = n % rowPitch;
		UINT y = n / rowPitch;
		UINT i = x / cellPitch;
		UINT j = y / cellHeight;

		if (i % 2 == j % 2)
		{
			pData[n] = 0x00;        // R
			pData[n + 1] = 0x00;    // G
			pData[n + 2] = 0x00;    // B
			pData[n + 3] = 0xff;    // A
		}
		else
		{
			pData[n] = r;			// R
			pData[n + 1] = g;		// G
			pData[n + 2] = b;		// B
			pData[n + 3] = 0xff;    // A
		}
	}

	return data;
}

void MyBaseApp::DisposeUploaders()
{
	m_UploadVB = nullptr;
	m_UploadIB = nullptr;

	m_UploadTex = nullptr;
}

void MyBaseApp::Test()
{
	pDevice;
	
}

/**
	DirectX-Graphics-Samples/Samples/.../D3d12HelloTriangle.cpp
	Upload Heap
	Note: using upload heaps to transfer static data like vertex buffers is not
	recommended. Every time the GPU needs it, the upload heap will be marshalled over.
	Use Default Heap.
	ThrowIfFailed(pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ConstantBuffer.ReleaseAndGetAddressOf())
	));

	// copy the triangle data to the constant buffer
	UINT8* pConstantDataBegin;
	CD3DX12_RANGE readRange(0, 0);	// we do not intend to read from this resource on the CPU
	ThrowIfFailed(m_ConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pConstantDataBegin)));
	memcpy(pConstantDataBegin, constantData, sizeof(constantData));
	m_ConstantBuffer->Unmap(0, nullptr);
	
*/

/**
	https://www.braynzarsoft.net/viewtutorial/q16390-directx-12-constant-buffers-root-descriptor-tables
	Adding Descriptor Table Parameter to Root Signature

	## create a descriptor table, which will describe a range of descriptors inside the constant descriptor heap
	D3D12_DESCRIPTOR_RANGE
	typedef struct D3D12_DESCRIPTOR_RANGE {
	  D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
	  UINT                        NumDescriptors;
	  UINT                        BaseShaderRegister;
	  UINT                        RegisterSpace;
	  UINT                        OffsetInDescriptorsFromTableStart;
	} D3D12_DESCRIPTOR_RANGE;
	>> RangeType: this describes whether this is a range of srv's, uav's cbv's or samplers.

	## root parameter
	typedef struct D3D12_ROOT_PARAMETER {
	  D3D12_ROOT_PARAMETER_TYPE ParameterType;
	  union {
		D3D12_ROOT_DESCRIPTOR_TABLE DescriptorTable;
		D3D12_ROOT_CONSTANTS        Constants;
		D3D12_ROOT_DESCRIPTOR       Descriptor;
	  };
	  D3D12_SHADER_VISIBILITY   ShaderVisibility;
	} D3D12_ROOT_PARAMETER;
	... 

	Create the Constant Buffer Resource Heap
	create an upload heap to hold the constant buffer. Since we will be updating this constant buffer frequently (at least
once every frame), there is no reason to create a default heap to copy the upload heap to.
	we create a buffer of the size 64KB. This has to do with alignment requirements. Resource Heaps must be a multiple of
64KB. So even though our constant buffer is only 16 bytes(4 floats), we must allocate at least 64KB. If our constant buffer was
65KB, we would need to allocate 128KB.
	Single-texture and buffer resources must be 64KB aligned. Multi-sampled texture resources must be 4MB aligned.

	Creating the Constant Buffer View
	We can get a pointer to the GPU memory by calling the GetGPUVirtualAddress method of our constant buffer resource heap.
	Constant buffers must be 256 byte aligned, which is different from the alignment requirement for a resource heap.

	Mapping the Constant Buffer
	first we create a range. This range is the area of memory within the constant buffer the cpu can access. We can set the 
begin to be equal to or larger than end which means the CPU will not read from the constant buffer.
	next we map our constant buffer resource. We will get a pointer to a chunk of memory that the GPU will access and upload
when a command list uses it.
	it is ok to keep a resource mapped for as long as you need it. We just need to make sure we do not access the mapped 
area after we have released the resource.
	once we our resource mapped, we can copy data to it using memcpy.
	so that when we execute our command list, it will upload that chunk of data to the register b0 and our vertex shader 
will have access to the new data.
*/
