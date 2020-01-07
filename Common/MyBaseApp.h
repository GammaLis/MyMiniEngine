#pragma once

#include "MyApp.h"

namespace MyDirectX
{
	class MyBaseApp : public MyApp
	{
	public:
		MyBaseApp(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update() override;

		virtual void Render() override; 

	protected:
		virtual void InitAssets() override;

		// 其实放到helper类里更好
		std::vector<UINT8> GenerateTextureData(bool bRandColor = false);

		virtual void DisposeUploaders();

	private:
		// 这些方法 可供子类直接使用
		virtual void InitRootSignatures();

		virtual void InitShadersAndInputElements();

		virtual void InitPipelineStates();

		virtual void InitGeometryBuffers();
		virtual void InitVertexBuffers();
		virtual void InitIndexBuffers();

		virtual void InitConstantBuffers();

		virtual void InitTextures();

		// 自定义初始化（其它效果可能需要初始化，留个接口）
		virtual void CustomInit();

	protected:
		// 没有实际意义，只是声明一下，方便写些测试函数
		void Test();

		// pipeline objects
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_EmptyRootSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

		// resources
		Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadVB;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
		Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadIB;
		D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

		// CBV_SRV_UAV
		UINT m_CSUV_DescriptorSize = 0;
		// cbv
		Microsoft::WRL::ComPtr<ID3D12Resource> m_ConstantBuffer;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CBVDescHeap;
		DirectX::XMFLOAT4 m_BaseColor = DirectX::XMFLOAT4(0.2f, 0.0f, 0.0f, 1.0f);
		UINT8* m_pCBVDataBegin = nullptr;

		// srv
		Microsoft::WRL::ComPtr<ID3D12Resource> m_Tex;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadTex;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SRVDescHeap;
		UINT m_TexWidth = 256;
		UINT m_TexHeight = 256;
		UINT m_TexturePixelSize = 4;

		// geometry
		UINT m_VertexCount = 0;
		UINT m_VertexOffset = 0;
		UINT m_IndexCount = 0;
		UINT m_IndexOffset = 0;

		DirectX::XMFLOAT4X4 m_WorldMat;
		DirectX::XMFLOAT4X4 m_ViewMat;
		DirectX::XMFLOAT4X4 m_ProjMat;

		// shaders
		Microsoft::WRL::ComPtr<ID3DBlob> m_VertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> m_HullShader;
		Microsoft::WRL::ComPtr<ID3DBlob> m_DomainShader;
		Microsoft::WRL::ComPtr<ID3DBlob> m_GeometryShader;
		Microsoft::WRL::ComPtr<ID3DBlob> m_PixelShader;

		std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
	};
}

