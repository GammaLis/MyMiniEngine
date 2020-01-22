#pragma once
#include "pch.h"
#include "GpuResource.h"
#include <mutex>

namespace MyDirectX
{
	class Texture : public GpuResource
	{
	public:
		Texture()
		{ 
			m_hCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}
		Texture(D3D12_CPU_DESCRIPTOR_HANDLE handle) : m_hCpuDescriptorHandle(handle) {  }

		// create a 1-level 2D texture
		void Create(ID3D12Device *pDevice, size_t pitch, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData);
		void Create(ID3D12Device* pDevice, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData)
		{
			Create(pDevice, width, width, height, format, pInitData);
		}

		void CreateTGAFromMemory(ID3D12Device *pDevice, const void* memBuffer, size_t fileSize, bool sRGB);
		bool CreateDDSFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB);
		void CreatePIXImageFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize);

		virtual void Destroy() override
		{
			GpuResource::Destroy();
			// this leaks descriptor handles. We should really give it back to be reused
			m_hCpuDescriptorHandle.ptr = 0;
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_hCpuDescriptorHandle; }

		bool operator! () { return m_hCpuDescriptorHandle.ptr == 0; }

	protected:
		D3D12_CPU_DESCRIPTOR_HANDLE m_hCpuDescriptorHandle;
	};

	class ManagedTexture : public Texture
	{
	public:
		ManagedTexture(const std::wstring& fileName) : m_MapKey(fileName), m_IsValid(true) {  }

		void WaitForLoad() const;

		// -mf
		void Unload();

		void SetToInvalidTexture();
		bool IsValid() const { return m_IsValid; }

	private:
		std::wstring m_MapKey;	// for deleting from the map later
		bool m_IsValid;
	};

	class TextureManager
	{
	public:
		void Init(const std::wstring& textureLibRoot);
		void Shutdown();

		std::pair<ManagedTexture*, bool> FindOrLoadTexture(const std::wstring& fileName);

		const ManagedTexture* LoadFromFile(ID3D12Device *pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadDDSFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadTGAFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadPIXImageFromFile(ID3D12Device* pDevice, const std::wstring& fileName);

		const ManagedTexture* LoadFromFile(ID3D12Device* pDevice, const std::string& fileName, bool sRGB = false)
		{
			return LoadFromFile(pDevice, MakeWStr(fileName), sRGB);
		}

		const ManagedTexture* LoadDDSFromFile(ID3D12Device* pDevice, const std::string& fileName, bool sRGB = false)
		{
			return LoadDDSFromFile(pDevice, MakeWStr(fileName), sRGB);
		}

		const ManagedTexture* LoadTGAFromFile(ID3D12Device* pDevice, const std::string& fileName, bool sRGB = false)
		{
			return LoadTGAFromFile(pDevice, MakeWStr(fileName), sRGB);
		}

		const ManagedTexture* LoadPIXImageFromFile(ID3D12Device* pDevice, const std::string& fileName)
		{
			return LoadPIXImageFromFile(pDevice, MakeWStr(fileName));
		}

		// 
		void ReleaseTextures(size_t numTex, const std::wstring fileName[]);

		// static members
		static const Texture& GetBlackTex2D();
		static const Texture& GetWhiteTex2D();
		static const Texture& GetMagentaTex2D();

	private:
		std::wstring m_RootPath;
		std::map<std::wstring, std::unique_ptr<ManagedTexture>> m_TextureCache;
		std::mutex m_TexMutex;
	};
}
