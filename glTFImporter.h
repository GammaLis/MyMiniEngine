#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

// rapidjson
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "glTFCommon.h"
#include "GpuBuffer.h"

// OpenGL glTF, TinyGLTF,...
namespace glTF
{
	class glTFImporter
	{
	public:
		glTFImporter();
		glTFImporter(const std::string &glTFFilePath);

		bool Load(const std::string &glTFFilePath);

		bool Create(ID3D12Device* pDevice);

		void Clear();

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
		//临时	to be deleted
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

		// 缓存顶点属性格式
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
		int m_DefaultScene = -1;	// 默认为空
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
		Matrix4x4 m_DefaultTransorm;

	public:
		// -mf
		bool m_bDirty = true;
		std::vector<int> m_RootNodes;	// 存储根节点（一般只有一个，但也可能有多个NodeTree）

		// meshes
		std::vector<Mesh> m_oMeshes;
		BoundingBox m_BoundingBox;
		vAttribute m_VertexAttributes[Attrib::maxAttrib];	// 缓存顶点属性（各个mesh顶点属性应该一致）

		// materials
		std::vector<Material> m_oMaterials;
		std::string m_DefaultBaseColor;
		std::string m_DefaultMetallicRoughness;
		std::string m_DefaultNormal;
		std::string m_DefaultOcclusion;
		std::string m_DefaultEmissive;

		// active meshes & materials
		std::vector<int> m_ActiveNodes;
		std::vector<int> m_ActiveMeshes;	// 一般情况，一个node对应一个mesh
		std::map<int, int> m_ActiveMaterials;	// 一个material可能对应多个mesh (key - matIdx, val - activeMat)
		std::set<int> m_ActiveImages;	// 一个image可能对应多个material
	};

}
