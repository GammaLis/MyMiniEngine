#include "TextureManager.h"
#include "DDSTextureLoader.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "../FileUtility.h"
#include <thread>

namespace MyDirectX
{

	static UINT BytesPerPixel(DXGI_FORMAT format)
	{
		return (UINT)BitsPerPixel(format) / 8;
	}

	void Texture::Create(ID3D12Device* pDevice, size_t pitch, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData)
	{
		m_UsageState = D3D12_RESOURCE_STATE_COPY_DEST;

		D3D12_HEAP_PROPERTIES heapProps;
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width = width;
		texDesc.Height = (UINT)height;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			m_UsageState, nullptr, IID_PPV_ARGS(&m_pResource));
		m_pResource->SetName(L"Texture");

		D3D12_SUBRESOURCE_DATA texResource;
		texResource.pData = pInitData;
		texResource.RowPitch = pitch * BytesPerPixel(format);
		texResource.SlicePitch = texResource.RowPitch * height;

		CommandContext::InitializeTexture(*this, 1, &texResource);

		if (m_hCpuDescriptorHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_hCpuDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		//-mf
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//srvDesc.Texture2D.MipLevels = 1;
		//srvDesc.Texture2D.MostDetailedMip = 0;
		//srvDesc.Format = format;

		// MSDN CreateShaderResourceView
		// at least one of pResource or pDesc must be provided. 
		// 1. A null pResource is used to initialize a null descriptor
		// which guarantees D3D11-like null binding behavior (reading 0s, writes are discarded),but must have a valid 
		// pDesc in order to determine the descriptor type.
		// 2. A null pDesc is used to initialize a default descriptor, if possible. This behavior is identical to the D3D11
		// null descriptor behavior, where defaults are filled in. This behavior inherits the resource format and dimension
		// (if not typeless) and for buffers SRVs target a full buffer and are typed (not raw or structured), and for textures 
		// SRVs target a full texture, all mips and all array slices. Not all resources support null descriptor initialization.
		pDevice->CreateShaderResourceView(m_pResource.Get(), nullptr, m_hCpuDescriptorHandle);
	}

