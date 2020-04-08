#pragma once
#include "pch.h"
#include "Math/GLMath.h"
#include "Scenes/Scene.h"
#include "Scenes/SceneDefines.h"
#include "Scenes/Material.h"
#include <unordered_map>

#include "Camera.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

#ifdef _DEBUG
#pragma comment(lib, "assimp-vc142-mtd.lib")
#else
#pragma comment(lib, "assimp-vc142-mt.lib")
#endif

namespace MyDirectX
{
	class GameInput;
}

namespace MFalcor
{
	//using MFalcor::Vector3;
	//using MFalcor::Quaternion;
	//using MFalcor::Matrix4x4;
	//using MFalcor::Scene;

	enum class ImportMode
	{
		Default,
		OBJ,
		GLTF2
	};
	
	// mapping from Assimp to my TextureType
	struct TextureMapping
	{
		aiTextureType srcType;
		unsigned srcIndex;
		TextureType dstType;
	};

	// assimp math cast
	inline Matrix4x4 aiCast(const Math::Matrix4 mat)
	{
		Matrix4x4 glmMat;
		glmMat[0][0] = mat.GetX().GetX(); glmMat[0][1] = mat.GetX().GetY(); glmMat[0][2] = mat.GetX().GetZ(); glmMat[0][3] = mat.GetX().GetW();
		glmMat[1][0] = mat.GetY().GetX(); glmMat[1][1] = mat.GetY().GetY(); glmMat[1][2] = mat.GetY().GetZ(); glmMat[1][3] = mat.GetY().GetW();
		glmMat[2][0] = mat.GetZ().GetX(); glmMat[2][1] = mat.GetZ().GetY(); glmMat[2][2] = mat.GetZ().GetZ(); glmMat[2][3] = mat.GetZ().GetW();
		glmMat[3][0] = mat.GetW().GetX(); glmMat[3][1] = mat.GetW().GetY(); glmMat[3][2] = mat.GetW().GetZ(); glmMat[3][3] = mat.GetW().GetW();
		return glmMat;
	}
	inline Matrix4x4 aiCast(const aiMatrix4x4& aiMat)
	{
		Matrix4x4 glmMat;
		glmMat[0][0] = aiMat.a1; glmMat[0][1] = aiMat.a2; glmMat[0][2] = aiMat.a3; glmMat[0][3] = aiMat.a4;
		glmMat[1][0] = aiMat.b1; glmMat[1][1] = aiMat.b2; glmMat[1][2] = aiMat.b3; glmMat[1][3] = aiMat.b4;
		glmMat[2][0] = aiMat.c1; glmMat[2][1] = aiMat.c2; glmMat[2][2] = aiMat.c3; glmMat[2][3] = aiMat.c4;
		glmMat[3][0] = aiMat.d1; glmMat[3][1] = aiMat.d2; glmMat[3][2] = aiMat.d3; glmMat[3][3] = aiMat.d4;
		return glmMat;
	}
	inline Vector3 aiCast(const aiColor3D& aiColor)
	{
		return Vector3(aiColor.r, aiColor.g, aiColor.b);
	}
	inline Vector3 aiCast(const aiVector3D& val)
	{
		return Vector3(val.x, val.y, val.z);
	}
	inline Quaternion aiCast(const aiQuaternion& q)
	{
		return Quaternion(q.w, q.x, q.y, q.z);
	}

	/** Converts specular power to roughness. Note there is no "the conversion".
		   Reference: http://simonstechblog.blogspot.com/2011/12/microfacet-brdf.html
		   \param specPower specular power of an obsolete Phong BSDF
	   */
	inline float ConvertSpecPowerToRoughness(float specPower)
	{
		return Math::Clamp(sqrt(2.0f / (specPower + 2.0f)), 0.f, 1.f);
	}	

	inline bool IsSrgbRequired(TextureType type, uint32_t shadingModel)
	{
		switch (type)
		{
		case TextureType::Specular:
			ASSERT(shadingModel == ShadingModel_MetallicRoughness || shadingModel == ShadingModel_SpecularGlossiness);
			return (shadingModel == ShadingModel_SpecularGlossiness);
		case TextureType::BaseColor:
		case TextureType::Emissive:
		case TextureType::Occlusion:
			return true;
		case TextureType::Normal:
			return false;
		default:
			ASSERT(false, L"Shouldn't get here");
			return false;
		}
	}

	inline void SetTexture(TextureType type, Material* pMat, D3D12_CPU_DESCRIPTOR_HANDLE srv)
	{
		switch (type)
		{
		case TextureType::BaseColor:
			pMat->SetBaseColorTexture(srv);
			break;
		case TextureType::Specular:
			pMat->SetMetalRoughTexture(srv);
			break;
		case TextureType::Normal:
			pMat->SetNormalMap(srv);
			break;
		case TextureType::Emissive:
			pMat->SetEmissiveTexture(srv);
			break;
		case TextureType::Occlusion:
			pMat->SetOcclusionTexture(srv);
			break;		
		default:
			ASSERT(false, L"Shouldn't get here");
			break;
		}
	}

