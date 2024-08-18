#pragma once
#include "pch.h"
#include "GpuBuffer.h"

#include "Core/RayTracing/RayTracingHlslCompat.h"

struct aiScene;

namespace MyDirectX
{
	class Model
	{
	public:
		Model();
		virtual ~Model();

		void Cleanup();

		virtual void Create(ID3D12Device *pDevice);
		virtual void CreateFromAssimp(ID3D12Device* pDevice, const std::string &fileName);
		virtual void Load() {  }

		// Attributes
		// Currently default to 8
		static constexpr unsigned kMaxAttrib = 8;
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

			// Friendly name aliases
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

			// Friendly name aliases
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

			Math::Vector3 Center() const { return (min + max) * 0.5f; }
			Math::Vector3 Extent() const { return (max - min) * 0.5f; }
		};

		struct VAttrib
		{
			uint16_t offset;		// byte offset from the start of the vertex
			uint16_t normalized;	// if true, integer formats are interpreted as [-1, 1] or [0, 1]
			uint16_t components;	// 1-4
			uint16_t format;
		};

		struct DrawParams 
		{
			uint32_t indexCount; // number of indices 
			uint32_t startIndex; // offset to first index in index buffer
			uint32_t baseVertex; // offset to first vertex in vertex buffer
		};

		struct DrawInstanceParams 
		{
			uint32_t indexCount; // number of indices 
			uint32_t startIndex; // offset to first index in index buffer
			uint32_t baseVertex; // offset to first vertex in vertex buffer

			uint32_t instanceCount{1};
			uint32_t baseInstance{0};
		};

		// mesh
		static constexpr uint32_t kIndexStride = sizeof(uint16_t);
		struct Mesh
		{
			BoundingBox boundingBox;

			unsigned int materialIndex;

			// Vertex attributes
			unsigned int attribsEnabled;
			unsigned int vertexStride;
			VAttrib attrib[kMaxAttrib];

			unsigned vertexDataByteOffset;
			unsigned vertexCount;
			unsigned indexDataByteOffset;
			unsigned indexCount;

			DrawParams GetDrawParams() const
			{
				DrawParams params;
				params.indexCount = indexCount;
				params.startIndex = indexDataByteOffset / kIndexStride;
				params.baseVertex = vertexDataByteOffset / vertexStride;
				return params;
			}
		};
		std::unique_ptr<Mesh[]> m_Meshes;

		// Material
		static constexpr uint32_t kTexturesPerMaterial = 6;
		struct Material
		{
			// Constants
			Math::Vector3 diffuse;
			Math::Vector3 specular;
			Math::Vector3 ambient;
			Math::Vector3 emissive;
			Math::Vector3 transparent;	// light passing through a transparent surface is multiplied by this filter color
			float opacity;
			float shininess;			// specular exponent
			float specularStrength;		// multiplier on top of specular color

			// Textures
			std::string texDiffusePath;
			std::string texSpecularPath;
			std::string texNormalPath;
			std::string texEmissivePath;
			std::string texLightmapPath;
			std::string texReflectionPath;

			std::string name;
		};
		std::unique_ptr<Material[]> m_Materials;
		std::vector<bool> m_MaterialIsCutout;

		std::unique_ptr<uint8_t[]> m_VertexData;
		std::unique_ptr<uint8_t[]> m_IndexData;

		template <Attrib attrib, typename T = float>
		T* GetVertexData(const Mesh &mesh, uint32_t vertexIndex = 0) const
		{
			auto* data = m_VertexData.get();
			data += mesh.vertexDataByteOffset + vertexIndex * mesh.vertexStride + mesh.attrib[(uint32_t)attrib].offset;
			return reinterpret_cast<T*>(data);
		}

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

		uint32_t GetVertexStride() const { return m_VertexStride; }
		D3D12_VERTEX_BUFFER_VIEW GetVertexBuffer() const { return m_VertexBuffer.VertexBufferView(); }
		D3D12_CPU_DESCRIPTOR_HANDLE GetVertexBufferSRV() const { return m_VertexBuffer.GetSRV(); }
		D3D12_INDEX_BUFFER_VIEW GetIndexBuffer() const { return m_IndexBuffer.IndexBufferView(); }
		D3D12_CPU_DESCRIPTOR_HANDLE GetIndexBufferSRV() const { return m_IndexBuffer.GetSRV(); }

		D3D12_CPU_DESCRIPTOR_HANDLE* GetSRVs(uint32_t materialIdx, uint32_t subIdx = 0) const
		{
			return m_SRVs.get()  + (materialIdx * kTexturesPerMaterial + subIdx);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultSRV() const
		{
			return m_DefaultSRV;
		}

		// Raytracing
		void InitializeRayTraceSceneInfo();
		std::vector<RayTraceMeshInfo> m_MeshInfoData;
		StructuredBuffer m_HitShaderMeshInfoBuffer;

		D3D12_CPU_DESCRIPTOR_HANDLE m_MeshInfoSRV;

	protected:

		virtual bool LoadFromAssimp(const std::string &fileName);
		virtual void PrepareDataFromScene(const aiScene* scene);

		void ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox& bbox) const;
		void ComputeGlobalBoundingBox(BoundingBox& bbox) const;
		void ComputeAllBoundingBoxes();

		void ReleaseTextures();
		void LoadTextures(ID3D12Device *pDevice);
		void LoadTexturesBySTB_IMAGE(ID3D12Device* pDevice);

		std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_SRVs;
		D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSRV;

		std::string name;

	};

}