	void Texture::CreateTGAFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB)
	{
		const uint8_t* filePtr = (const uint8_t*)memBuffer;

		// skip first 2 bytes
		filePtr += 2;

		/*uint8_t imageTypeCode = */ *filePtr++;

		// ignore another 9 bytes
		filePtr += 9;

		uint16_t imageWidth = *(uint16_t*)filePtr;
		filePtr += sizeof(uint16_t);
		uint16_t imageHeight = *(uint16_t*)filePtr;
		filePtr += sizeof(uint16_t);
		uint8_t bitCount = *filePtr++;

		// ignore another type
		filePtr++;

		uint32_t* formattedData = new uint32_t[imageWidth * imageHeight];
		uint32_t* iter = formattedData;

		uint8_t numChannels = bitCount / 8;
		uint32_t numBytes = imageWidth * imageHeight * numChannels;

		switch (numChannels)
		{
		case 3:		
			// 3 通道，存储类似 0x FF R(8) G(8) B(8)
 			for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx += 3)
			{
				// 0x | ff | filePtr[0] | filePtr[1] | filePtr[2]
				*iter++ = 0xff000000 | filePtr[0] << 16 | filePtr[1] << 8 | filePtr[2];
				filePtr += 3;
			}
			break;

		case 4:
			for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx += 4)
			{
				// 0x | filePtr[3] | filePtr[0] | filePtr[1] | filePtr[2]
				*iter++ = filePtr[3] << 24 | filePtr[0] << 16 | filePtr[1] << 8 | filePtr[2];
				filePtr += 4;
			}
			break;

		default:
			break;
		}

		Create(pDevice, imageWidth, imageHeight, sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, formattedData);

		delete[] formattedData;
	}

	bool Texture::CreateDDSFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB)
	{
		if (m_hCpuDescriptorHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_hCpuDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		HRESULT hr = CreateDDSTextureFromMemory(pDevice,
			(const uint8_t*)memBuffer, fileSize, 0, sRGB, &m_pResource, m_hCpuDescriptorHandle);
		
		return SUCCEEDED(hr);
	}

	void Texture::CreatePIXImageFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize)
	{
		struct Header
		{
			DXGI_FORMAT format;
			uint32_t pitch;
			uint32_t width;
			uint32_t height;
		};

		const Header& header = *(Header*)memBuffer;

		ASSERT(fileSize >= header.pitch * BytesPerPixel(header.format) * header.height + sizeof(Header),
			"Raw PIX image dump has an invalid file size");

		Create(pDevice, header.pitch, header.width, header.height, header.format, (uint8_t*)memBuffer + sizeof(Header));
	}

	// ManagedTexture
	void ManagedTexture::WaitForLoad() const
	{
		volatile D3D12_CPU_DESCRIPTOR_HANDLE& volHandle = (volatile D3D12_CPU_DESCRIPTOR_HANDLE&)m_hCpuDescriptorHandle;
		volatile bool& volValid = (volatile bool&)m_IsValid;
		while (volHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN && volValid)
			std::this_thread::yield();
	}

	void ManagedTexture::Unload()
	{
		Graphics::s_TextureManager.ReleaseTextures(1, &m_MapKey);
	}

	// 默认 不可用纹理
	void ManagedTexture::SetToInvalidTexture()
	{
		m_hCpuDescriptorHandle = TextureManager::GetMagentaTex2D().GetSRV();
		m_IsValid = false;
	}

	// TextureManager
	void TextureManager::Init(const std::wstring& textureLibRoot)
	{
		m_RootPath = textureLibRoot;
	}

	void TextureManager::Shutdown()
	{
		m_TextureCache.clear();
	}

	std::pair<ManagedTexture*, bool> TextureManager::FindOrLoadTexture(const std::wstring& fileName)
	{
		std::lock_guard<std::mutex> lockGuard(m_TexMutex);

		auto iter = m_TextureCache.find(fileName);

		// if it's found, it has already been loaded or the load process has begun
		if (iter != m_TextureCache.end())
		{
			return std::make_pair(iter->second.get(), false);
		}

		ManagedTexture* newTexture = new ManagedTexture(fileName);
		m_TextureCache[fileName].reset(newTexture);

		// this was the first time it was request, so indicate that the caller must read the file
		return std::make_pair(newTexture, true);
	}

	const ManagedTexture* TextureManager::LoadFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB)
	{
		std::wstring catPath = fileName;

		const ManagedTexture* tex = LoadDDSFromFile(pDevice, catPath + L".dds", sRGB);
		if (!tex->IsValid())
			tex = LoadTGAFromFile(pDevice, catPath + L".tga", sRGB);

		return tex;
	}

	const ManagedTexture* TextureManager::LoadDDSFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB)
	{
		auto managedTex = FindOrLoadTexture(fileName);

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return tex;
		}

		Utility::ByteArray ba = Utility::ReadFileSync(m_RootPath + fileName);
		if (ba->size() == 0 || !tex->CreateDDSFromMemory(pDevice, ba->data(), ba->size(), sRGB))
			tex->SetToInvalidTexture();
		else
			tex->GetResource()->SetName(fileName.c_str());

		return tex;
	}

	const ManagedTexture* TextureManager::LoadTGAFromFile(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB)
	{
		auto managedTex = FindOrLoadTexture(fileName);

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return tex;
		}

		Utility::ByteArray ba = Utility::ReadFileSync(m_RootPath + fileName);
		if (ba->size() > 0)
		{
			tex->CreateTGAFromMemory(pDevice, ba->data(), ba->size(), sRGB);
			tex->GetResource()->SetName(fileName.c_str());
		}
		else
			tex->SetToInvalidTexture();

		return tex;
	}

	const ManagedTexture* TextureManager::LoadPIXImageFromFile(ID3D12Device* pDevice, const std::wstring& fileName)
	{
		auto managedTex = FindOrLoadTexture(fileName);

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return tex;
		}

		Utility::ByteArray ba = Utility::ReadFileSync(m_RootPath + fileName);
		if (ba->size() > 0)
		{
			tex->CreatePIXImageFromMemory(pDevice, ba->data(), ba->size());
			tex->GetResource()->SetName(fileName.c_str());
		}
		else
			tex->SetToInvalidTexture();

		return nullptr;
	}

	// -mf
	void TextureManager::ReleaseTextures(size_t numTex, const std::wstring fileName[])
	{
		std::lock_guard<std::mutex> lockGuard(m_TexMutex);

		ASSERT(numTex > 0 && fileName != nullptr);

		for (size_t i = 0; i < numTex; ++i)
		{
			auto iter = m_TextureCache.find(fileName[i]);

			if (iter != m_TextureCache.end())
			{
				auto &pTex = iter->second;
				pTex->Destroy();
				pTex.release();

				m_TextureCache.erase(fileName[i]);
			}
		}		
	}

	// static members 
	const Texture& TextureManager::GetBlackTex2D()
	{
		auto managedTex = Graphics::s_TextureManager.FindOrLoadTexture(L"DefaultBlackTexture");

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return *tex;
		}

		uint32_t blackPixel = 0;
		tex->Create(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blackPixel);
		return *tex;
	}

	const Texture& TextureManager::GetWhiteTex2D()
	{
		auto managedTex = Graphics::s_TextureManager.FindOrLoadTexture(L"DefaultWhiteTexture");

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return *tex;
		}

		uint32_t whitePixel = 0xFFFFFFFFul;
		tex->Create(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &whitePixel);
		return *tex;
	}

	const Texture& TextureManager::GetMagentaTex2D()
	{
		auto managedTex = Graphics::s_TextureManager.FindOrLoadTexture(L"DefaultMagentaTexture");

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return *tex;
		}

		uint32_t magentaPixel = 0x00FF00FF;
		tex->Create(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &magentaPixel);
		return *tex;
	}

	

}

