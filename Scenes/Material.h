#pragma once
#include "pch.h"
#include "Scenes/MaterialDefines.h"

namespace MyDirectX
{
	using float2 = DirectX::XMFLOAT2;
	using float3 = DirectX::XMFLOAT3;
	using float4 = DirectX::XMFLOAT4;

	enum class AlphaMode
	{
		UNKNOWN = -1,

		kOPAQUE,	// the alpha value is ignored and the rendered output is fully opaque
		kMASK,	// the rendered output is either fully opaque or fully transparent depending on the alpha value and
				// the specified alpha cutoff value
		kBLEND	// the alpha value is used to composite the source and destination areas. 
				// The rendered output is combined with the background using the normal painting operation 
				// (i.e. the Porter and Duff over operator).
	};

	enum class TextureType
	{
		BaseColor,
		Specular,
		Normal,
		Emissive,
		Occlusion
	};

	struct alignas(16) MaterialData
	{
		float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);		// base color (RGB) and opacity (A)
		// 一个union最多只能有一个初始值
		union
		{
			float4 metallicRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f);	// occlusion (R), metallic (G), roughness (B)
			float4 specularGlossiness;	// specular color (RGB), glossiness (A)
		};
		float3 emissive = float3(0.0f, 0.0f, 0.0f);	// emissive color (RGB)
		float emissiveFactor = 1.0f;
		float alphaCutout = 0.5f;	// alpha threshold, only used in case the alpha mode is mask
		float normalScale = 1.0f;
		float occlusionStrength = 1.0f;
		float f0 = 0.04f;	// or IoR index of refraction
			// f0 = (ior - 1)^2 / (ior + 1)^2
		float specularTransmission = 0.0f;
		uint32_t flags = 0;

		bool operator== (const MaterialData& other) const
		{
			if (baseColor.x != other.baseColor.x ||
				baseColor.y != other.baseColor.y ||
				baseColor.z != other.baseColor.z ||
				baseColor.w != other.baseColor.w)
				return false;
			if (metallicRoughness.x != other.metallicRoughness.x ||
				metallicRoughness.y != other.metallicRoughness.y ||
				metallicRoughness.z != other.metallicRoughness.z ||
				metallicRoughness.w != other.metallicRoughness.w)
				return false;
			if (emissive.x != other.emissive.x ||
				emissive.y != other.emissive.y ||
				emissive.z != other.emissive.z)
				return false;
			if (alphaCutout != other.alphaCutout) return false;
			if (emissiveFactor != other.emissiveFactor) return false;
			if (normalScale != other.normalScale) return false;
			if (occlusionStrength != other.occlusionStrength) return false;
			if (f0 != other.f0) return false;
			if (specularTransmission != other.specularTransmission) return false;
			if (flags != other.flags) return false;
			
			return true;
		}
		bool operator!= (const MaterialData& other) const
		{
			return !(*this == other);
		}
	};

	struct MaterialTextures
	{
		MaterialTextures()
		{
			baseColorSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			metalRoughSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			normalMapSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			emissiveSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			occlusionMapSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE baseColorSRV;
		union
		{
			D3D12_CPU_DESCRIPTOR_HANDLE metalRoughSRV;
			D3D12_CPU_DESCRIPTOR_HANDLE specGlossSRV;
		};
		D3D12_CPU_DESCRIPTOR_HANDLE normalMapSRV;
		D3D12_CPU_DESCRIPTOR_HANDLE emissiveSRV;
		D3D12_CPU_DESCRIPTOR_HANDLE occlusionMapSRV;
	};

	class Material
	{
	public:
		using SharedPtr = std::shared_ptr<Material>;
		using ConstSharedPtrRef = const SharedPtr&;
		using SharedConstPtr = std::shared_ptr<const Material>;

		static const uint32_t TextureNum = 5;

		static SharedPtr Create(const std::string& name) { return SharedPtr(new Material(name)); }

		Material(const std::string& name) : m_Name{ name } {  }

		~Material() = default;

		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const { return m_Name; }

		bool operator== (const Material& other) const;
		bool operator!= (const Material& other) const;

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GetDescriptors()
		{
			std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvs;

			srvs.push_back(m_Textures.baseColorSRV);

			D3D12_CPU_DESCRIPTOR_HANDLE curSrv;
			curSrv = m_Textures.metalRoughSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ?
				m_Textures.metalRoughSRV : m_Textures.baseColorSRV;
			srvs.push_back(curSrv);

			curSrv = m_Textures.normalMapSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ?
				m_Textures.normalMapSRV : m_Textures.baseColorSRV;
			srvs.push_back(curSrv); 
			
			curSrv = m_Textures.occlusionMapSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ?
				m_Textures.occlusionMapSRV : m_Textures.baseColorSRV;
			srvs.push_back(curSrv); 
			
			curSrv = m_Textures.emissiveSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ?
				m_Textures.emissiveSRV : m_Textures.baseColorSRV;
			srvs.push_back(curSrv); 

			return srvs;
		}

		// set the base color texture
		void SetBaseColorTexture(D3D12_CPU_DESCRIPTOR_HANDLE baseColorSRV)
		{
			if (m_Textures.baseColorSRV.ptr != baseColorSRV.ptr)
			{
				m_Textures.baseColorSRV = baseColorSRV;
				UpdateBaseColorType();
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetBaseColorTexture() const { return m_Textures.baseColorSRV; }

		// set the metallic roughness texture
		void SetMetalRoughTexture(D3D12_CPU_DESCRIPTOR_HANDLE metalRoughSRV)
		{
			if (m_Textures.metalRoughSRV.ptr != metalRoughSRV.ptr)
			{
				m_Textures.metalRoughSRV = metalRoughSRV;
				UpdateMetalRoughType();
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetMetalRoughTexture() const { return m_Textures.metalRoughSRV; }

		// set the normal map 
		void SetNormalMap(D3D12_CPU_DESCRIPTOR_HANDLE normalMapSRV)
		{
			if (m_Textures.normalMapSRV.ptr != normalMapSRV.ptr)
			{
				m_Textures.normalMapSRV = normalMapSRV;

				bool bUseTexture = normalMapSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
				SetFlags(PACK_NORMAL_MAP_TYPE(m_MatData.flags, bUseTexture ? NormalMapRGB : NormalMapUnused));	// 目前只考虑NormalMapRGB
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetNormalMap() const { return m_Textures.normalMapSRV; }

		// set the emissive texture
		void SetEmissiveTexture(D3D12_CPU_DESCRIPTOR_HANDLE emissiveSRV)
		{
			if (m_Textures.emissiveSRV.ptr != emissiveSRV.ptr)
			{
				m_Textures.emissiveSRV = emissiveSRV;
				UpdateEmissiveType();
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetEmissiveTexture() const { return m_Textures.emissiveSRV; }

		// set the occlusion texture
		void SetOcclusionTexture(D3D12_CPU_DESCRIPTOR_HANDLE occlusionSRV)
		{
			if (m_Textures.occlusionMapSRV.ptr != occlusionSRV.ptr)
			{
				m_Textures.occlusionMapSRV = occlusionSRV;
				UpdateOcclusionFlag();
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetOcclusionTexture() const { return m_Textures.occlusionMapSRV; }

		// set the base color
		void SetBaseColor(const float4& color)
		{
			// if (m_MatData.baseColor != color)
			{
				m_MatData.baseColor = color;
				UpdateBaseColorType();
			}
		}
		const float4& GetBaseColor() const { return m_MatData.baseColor; }

		// set the metallic roughness parameters
		void SetMetalRoughParams(const float4& color)
		{
			// if (m_MatData.metallicRoughness != color)
			{
				m_MatData.metallicRoughness = color;
				UpdateMetalRoughType();
			}
		}
		const float4& GetMetalRoughParams() const { return m_MatData.metallicRoughness; }

		void SetSpecGlossParams(const float4& color)
		{
			// if (m_MatData.specularGlossiness != color)
			{
				m_MatData.specularGlossiness = color;
				UpdateMetalRoughType();
			}
		}
		const float4& GetSpecGlossParams() const { return m_MatData.specularGlossiness; }

		// set the emissive color
		void SetEmissiveColor(const float3& color)
		{
			// if (m_MatData.emissive != color)
			{
				m_MatData.emissive = color;
				UpdateEmissiveType();
			}
		}
		const float3& GetEmissiveColor() const { return m_MatData.emissive; }

		// set the specular transmission
		void SetSpecularTransmission(float specularTransmission)
		{
			if (m_MatData.specularTransmission != specularTransmission)
			{
				m_MatData.specularTransmission = specularTransmission;
			}
		}
		float GetSpecularTransmission() const { return m_MatData.specularTransmission; }
		
		// set the shading model
		void SetShadingModel(uint32_t model) { SetFlags(PACK_SHADING_MODEL(m_MatData.flags, model)); }
		uint32_t GetShadingModel() const { return EXTRACT_SHADING_MODEL(m_MatData.flags); }

		// set alpha mode
		void SetAlphaMode(uint32_t alphaMode) { SetFlags(PACK_ALPHA_MODE(m_MatData.flags, alphaMode)); }
		uint32_t GetAlphaMode() const { return EXTRACT_ALPHA_MODE(m_MatData.flags); }

		// set the double-sided flag. This flag doesn't affect the rasterizer state, just the shading
		void SetDoubleSided(bool doubleSided) { SetFlags(PACK_DOUBLE_SIDED(m_MatData.flags, doubleSided ? 1 : 0)); }
		bool IsDoubleSided() const { return EXTRACT_DOUBLE_SIDED(m_MatData.flags); }

		// set the alpha threshold. The threshold is only used if the alpha mode is `Mask`
		void SetAlphaThreshold(float alpha) { if (m_MatData.alphaCutout != alpha) m_MatData.alphaCutout = alpha; }
		float GetAlphaThreshold() const { return m_MatData.alphaCutout; }

		// get the flags
		uint32_t GetFlags() const { return m_MatData.flags; }

		const MaterialData& GetMaterialData() const { return m_MatData; }
		MaterialData& GetMaterialData() { return m_MatData; }

		const MaterialTextures& GetMaterialTextures() const { return m_Textures; }
		MaterialTextures& GetMaterialTextures() { return m_Textures; }

		const std::string& GetTexturePath(TextureType type) const
		{
			switch (type)
			{
			case TextureType::BaseColor:
				return m_TexBaseColorPath;
			case TextureType::Specular:
				return m_TexMetalRoughPath;
			case TextureType::Normal:
				return m_TexNormalPath;
			case TextureType::Emissive:
				return m_TexEmissivePath;
			case TextureType::Occlusion:
				return m_TexOcclusionPath;
			default:
				Utility::Printf("Error, no texture type %d\n", (int)type);
				return "";
			}
		}
		std::string& GetTexturePath(TextureType type)
		{
			switch (type)
			{
			case TextureType::BaseColor:
				return m_TexBaseColorPath;
			case TextureType::Specular:
				return m_TexMetalRoughPath;
			case TextureType::Normal:
				return m_TexNormalPath;
			case TextureType::Emissive:
				return m_TexEmissivePath;
			case TextureType::Occlusion:
				return m_TexOcclusionPath;
			default:
				Utility::Printf("Error, no texture type %d\n", (int)type);
				return m_TexBaseColorPath;
			}
		}
		void SetTexturePath(TextureType type, const std::string& filePath)
		{
			if (filePath.empty())
				return;

			switch (type)
			{
			case TextureType::BaseColor:
				m_TexBaseColorPath = filePath;
				UpdateBaseColorType();
				break;
			case TextureType::Specular:
				m_TexMetalRoughPath = filePath;
				UpdateMetalRoughType();
				break;
			case TextureType::Normal:
				m_TexNormalPath = filePath;
				SetFlags(PACK_NORMAL_MAP_TYPE(m_MatData.flags, NormalMapRGB));	// 目前只考虑NormalMapRGB
				break;
			case TextureType::Emissive:
				m_TexEmissivePath = filePath;
				UpdateEmissiveType();
				break;
			case TextureType::Occlusion:
				m_TexOcclusionPath = filePath;
				UpdateOcclusionFlag();
				break;
			default:
				Utility::Printf("Error, no texture type %d\n", (int)type);
				break;
			}
		}

	private:
		void SetFlags(uint32_t flags)
		{
			if (m_MatData.flags != flags)
			{
				m_MatData.flags = flags;
			}
		}

		template <typename T>
		uint32_t GetChannelMode(bool hasTexture, const T& color)
		{
			if (hasTexture) return ChannelTypeTexture;
			if (color.x == 0 && color.y == 0 && color.z == 0) return ChannelTypeUnused;
			return ChannelTypeConst;
		}
		void UpdateBaseColorType()
		{
			SetFlags(PACK_DIFFUSE_TYPE(m_MatData.flags, GetChannelMode(m_Textures.baseColorSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN, m_MatData.baseColor)));
		}
		void UpdateMetalRoughType()
		{
			SetFlags(PACK_SPECULAR_TYPE(m_MatData.flags, GetChannelMode(m_Textures.metalRoughSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN, m_MatData.metallicRoughness)));
		}
		void UpdateEmissiveType()
		{
			SetFlags(PACK_EMISSIVE_TYPE(m_MatData.flags, GetChannelMode(m_Textures.emissiveSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN, m_MatData.emissive)));
		}
		void UpdateOcclusionFlag()
		{
			bool bUseTexture = m_Textures.occlusionMapSRV.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			SetFlags(PACK_OCCLUSION_MAP(m_MatData.flags, bUseTexture ? 1 : 0));
		}

		std::string m_Name;

		// properties
		MaterialData m_MatData;

		// textures
		MaterialTextures m_Textures;
		
		std::string m_TexBaseColorPath;
		std::string m_TexMetalRoughPath;
		std::string m_TexNormalPath;
		std::string m_TexEmissivePath;
		std::string m_TexOcclusionPath;

	public:
		// settings
		AlphaMode eAlphaMode = AlphaMode::kOPAQUE;
		bool doubleSided = false;
		bool unlit = false;
	};
}
