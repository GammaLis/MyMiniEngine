#pragma once
#include "CoreMinimal.h"
#include <set>

// rapidjson
#define RAPIDJSON_NOMEMBERITERATORCLASS
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "glTFCommon.h"
#include "GpuBuffer.h"

#define USE_VERTEX_COMPRESSION 0

// OpenGL glTF, TinyGLTF,...
namespace glTF
{
	struct MeshBatch;
	struct MeshInstance;
	
	class IModelImporter
	{
	public:
		virtual ~IModelImporter() = default;

		virtual bool Load(const std::string &filePath) = 0;
		virtual void Clear() { }
	};
	
	class glTFImporter final : public IModelImporter
	{
	public:
		glTFImporter();
		glTFImporter(const std::string &filePath);

		bool Load(const std::string &filePath) override;
		void Clear() override;

		bool Create(ID3D12Device* pDevice);
		
		Matrix4x4 GetMeshTransform(const Mesh& mesh) const;

		const BoundingBox& GetBoundingBox() const
		{
			return m_BoundingBox;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE* GetSRVs(uint32_t materialIdx) const
		{
			return m_SRVs.get() + materialIdx * Material::TextureNum;
		}

		bool IsValidMaterial(int index)
		{
			return m_ActiveMaterials.find(index) != m_ActiveMaterials.end();
		}
		//???	to be deleted
		ID3D12Device* m_pDevice;
		//
	private:
		void Parse(const rapidjson::Document &dom);

		bool BuildScenes();
		bool BuildMeshes();
		bool BuildMaterials();

		void ReadBuffers();
		bool ReadFromFile(const std::string &filePath, uint32_t bufferLength = 0);

		void BuildNodeTree();
		void CacheTransform();

		void ComputeBoundingBox();
		
		std::string GetImagePath(int curTexIdx, const std::string &defaultPath = "");
		void LoadTextures(ID3D12Device *pDevice);

		// Vertex attribute formats
		void InitVAttribFormats();
		void InitTextures();

	public:
		std::string m_glTFJson;
		std::string m_FileDir;
		std::string m_FileName;
		
		// mesh data
		std::unique_ptr<unsigned char[]> m_VertexData;
		uint32_t m_VertexByteLength = 0;
		std::unique_ptr<unsigned char[]> m_IndexData;
		uint32_t m_IndexByteLength = 0;

		uint32_t m_VertexStride = 0;
		MyDirectX::StructuredBuffer m_VertexBuffer;
		MyDirectX::ByteAddressBuffer m_IndexBuffer;

		// texture data
		std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_SRVs;

	private:
		//
		int m_DefaultScene = -1; // default null
		std::vector<glScene> m_Scenes;
		std::vector<glNode> m_Nodes;

		glCamera m_MainCamera;
		std::vector<glMesh> m_Meshes;

		// buffers & accessors
		std::vector<glBuffer> m_Buffers;
		std::vector<glBufferView> m_BufferViews;
		std::vector<glAccessor> m_Accessors;

		// materials & textures
		std::vector<glMaterial> m_Materials;
		std::vector<glTexture> m_Textures;
		std::vector<glImage> m_Images;
		std::vector<glSampler> m_Samplers;

		std::vector<std::unique_ptr<unsigned char[]>> m_BinData;
		Matrix4x4 m_DefaultTransform;

	public:
		// -mf
		bool m_bDirty = true;
		std::vector<int> m_RootNodes;	// Root node, basically one, but may have multiple NodeTree

		// meshes
		std::vector<Mesh> m_oMeshes;
		BoundingBox m_BoundingBox;
		vAttribute m_VertexAttributes[Attrib::maxAttrib];

		// materials
		std::vector<Material> m_oMaterials;
		std::string m_DefaultBaseColor;
		std::string m_DefaultMetallicRoughness;
		std::string m_DefaultNormal;
		std::string m_DefaultOcclusion;
		std::string m_DefaultEmissive;

		// active meshes & materials
		std::vector<int> m_ActiveNodes;
		std::vector<int> m_ActiveMeshes;	// basically one node has one mesh
		std::map<int, int> m_ActiveMaterials;
		std::set<int> m_ActiveImages;
	};


	struct Vertex
	{
		glm::vec3 p;
#if USE_VERTEX_COMPRESSION
		glm::u8vec4 n;
		glm::u16vec2 uv;
#else
		glm::vec3 n;
		glm::vec2 uv;
#endif
	};

	struct BatchElement
	{
		uint32_t vertexOffset;
		uint32_t vertexCount;
		uint32_t indexOffset;
		uint32_t indexCount;
		uint32_t materialIndex;
	};
	
	struct MeshBatch
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		// std::vector<uint32_t> meshletData;
		// std::vector<Meshlet> meshlets;

		std::vector<BatchElement> batchElements;
	};

	// Instance into one MeshBatch's batchElements
	struct MeshInstance
	{
		glm::mat4 transform;
		uint32_t elementIndex;
		// TODO: duplicate
		uint32_t materialIndex;
	};

	struct DrawObject
	{
		MeshBatch mesh;
		std::vector<MeshInstance> instances;
	};
	
	class glTFImporterNew : public IModelImporter
	{
	public:
		glTFImporterNew();
		
		bool Load(const std::string &filePath) override;

		const auto& GetDrawObjects() const { return m_DrawObjects; }

	private:
		std::unique_ptr<class ImporterImpl> m_Impl;
		std::vector<DrawObject> m_DrawObjects;
	};

}
