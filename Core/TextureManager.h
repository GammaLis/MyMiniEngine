#pragma once
#include "pch.h"
#include "GpuResource.h"
#include <mutex>

namespace MyDirectX
{
	enum class EDefaultTexture
	{
		kMagenta2D,
		kBlackOpaque2D,
		kBlackTransparent2D,
		kWhiteOpaque2D,
		kWhiteTransparent2D,
		kDefaultNormalMap,
		kBlackCubeMap,

		kNumDefaultTextures
	};

	class Texture : public GpuResource
	{
	public:
		Texture()
		{ 
			m_hCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}
		Texture(D3D12_CPU_DESCRIPTOR_HANDLE handle) : m_hCpuDescriptorHandle(handle) {  }

		// create a 1-level 2D texture
		void Create2D(ID3D12Device *pDevice, size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData);
		void Create2D(ID3D12Device *pDevice, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData);
		void CreateCube(ID3D12Device *pDevice, size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void *pInitData);

		void CreateTGAFromMemory(ID3D12Device *pDevice, const void* memBuffer, size_t fileSize, bool sRGB);
		bool CreateDDSFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB);
		void CreatePIXImageFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize);
		void CreateTexBySTB_IMAGE(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB);

		virtual void Destroy() override
		{
			GpuResource::Destroy();
			// this leaks descriptor handles. We should really give it back to be reused
			m_hCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_hCpuDescriptorHandle; }

		bool operator! () { return m_hCpuDescriptorHandle.ptr == 0; }

		uint32_t GetWidth()  const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetDepth()  const { return m_Depth; }

	protected:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE m_hCpuDescriptorHandle;
	};

	class ManagedTexture : public Texture
	{
		friend class TextureRef;
		friend class TextureManager;
	public:
		ManagedTexture(const std::wstring& fileName) : m_MapKey(fileName), m_IsValid(false), m_IsLoading(true), m_ReferenceCount(0) {  }

		void WaitForLoad() const;
		void Unload();

		void SetDefault(EDefaultTexture detaultTex = EDefaultTexture::kMagenta2D);
		void SetToInvalidTexture();
		bool IsValid() const { return m_IsValid; }

	private:
		std::wstring m_MapKey;	// for deleting from the map later
		bool m_IsValid;
		bool m_IsLoading;
		size_t m_ReferenceCount = 0;
	};

	/**
	*	A handle to a ManagedTexture. Constructors and destructors modify the reference count.
	* When the last reference is destroyed, the TextureManager is informed that the texture should be deleted.
	*/ 
	class TextureRef
	{
	public:
		TextureRef(ManagedTexture *tex = nullptr);
		TextureRef(const TextureRef &);
		~TextureRef();

		void operator= (const TextureRef &rhs);
		void operator= (std::nullptr_t);

		// check that this points to a valid texture (which loaded successfully)
		bool IsValid() const { return m_ref != nullptr && m_ref->IsValid(); }

		// gets the SRV descriptor handle. If the reference is invalid, 
		// returns a valid descriptor handle (specified by the fallback)
		D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;

		// get the texture pointer. Client is responsible to not dereference null pointers
		const Texture *Get() const;

		const Texture* operator->() const;

	private:
		ManagedTexture *m_ref = nullptr;
	};

	/**
	*	Texture file loading system
	*	references to textures are passed around so that a texture may be shared. 
	* When all references to a texture expire, the texture memory is reclaimed.
	*/ 
	class TextureManager
	{
	public:
		void Init(const std::wstring& textureLibRoot);
		void InitDefaultTextures(ID3D12Device* pDevice);
		void Shutdown();

		std::pair<ManagedTexture*, bool> FindOrLoadTexture(const std::wstring& fileName, bool forceSRGB = false);
		ManagedTexture* FindOrLoadTextureWithFallback(const std::wstring& fileName, EDefaultTexture fallback = EDefaultTexture::kMagenta2D, bool forceSRGB = false);

		const ManagedTexture* LoadFromFile(ID3D12Device *pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadDDSFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadTGAFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB = false);
		const ManagedTexture* LoadPIXImageFromFile(ID3D12Device* pDevice, const std::wstring& fileName);
		const ManagedTexture* LoadBySTB_IMAGE(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB = false);

		const ManagedTexture* LoadFromFile(ID3D12Device* pDevice, const std::string& fileName, bool sRGB = false)
		{
			return LoadFromFile(pDevice, MakeWStr(fileName), sRGB);
		}

		const ManagedTexture* LoadBySTB_IMAGE(ID3D12Device* pDevice, const std::string& fileName, bool sRGB = false)
		{
			return LoadBySTB_IMAGE(pDevice, MakeWStr(fileName), sRGB);
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

		/// Default Textures
		static Texture s_DefaultTexture[(int)EDefaultTexture::kNumDefaultTextures];
		static D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultTexture(EDefaultTexture texID);
		static void DestroyDefaultTextures();

	private:
		std::wstring m_RootPath;
		std::map<std::wstring, std::unique_ptr<ManagedTexture>> m_TextureCache;
		std::mutex m_TexMutex;
	};
}
