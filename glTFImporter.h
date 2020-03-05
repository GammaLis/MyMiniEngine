#pragma once
#include <string>
#include <vector>
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

	public:
		void Parse(const rapidjson::Document &dom);

		bool BuildScenes();

		void ReadBuffers();
		bool ReadFromFile(const std::string &filePath, uint32_t bufferLength = 0);

		void BuildNodeTree();
		void CacheTransform();

		void ComputeBoundingBox();

		// ���ö������Ը�ʽ����
		void InitVAttribFormats();

		std::string m_glTFJson;
		std::string m_FileDir;
		std::string m_FileName;
		std::vector<std::unique_ptr<unsigned char[]>> m_BinData;
		Matrix4x4 m_DefaultTransorm;

		std::unique_ptr<unsigned char[]> m_VertexData;
		uint32_t m_VertexByteLength = 0;
		std::unique_ptr<unsigned char[]> m_IndexData;
		uint32_t m_IndexByteLength = 0;

		uint32_t m_VertexStride = 0;
		MyDirectX::StructuredBuffer m_VertexBuffer;
		MyDirectX::ByteAddressBuffer m_IndexBuffer;

		//
		int m_DefaultScene = -1;	// Ĭ��Ϊ��
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

		// -mf
		bool m_bDirty = true;
		std::vector<int> m_RootNodes;	// �洢���ڵ㣨һ��ֻ��һ������Ҳ�����ж��NodeTree��

		std::vector<Mesh> m_oMeshes;
		BoundingBox m_BoundingBox;
		vAttribute m_VertexAttributes[Attrib::maxAttrib];	// ���涥�����ԣ�����mesh��������Ӧ��һ�£�
	};

}