	// default mapping
	static const std::vector<TextureMapping> kDefaultTextureMapping =
	{
		{aiTextureType_DIFFUSE,	0, TextureType::BaseColor},
		{aiTextureType_SPECULAR,0, TextureType::Specular},
		{aiTextureType_NORMALS, 0, TextureType::Normal},
		{aiTextureType_EMISSIVE,0, TextureType::Emissive},
		{aiTextureType_AMBIENT, 0, TextureType::Occlusion}
	};

	class ImporterData
	{
	public:
		ImporterData(const aiScene* pAiScene, const std::vector<MFalcor::Matrix4x4> &meshInstances = std::vector<Matrix4x4>()) 
			: pScene{ pAiScene }, modelInstances{ meshInstances } {  }

		const aiScene* pScene = nullptr;

		std::map<uint32_t, Material::SharedPtr > materialMap;
		std::map<uint32_t, size_t> meshMap;

		const std::vector<MFalcor::Matrix4x4 >& modelInstances;
		std::map<std::string, MFalcor::Matrix4x4> localToBindPoseMatrices;

		size_t GetSceneNode(const aiNode* pNode) const
		{
			return m_AiToSceneNode.at(pNode);
		}
		
		size_t GetSceneNode(const std::string& aiNodeName, uint32_t index) const
		{
			try 
			{
				return GetSceneNode(m_AiNodes.at(aiNodeName)[index]);
			}
			catch (const std::exception &)
			{
				return MFalcor::Scene::kInvalidNode;
			}
		}

		uint32_t GetNodeInstanceCount(const std::string& nodeName) const
		{
			return (uint32_t)m_AiNodes.at(nodeName).size();
		}

		void AddAiNode(const aiNode* pNode, size_t id)
		{
			ASSERT(m_AiToSceneNode.find(pNode) == m_AiToSceneNode.end());
			m_AiToSceneNode[pNode] = id;

			if (m_AiNodes.find(pNode->mName.C_Str()) == m_AiNodes.end())
			{
				m_AiNodes[pNode->mName.C_Str()] = {};
			}
			m_AiNodes[pNode->mName.C_Str()].push_back(pNode);
		}

	private:
		std::map<const aiNode*, size_t > m_AiToSceneNode;
		std::map<const std::string, std::vector<const aiNode*>> m_AiNodes;
	};

	class AssimpImporter
	{
	public:
		using SharedPtr = std::shared_ptr<AssimpImporter>;

		AssimpImporter() = default;
		AssimpImporter(ID3D12Device* pDevice, const std::string& filePath);

		bool ProcessScenes(const aiScene *scene);
		bool ProcessNodes(const aiNode* curNode, ImporterData& data);
		void ProcessMaterials(const aiScene* scene, ImporterData& importerData);

		// mesh description
		struct Mesh
		{
			std::string name;
			uint32_t vertexCount = 0;	// the number of vertices in the mesh
			uint32_t indexCount = 0;	// the number of indices in the mesh
			uint32_t indexStride = sizeof(uint16_t);
			const uint8_t* pIndices = nullptr;		// byte array of indices. the element count must match `indexCount`
			const Vector3* pPositions = nullptr;	// array of vertex positions.	count = `vertexCount`
			const Vector3* pNormals = nullptr;		// array of veretex normals.	count = `vertexCount`
			const Vector3* pTangents = nullptr;		// array of vertex tangent .	count = `vertexCount`	Assimp tangent-float3, Ò»°ãfloat4
			const Vector3* pBitangents = nullptr;	// array of vertex bitangent	count = `vertexCount`
			const Vector2* pUVs = nullptr;			// array of vertex uv.			count = `veretxCount`
			const Vector3* pLightMapUVs = nullptr;	// array of light-map UVs.		count = `vertexCount`
			const UVector4* pBoneIDs = nullptr;		// array of bone IDs
			const Vector4* pBoneWeights = nullptr;	// array of bone weights.
			D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Material::SharedPtr pMaterial;
		};

		using InstanceMatrices = std::vector<Matrix4x4>;

		static SharedPtr Create(uint32_t indexStride = 2);	// index - uint16_t
		static SharedPtr Create(ID3D12Device* pDevice, const std::string& fileName, const InstanceMatrices& instanceMatrices = InstanceMatrices(),
			uint32_t indexStride = 2);

		// import a scene/model file
		bool Load(ID3D12Device* pDevice, const std::string& fileName, const InstanceMatrices& instances = InstanceMatrices(), uint32_t indexStride = 2);
		bool Init(ID3D12Device* pDevice, Scene* pScene, GameInput *pInput = nullptr);

