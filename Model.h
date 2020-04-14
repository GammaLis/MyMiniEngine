#pragma once
#include "pch.h"
#include "GpuBuffer.h"

struct aiScene;

namespace MyDirectX
{
	class Model
	{
	public:
		Model();
		~Model();

		void Cleanup();

		virtual void Create(ID3D12Device *pDevice);
		virtual void CreateFromAssimp(ID3D12Device* pDevice, const std::string &fileName);
		virtual void Load() {  }

		// attributes
		// 暂定8个
		static const unsigned MaxAttrib = 8;
		enum class AttribMask
		{
			attrib_mask_0 = (1 << 0),
			attrib_mask_1 = (1 << 1),
			attrib_mask_2 = (1 << 2),
			attrib_mask_3 = (1 << 3),
			attrib_mask_4 = (1 << 4),
			attrib_mask_5 = (1 << 5),
			attrib_mask_6 = (1 << 6),
			attrib_mask_7 = (1 << 7),

			// friendly name aliases
			attrib_mask_position = attrib_mask_0,
			attrib_mask_texcoord0 = attrib_mask_1,
			attrib_mask_normal = attrib_mask_2,
			attrib_mask_tangent = attrib_mask_3,
			attrib_mask_bitangent = attrib_mask_4,
		};

		enum class Attrib
		{
			attrib_0 = 0,
			attrib_1 = 1,
			attrib_2 = 2,
			attrib_3 = 3,
			attrib_4 = 4,
			attrib_5 = 5,
			attrib_6 = 6,
			attrib_7 = 7,

			// friendly name aliases
			attrib_position = attrib_0,
			attrib_texcoord0 = attrib_1,
			attrib_normal = attrib_2,
			attrib_tangent = attrib_3,
			attrib_bitangent = attrib_4,

			maxAttrib = 8
		};

		enum class AttribFormat
		{
			attrib_format_none = 0,
			attrib_format_ubyte,
			attrib_format_byte,
			attrib_format_ushort,
			attrib_format_short,
			attrib_format_float,

			attrib_formats
		};

		struct BoundingBox
		{
			Math::Vector3 min;
			Math::Vector3 max;
		};

		struct VAttrib
		{
			uint16_t offset;		// byte offset from the start of the vertex
			uint16_t normalized;	// if true, integer formats are interpreted as [-1, 1] or [0, 1]
			uint16_t components;	// 1-4
			uint16_t format;
		};		

		// mesh
		struct Mesh
		{
			BoundingBox boundingBox;

			unsigned int materialIndex;

			// 顶点属性
			unsigned int attribsEnabled;
			unsigned int vertexStride;
			VAttrib attrib[MaxAttrib];

			unsigned vertexDataByteOffset;
			unsigned vertexCount;
			unsigned indexDataByteOffset;
			unsigned indexCount;
		};
		Mesh *m_pMesh;

		// material
		struct Material
		{
			// constants
			Math::Vector3 diffuse;
			Math::Vector3 specular;
			Math::Vector3 ambient;
			Math::Vector3 emissive;
			Math::Vector3 transparent;	// light passing through a transparent surface is multiplied by this filter color
			float opacity;
			float shininess;			// specular exponent
			float specularStrength;		// multiplier on top of specular color

			// textures (纹理文件路径)
			std::string texDiffusePath;
			std::string texSpecularPath;
			std::string texNormalPath;
			std::string texEmissivePath;
			std::string texLightmapPath;
			std::string texReflectionPath;

			std::string name;
		};
		Material* m_pMaterial;

		unsigned char* m_pVertexData = nullptr;
		unsigned char* m_pIndexData = nullptr;
		StructuredBuffer m_VertexBuffer;
		ByteAddressBuffer m_IndexBuffer;
		uint32_t m_VertexStride = 0;
		uint32_t m_IndexCount = 0;

		//
		uint32_t m_MeshCount = 0;
		uint32_t m_MaterialCount = 0;
		uint32_t m_VertexDataByteSize = 0;
		uint32_t m_IndexDataByteSize = 0;
		BoundingBox m_BoundingBox;

		const BoundingBox& GetBoundingBox() const
		{
			return m_BoundingBox;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE* GetSRVs(uint32_t materialIdx) const
		{
			return m_SRVs + materialIdx * 6;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultSRV() const
		{
			return m_DefaultSRV;
		}

	protected:

		virtual bool LoadFromAssimp(const std::string &fileName);
		virtual void PrepareDataFromScene(const aiScene* scene);

		void ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox& bbox) const;
		void ComputeGlobalBoundingBox(BoundingBox& bbox) const;
		void ComputeAllBoundingBoxes();

		void ReleaseTextures();
		void LoadTextures(ID3D12Device *pDevice);
		void LoadTexturesBySTB_IMAGE(ID3D12Device* pDevice);

		D3D12_CPU_DESCRIPTOR_HANDLE* m_SRVs;
		D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSRV;

		std::string name;

	};

}
