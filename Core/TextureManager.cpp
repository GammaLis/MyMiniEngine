#include "TextureManager.h"
#include "DDSTextureLoader.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "FileUtility.h"
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace MyDirectX
{

	Texture TextureManager::s_DefaultTexture[(int)EDefaultTexture::kNumDefaultTextures];

	static UINT BytesPerPixel(DXGI_FORMAT format)
	{
		return (UINT)BitsPerPixel(format) / 8;
	}

	/// Texture
	void Texture::Create2D(ID3D12Device* pDevice, size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData)
	{
		Destroy();

		m_UsageState = D3D12_RESOURCE_STATE_COPY_DEST;

		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;
		m_Depth = 1;

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

		ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			m_UsageState, nullptr, IID_PPV_ARGS(m_pResource.ReleaseAndGetAddressOf())));
		m_pResource->SetName(L"Texture");

		D3D12_SUBRESOURCE_DATA texResource;
		texResource.pData = pInitData;
		texResource.RowPitch = rowPitchBytes /* width * BytesPerPixel(format)*/;
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

	void Texture::Create2D(ID3D12Device* pDevice, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData)
	{
		Create2D(pDevice, width * BytesPerPixel(format), width, height, format, pInitData);
	}

	void Texture::CreateCube(ID3D12Device* pDevice, size_t rowPitchBytes, size_t width, size_t height, DXGI_FORMAT format, const void* pInitData)
	{
		Destroy();

		m_UsageState = D3D12_RESOURCE_STATE_COPY_DEST;

		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;
		m_Depth = 6;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width = width;
		texDesc.Height = (UINT)height;
		texDesc.DepthOrArraySize = 6;
		texDesc.MipLevels = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, 
			m_UsageState, nullptr, IID_PPV_ARGS(m_pResource.ReleaseAndGetAddressOf())));
		m_pResource->SetName(L"Texture");

		D3D12_SUBRESOURCE_DATA texResource;
		texResource.pData = pInitData;
		texResource.RowPitch = rowPitchBytes;
		texResource.SlicePitch = texResource.RowPitch * height;

		CommandContext::InitializeTexture(*this, 1, &texResource);

		if (m_hCpuDescriptorHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_hCpuDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MipLevels = 1;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
		pDevice->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_hCpuDescriptorHandle);

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

		Create2D(pDevice, imageWidth, imageHeight, sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, formattedData);

		delete[] formattedData;
	}

	bool Texture::CreateDDSFromMemory(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB)
	{
		if (m_hCpuDescriptorHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_hCpuDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		bool valid = SUCCEEDED(CreateDDSTextureFromMemory(pDevice,
			(const uint8_t*)memBuffer, fileSize, 0, sRGB, &m_pResource, m_hCpuDescriptorHandle));

		if (valid)
		{
			D3D12_RESOURCE_DESC desc = GetResource()->GetDesc();
			m_Width = (uint32_t)desc.Width;
			m_Height = desc.Height;
			m_Depth = desc.DepthOrArraySize;
		}
		
		return valid;
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
		// 这里的pitch似乎不是rowPitchBytes？上面是pitch * BytesPerPixel	??? -2021-3-8	MS-Graphic Samples-Texture.cpp
		Create2D(pDevice, header.pitch * BytesPerPixel(header.format), header.width, header.height, header.format, (uint8_t*)memBuffer + sizeof(Header));
	}

	void Texture::CreateTexBySTB_IMAGE(ID3D12Device* pDevice, const void* memBuffer, size_t fileSize, bool sRGB)
	{
		int width, height, nChannels;
		stbi_uc* data = stbi_load_from_memory((const stbi_uc*)memBuffer, (int)fileSize, &width, &height, &nChannels, 0);
		if (data)
		{
			DXGI_FORMAT format;
			switch (nChannels)
			{
			case 1:
				format = DXGI_FORMAT_R8_UNORM;
				break;
			case 2:
				format = DXGI_FORMAT_R8G8_UNORM;
				break;
			case 4:
			default:
				format = sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
				break;
			}
			Create2D(pDevice, width, height, format, data);
		}
		stbi_image_free(data);
	}

	/// ManagedTexture
	void ManagedTexture::WaitForLoad() const
	{
		volatile D3D12_CPU_DESCRIPTOR_HANDLE& volHandle = (volatile D3D12_CPU_DESCRIPTOR_HANDLE&)m_hCpuDescriptorHandle;
		volatile bool& volValid = (volatile bool&)m_IsValid;
		// 等待加载- 1.加载成功 volHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN
		// 2.加载失败 volHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN && volValid == false
		// 这2个初始值 改变一个，即可返回，结束等待	
		// 注：之前纠结 为什么是 volValid = true时等待，如果 volValid = false时等待，如果加载失败，
		// volHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN && volValid == false 无法结束等待	-20-2-26
		// while (volHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN && volValid) std::this_thread::yield();

		// 更新 -2021-3-8
		while ((volatile bool&)m_IsLoading) std::this_thread::yield();
	}

	void ManagedTexture::Unload()
	{
		Graphics::s_TextureManager.ReleaseTextures(1, &m_MapKey);
	}

	void ManagedTexture::SetDefault(EDefaultTexture detaultTex)
	{
		m_hCpuDescriptorHandle = TextureManager::GetDefaultTexture(detaultTex);
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

	void TextureManager::InitDefaultTextures(ID3D12Device* pDevice)
	{
		uint32_t MagentPixel = 0xFFFF00FF;
		s_DefaultTexture[(int)EDefaultTexture::kMagenta2D].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &MagentPixel);
		uint32_t BlackOpaqueTexel = 0xFF000000;
		s_DefaultTexture[(int)EDefaultTexture::kBlackOpaque2D].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &BlackOpaqueTexel);
		uint32_t BlackTransparentTexel = 0x00000000;
		s_DefaultTexture[(int)EDefaultTexture::kBlackTransparent2D].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &BlackTransparentTexel);
		uint32_t WhiteOpaqueTexel = 0xFFFFFFFF;
		s_DefaultTexture[(int)EDefaultTexture::kWhiteOpaque2D].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &WhiteOpaqueTexel);
		uint32_t WhiteTransparentTexel = 0x00FFFFFF;
		s_DefaultTexture[(int)EDefaultTexture::kWhiteTransparent2D].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &WhiteTransparentTexel);
		uint32_t FlatNormalTexel = 0x00FF8080;
		s_DefaultTexture[(int)EDefaultTexture::kDefaultNormalMap].Create2D(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &FlatNormalTexel);
		uint32_t BlackCubeTexels[6] = {};
		s_DefaultTexture[(int)EDefaultTexture::kBlackCubeMap].CreateCube(pDevice, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, BlackCubeTexels);
	}

	void TextureManager::Shutdown()
	{
		DestroyDefaultTextures();

		m_TextureCache.clear();
	}

	// <ManagedTexture*, bRequestLoad : bool>
	std::pair<ManagedTexture*, bool> TextureManager::FindOrLoadTexture(const std::wstring& fileName, bool forceSRGB)
	{
		std::lock_guard<std::mutex> lockGuard(m_TexMutex);

		std::wstring key = fileName;
		if (forceSRGB)
			key += L"_SRGB";

		// searching for an existing managed texture
		auto iter = m_TextureCache.find(key);

		// if it's found, it has already been loaded or the load process has begun
		if (iter != m_TextureCache.end())
		{
			return std::make_pair(iter->second.get(), false);
		}

		ManagedTexture* newTexture = new ManagedTexture(key);
		m_TextureCache[key].reset(newTexture);

		// this was the first time it was request, so indicate that the caller must read the file
		return std::make_pair(newTexture, true);
	}

	// fileName需含扩展名
	ManagedTexture* TextureManager::FindOrLoadTextureWithFallback(const std::wstring& fileName, EDefaultTexture fallback, bool forceSRGB)
	{
		ManagedTexture* tex = nullptr;

		{
			std::lock_guard<std::mutex> Guard(m_TexMutex);

			std::wstring key = fileName;
			if (forceSRGB)
				key += L"_SRGB";

			// Search for an existing managed texture
			auto& iter = m_TextureCache.find(key);
			if (iter != m_TextureCache.end())
			{
				// If a texture was already created make sure it has finished loading before
				// returning a point to it
				tex = iter->second.get();
				tex->WaitForLoad();
				return tex;
			}
			else
			{
				// If it's not found, create a new managed texture and start loading it
				tex = new ManagedTexture(key);
				m_TextureCache[key].reset(tex);
			}
		}

		Utility::ByteArray ba = Utility::ReadFileSync(m_RootPath + fileName);
		if (ba->size() > 0)
		{
			tex->CreateTexBySTB_IMAGE(Graphics::s_Device, ba->data(), ba->size(), forceSRGB);
			tex->GetResource()->SetName(fileName.c_str());
			tex->m_IsValid = true;
		}
		else
		{
			tex->SetDefault(fallback);
		}
		tex->m_IsLoading = false;

		return nullptr;
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
		{
			tex->GetResource()->SetName(fileName.c_str());
			tex->m_IsValid = true;
		}
		tex->m_IsLoading = false;

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
			tex->m_IsValid = true;
		}
		else
			tex->SetToInvalidTexture();
		tex->m_IsLoading = false;

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
			tex->m_IsValid = true;
		}
		else
			tex->SetToInvalidTexture();
		tex->m_IsLoading = false;

		return tex;
	}

	const ManagedTexture* TextureManager::LoadBySTB_IMAGE(ID3D12Device* pDevice, const std::wstring& fileName, bool sRGB)
	{
		auto managedTex = FindOrLoadTexture(fileName);

		ManagedTexture* tex = managedTex.first;
		const bool requestsLoad = managedTex.second;

		if (!requestsLoad)
		{
			tex->WaitForLoad();
			return tex;
		}

		// 直接使用stbi_load
		/**
		std::wstring path = (m_RootPath + fileName);
		size_t len = path.size() + 1;
		char* cpath = (char*)_malloca(len);
		size_t r;
		wcstombs_s(&r, cpath, len, path.c_str(), len);
		int x, y, n;
		auto data = stbi_load(cpath, &x, &y, &n, 0);
		if (data)
		{
			DXGI_FORMAT format;
			switch (n)
			{
			case 1:
				format = DXGI_FORMAT_R8_UNORM;
				break;
			case 2:
				format = DXGI_FORMAT_R8G8_UNORM;
				break;
			case 4:
			default:
				format = sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
				break;
			}
			tex->Create(pDevice, x, y, format, data);
			tex->GetResource()->SetName(fileName.c_str());
		}
		else
			tex->SetToInvalidTexture();
		stbi_image_free(data);
		*/

		Utility::ByteArray ba = Utility::ReadFileSync(m_RootPath + fileName);
		if (ba->size() > 0)
		{
			tex->CreateTexBySTB_IMAGE(pDevice, ba->data(), ba->size(), sRGB);
			tex->GetResource()->SetName(fileName.c_str());
			tex->m_IsValid = true;
		}
		else
			tex->SetToInvalidTexture();
		tex->m_IsLoading = false;

		return tex;
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

#pragma region Default Textures
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
		tex->Create2D(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blackPixel);
		tex->m_IsLoading = false;

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
		tex->Create2D(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &whitePixel);
		tex->m_IsLoading = false;

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
		tex->Create2D(Graphics::s_Device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &magentaPixel);
		tex->m_IsLoading = false;

		return *tex;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::GetDefaultTexture(EDefaultTexture texID)
	{
		ASSERT(texID < EDefaultTexture::kNumDefaultTextures);
		return s_DefaultTexture[(int)texID].GetSRV();
	}

	void TextureManager::DestroyDefaultTextures()
	{
		for (int i = 0; i < (int)EDefaultTexture::kNumDefaultTextures; ++i)
		{
			s_DefaultTexture[i].Destroy();
		}
	}
#pragma endregion

	/// TextureRef
	TextureRef::TextureRef(ManagedTexture* tex) : m_ref{tex}
	{
		if (m_ref != nullptr)
			++m_ref->m_ReferenceCount;
	}

	TextureRef::TextureRef(const TextureRef& ref) : m_ref{ref.m_ref}
	{
		if (m_ref != nullptr)
			++m_ref->m_ReferenceCount;
	}

	TextureRef::~TextureRef()
	{
		if (m_ref != nullptr && --m_ref->m_ReferenceCount == 0)
			m_ref->Unload();
	}

	void TextureRef::operator=(const TextureRef& rhs)
	{
		if (&rhs != this)
		{
			if (m_ref != nullptr && --m_ref->m_ReferenceCount == 0)
				m_ref->Unload();
			m_ref = rhs.m_ref;
			if (m_ref != nullptr)
				++m_ref->m_ReferenceCount;
		}
	}

	void TextureRef::operator=(std::nullptr_t)
	{
		if (m_ref != nullptr && --m_ref->m_ReferenceCount == 0)
			m_ref->Unload();
		m_ref = nullptr;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE TextureRef::GetSRV() const
	{
		if (m_ref != nullptr)
			return m_ref->GetSRV();
		else 
			return TextureManager::GetDefaultTexture(EDefaultTexture::kMagenta2D);
	}

	const Texture* TextureRef::Get() const
	{
		return m_ref;
	}

	const Texture* TextureRef::operator->() const
	{
		ASSERT(m_ref != nullptr);
		return m_ref;
	}

}