		// get the scene. Make sure to add all the objects before calling this function
		Scene::SharedPtr GetScene(ID3D12Device *pDevice);

		// add a node to the  graph
		// note that if the node contains data other than the transform matrix (such as meshes or lights), 
		// you'll need to add those objects before adding the node
		size_t AddNode(const Node& node);

		// add a mesh instance to a node
		void AddMeshInstance(size_t nodeID, size_t meshID);

		// add a mesh
		size_t AddMesh(const Mesh& mesh);

		// add a light source
		size_t AddLight();

		// get the number of attached lights
		size_t GetLightCount() const;

		// environment map

		// set the camera
		void SetCamera(const std::shared_ptr<Math::Camera> &pCamera, size_t nodeId = Scene::kInvalidNode);

		// add an animation
		size_t AddAnimation(size_t meshID);

		// set the camera's speed
		void SetCameraSpeed(float speed);

		// check if a camera exists
		bool HasCamera() const { return m_Camera != nullptr; }

		uint32_t m_IndexStride = 2;

	private:
		bool IsBone(ImporterData& data, const std::string& nodeName);
		std::string GetNodeType(ImporterData& data, const aiNode* pNode);
		Matrix4x4 GetLocalToBindPoseMatrix(ImporterData& data, const std::string& name);

		bool CreateSceneGraph(ImporterData& data);
		void CreateBoneList(ImporterData& data);
		// for debug
		void DumpSceneGraphHierarchy(ImporterData& data, const std::string& fileName, aiNode* pRoot);

		bool CreateMeshes(ImporterData& data);
		template <typename T>
		std::unique_ptr<uint8_t[]> CreateIndexList(const aiMesh* curMesh);
		std::vector<Vector2> CreateTexCoordList(const aiVector3D* curTexCoord, uint32_t count);
		void LoadBones(const aiMesh* curMesh, const ImporterData& data,
			std::vector<UVector4> ids, std::vector<Vector4>& weights);

		bool AddMeshes(ImporterData& data, aiNode* pNode);

		bool CreateCamera(ImporterData& data, ImportMode importMode);

		bool CreateLights(ImporterData& data);
		bool CreateDirLight(ImporterData& data, const aiLight* pAiLight);
		bool CreatePointLight(ImporterData& data, const aiLight* pAiLight);

		void LoadTextures(aiMaterial *curMat, Material *dstMat);

		static const std::string s_DefaultDiffusePath;
		static const std::string s_DefaultSpecularPath;
		static const std::string s_DefaultNormalPath;

		std::string m_FilePath;
		std::string m_FileName;
		
		ImportMode m_ImportMode = ImportMode::Default;
		
		// cached D3D device
		ID3D12Device* m_Device = nullptr;

		// ************************************************************
		struct InternalNode : public Node
		{
			InternalNode() = default;
			InternalNode(const Node& n) : Node(n) {}
			std::vector<size_t> children;
			std::vector<size_t> meshes;
		};
		struct MeshSpec
		{
			MeshSpec() = default;
			D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			uint32_t materialId = 0;
			uint32_t indexOffset = 0;
			uint32_t staticVertexOffset = 0;
			uint32_t dynamicVertexOffset = 0;
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			bool hasDynamicData = false;
			std::vector<uint32_t> instances;	// node ids
			// animation ...
		};
		// geometry data
		struct BufferData
		{
			std::vector<uint8_t> indices;		// uint32_t uint16_t
			std::vector<StaticVertexData> staticData;
			std::vector<DynamicVertexData> dynamicData;
		} m_BuffersData;

		using SceneGraph = std::vector<InternalNode>;
		using MeshList = std::vector<MeshSpec>;

		bool m_Dirty = true;
		// cached scene
		Scene::SharedPtr m_Scene;

		SceneGraph m_SceneGraph;
		MeshList m_Meshes;
		std::vector<Material::SharedPtr> m_Materials;
		std::unordered_map<const Material*, uint32_t> m_MaterialToId;

		// camera
		std::shared_ptr<Math::Camera> m_Camera;

		uint32_t AddMaterial(const Material::SharedPtr& pMat, bool removeDuplicate = false);
		std::shared_ptr<StructuredBuffer> CreateVertexBuffer(ID3D12Device *pDevice, Scene* pScene);
		std::shared_ptr<ByteAddressBuffer> CreateIndexBuffer(ID3D12Device* pDevice, Scene* pScene);

		uint32_t CreateMeshData(Scene* pScene);
		void CreateGlobalMatricesBuffer(Scene* pScene);
		void CalculateMeshBoundingBoxes(Scene* pScene);
		void CreateAnimationController(Scene* pScene);

	};

}
