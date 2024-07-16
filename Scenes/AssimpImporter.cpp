#include "AssimpImporter.h"
#include "Graphics.h"
#include "TextureManager.h"
#include <fstream>
#include <functional>

using namespace MFalcor;

const std::string AssimpImporter::s_DefaultDiffusePath	= "default";
const std::string AssimpImporter::s_DefaultSpecularPath	= "default_specular";
const std::string AssimpImporter::s_DefaultNormalPath	= "default_normal";

static AlphaMode Str2AlphaMode(const std::string& strAlphaMode, const std::string& errorInfo = "")
{
	if (strAlphaMode == "OPAQUE")
		return AlphaMode::kOPAQUE;
	else if (strAlphaMode == "MASK")
		return AlphaMode::kMASK;
	else if (strAlphaMode == "BLEND")
		return AlphaMode::kBLEND;
	else
	{
		Utility::Printf("Unsupported alpha mode %s\n", errorInfo + strAlphaMode);
		return AlphaMode::kOPAQUE;
	}
}

AssimpImporter::AssimpImporter(ID3D12Device* pDevice, const std::string& filePath)
{
	m_Device = pDevice;
	Load(pDevice, filePath);
}

AssimpImporter::SharedPtr AssimpImporter::Create(uint32_t indexStride)
{
	SharedPtr pImporter = SharedPtr(new AssimpImporter());
	pImporter->m_IndexStride = indexStride;
	return pImporter;
}

AssimpImporter::SharedPtr AssimpImporter::Create(ID3D12Device* pDevice, const std::string& fileName, const InstanceMatrices& instanceMatrices,
	uint32_t indexStride)
{
	SharedPtr pImporter = SharedPtr(new AssimpImporter());
	pImporter->m_Device = pDevice;
	ASSERT(pImporter->Load(pDevice, fileName, instanceMatrices, indexStride));
	return pImporter;
}

bool AssimpImporter::ProcessScenes(const aiScene* scene, const InstanceMatrices& instanceMatrices)
{
	ImporterData importerData(scene, instanceMatrices);

	// materials
	// Material
	if (scene->HasMaterials())
		ProcessMaterials(scene, importerData);

	// SceneGraph (Node MeshInstance is empty yet）
	if (CreateSceneGraph(importerData) == false)
	{
		Utility::Printf("Can't create lists for model %s\n", m_FilePath);
		return false;
	}

	// Mesh
	if (CreateMeshes(importerData) == false)
	{
		Utility::Printf("Can't create meshes for model %s\n", m_FilePath);
		return false;
	}

	// Node MeshInstance
	if (AddMeshes(importerData, scene->mRootNode) == false)
	{
		Utility::Printf("Can't add meshes for model %s.\n", m_FilePath);
		return false;
	}

	// TODO: -2020-4-4
	// if (CreateAnimations(data) == false) {  }

	if (CreateCamera(importerData, m_ImportMode) == false)
	{
		Utility::Printf("Can't create a camera for model %s.\n", m_FilePath);
	}

	if (CreateLights(importerData) == false)
	{
		Utility::Printf("Can't create lights for model %s.\n", m_FilePath);
	}

	return true;
}

bool AssimpImporter::ProcessNodes(const aiNode* curNode, ImporterData& data)
{
	MFalcor::Node n;
	n.name = curNode->mName.C_Str();
	bool currentIsBone = IsBone(data, n.name);
	ASSERT(currentIsBone == false || curNode->mNumMeshes == 0);

	n.parentIndex = curNode->mParent ? (uint32_t)data.GetSceneNode(curNode->mParent) : Scene::kInvalidNode;
	n.transform = aiCast(curNode->mTransformation);
	n.localToBindSpace = GetLocalToBindPoseMatrix(data, n.name);
	data.AddAiNode(curNode, AddNode(n));

	bool b = true;
	// visit the children
	for (uint32_t i = 0; i < curNode->mNumChildren; ++i)
	{
		b |= ProcessNodes(curNode->mChildren[i], data);
	}
	return b;
}

void AssimpImporter::ProcessMaterials(const aiScene* scene, ImporterData& importerData)
{
	// init texture manager 
	std::string pathRoot = "Textures/";
	Graphics::s_TextureManager.Init(std::wstring(pathRoot.begin(), pathRoot.end()));

	const auto matArr = scene->mMaterials;

	for (uint32_t matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx)
	{
		const auto curMat = matArr[matIdx];

		// name
		aiString name;
		curMat->Get(AI_MATKEY_NAME, name);

		Material::SharedPtr newMat = Material::Create(name.C_Str());

		// common
		// opacity
		float opacity = 1.0f;
		curMat->Get(AI_MATKEY_OPACITY, opacity);
		float4 diffuse = float4(1.0f, 1.0f, 1.0f, opacity);

		// normal scaling
		float bumpScaling = 1.0f;
		curMat->Get(AI_MATKEY_BUMPSCALING, bumpScaling);
		newMat->GetMaterialData().normalScale = bumpScaling;

		// refract (IoR)
		float refract = 1.5f;
		if (curMat->Get(AI_MATKEY_REFRACTI, refract) == AI_SUCCESS)
		{
			float f0 = (refract - 1) * (refract - 1) / ((refract + 1) * (refract + 1));
			f0 = std::max(f0, 0.04f);
			newMat->GetMaterialData().f0 = f0;
		}

		// config
		// double sided
		bool doubleSided = false;
		curMat->Get(AI_MATKEY_TWOSIDED, doubleSided);
		newMat->doubleSided = doubleSided;
		newMat->SetDoubleSided(doubleSided);

		// common textures
		aiString normalPath;
		if (curMat->GetTexture(aiTextureType_NORMALS, 0, &normalPath) == AI_SUCCESS)
		{
			newMat->SetTexturePath(TextureType::Normal, normalPath.C_Str());
		}
		else
		{
			newMat->SetTexturePath(TextureType::Normal, s_DefaultNormalPath);
		}

		aiString emissivePath;
		if (curMat->GetTexture(aiTextureType_EMISSIVE, 0, &emissivePath) == AI_SUCCESS)
		{
			newMat->SetTexturePath(TextureType::Emissive, emissivePath.C_Str());
		}
		float3 emissive = float3(0.0f, 0.0f, 0.0f);
		aiColor3D emissiveColor;
		if (curMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS)
		{
			emissive = float3(emissiveColor.r, emissiveColor.g, emissiveColor.b);
		}
		newMat->SetEmissiveColor(emissive);

		aiString occlusionPath;
		if (curMat->GetTexture(aiTextureType_AMBIENT, 0, &occlusionPath) == AI_SUCCESS)
		{
			newMat->SetTexturePath(TextureType::Occlusion, occlusionPath.C_Str());
		}

		if (m_ImportMode == ImportMode::GLTF2)
		{
			// config
			// unlit
			bool unlit = false;
			curMat->Get(AI_MATKEY_GLTF_UNLIT, unlit);
			newMat->unlit = unlit;
			// alpha mode
			aiString strAlphaMode;
			curMat->Get(AI_MATKEY_GLTF_ALPHAMODE, strAlphaMode);
			newMat->eAlphaMode = Str2AlphaMode(strAlphaMode.C_Str(), name.C_Str());
			if (newMat->eAlphaMode == AlphaMode::kMASK)
			{
				// alpha cutout
				float alphaCutout = 0.5f;
				curMat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutout);
				newMat->GetMaterialData().alphaCutout = alphaCutout;
				newMat->SetAlphaMode(AlphaModeMask);
			}

			aiString baseColorPath;
			if (curMat->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, &baseColorPath) == AI_SUCCESS)
			{
				newMat->SetTexturePath(TextureType::BaseColor, baseColorPath.C_Str());
			}
			else
			{
				newMat->SetTexturePath(TextureType::BaseColor, s_DefaultDiffusePath);
			}
			aiColor4D baseColorFactor;
			if (curMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColorFactor) == AI_SUCCESS)
			{
				diffuse = float4(baseColorFactor.r, baseColorFactor.g, baseColorFactor.b, diffuse.w);
			}
			newMat->SetBaseColor(diffuse);

			aiString metalRoughPath;
			if (curMat->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &metalRoughPath) == AI_SUCCESS)
			{
				newMat->SetTexturePath(TextureType::Specular, metalRoughPath.C_Str());
			}
			float metallic = 1.0f;
			curMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic);
			newMat->GetMaterialData().metallicRoughness.y = metallic;

			float roughness = 0.5f;
			curMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness);
			newMat->GetMaterialData().metallicRoughness.z = roughness;

			// TODO: specular glossiness	-2020-4-1
			bool isSpecularGlossiness = false;
			if (curMat->Get(AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS, isSpecularGlossiness) == AI_SUCCESS)
			{
				if (isSpecularGlossiness)
				{
					Utility::Print("Warning: pbrSpecularGlossiness textures are not currently supported\n");
				}
			}

			// shading model
			if (unlit)
				newMat->SetShadingModel(ShadingModel_Unlit);
			else
				newMat->SetShadingModel(ShadingModel_MetallicRoughness);
		}
		else
		{
			// texture path
			aiString diffusePath;
			if (curMat->GetTexture(aiTextureType_DIFFUSE, 0, &diffusePath) == AI_SUCCESS)
			{
				newMat->SetTexturePath(TextureType::BaseColor, diffusePath.C_Str());
			}
			else
			{
				newMat->SetTexturePath(TextureType::BaseColor, s_DefaultDiffusePath);
			}
			// diffuse color
			aiColor3D diffuseColor;
			if (curMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS)
			{
				diffuse.x = diffuseColor.r;
				diffuse.y = diffuseColor.g;
				diffuse.z = diffuseColor.b;
			}
			newMat->SetBaseColor(diffuse);

			// specular 
			aiString specularPath;
			if (curMat->GetTexture(aiTextureType_SPECULAR, 0, &specularPath) == AI_SUCCESS)
			{
				newMat->SetTexturePath(TextureType::Specular, specularPath.C_Str());
			}
			else
			{
				newMat->SetTexturePath(TextureType::Specular, s_DefaultSpecularPath);
			}
			// specGloss
			float shininess = 0.5f;
			if (curMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
			{
				if (m_ImportMode == ImportMode::OBJ)
				{
					shininess = 1.0f - ConvertSpecPowerToRoughness(shininess);
				}
			}
			float4 specGloss = float4(1.0f, 1.0f, 1.0f, shininess);
			aiColor3D specularColor;
			if (curMat->Get(AI_MATKEY_COLOR_SPECULAR, specularColor) == AI_SUCCESS)
			{
				specGloss = float4(specularColor.r, specularColor.g, specularColor.b, specGloss.w);
			}
			newMat->SetSpecGlossParams(specGloss);

			newMat->SetShadingModel(ShadingModel_SpecularGlossiness);
		}

		// textures
		LoadTextures(curMat, newMat.get());

		importerData.materialMap[matIdx] = newMat;
	}
}

bool AssimpImporter::Load(ID3D12Device* pDevice, const std::string& filePath, const InstanceMatrices& instances, uint32_t indexStride)
{
	Clear();

	Assimp::Importer importer;

	// and have it read the given file with some example postprocessing
	// usually - if speed is not the most important aspect for you - you'll
	// probably to request more postprocessing than we do in this example.
	const aiScene *scene = importer.ReadFile(filePath,
		aiProcess_CalcTangentSpace |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);

	if (!scene)
	{
		Utility::Print(importer.GetErrorString());
		return false;
	}

	// cached d3d device
	m_Device = pDevice;

	// path root
	m_FilePath = filePath;
	m_FileName = StringUtils::GetFileNameWithNoExtensions(filePath);

	m_IndexStride = indexStride;

	// enable special treatment for gltf files
	if (StringUtils::HasSuffix(filePath, ".gltf") || StringUtils::HasSuffix(filePath, ".gdb"))
		m_ImportMode = ImportMode::GLTF2;

	ProcessScenes(scene, instances);

	return true;
}

bool AssimpImporter::AddToScene(ID3D12Device* pDevice, Scene* pScene)
{
	if (pDevice == nullptr || pScene == nullptr)
		return false;

	if (m_Meshes.empty())
	{
		Utility::Print("Can't build scene. No meshes were loaded");
		return false;
	}

	if (pScene->GetCamera() == nullptr && m_Camera != nullptr)
	{
		pScene->m_Camera = m_Camera;
	}

	m_SceneMaterialOffset = (uint32_t)pScene->m_Materials.size();
	m_SceneMeshOffset = (uint32_t)pScene->m_MeshDescs.size();
	
	pScene->m_Name = m_FileName;

	// Lights
	// pScene->m_Lights
	
	// Materials
	pScene->m_Materials.insert(pScene->m_Materials.end(), m_Materials.begin(), m_Materials.end());

	CreateGlobalMatricesBuffer(pScene);
	// Meshes
	uint32_t drawCount = CreateMeshData(pScene);
	CreateVertexBuffer(pDevice, pScene);
	CreateIndexBuffer(pDevice, pScene);
	// InstanceBuffer creating delayed, Scene::Finalize() may sort
	// CreateInstanceBuffer(pDevice, pScene, drawCount);
	CalculateMeshBoundingBoxes(pScene);
	pScene->Finalize(pDevice);
	
	return true;
}

void AssimpImporter::Clear() 
{
	m_Dirty = true;
	m_Scene = nullptr;
	m_Camera = nullptr;

	m_SceneMaterialOffset = 0;
	m_SceneMeshOffset = 0;

	m_SceneGraph.clear();
	m_Meshes.clear();
	m_Materials.clear();
	m_MaterialToId.clear();

	m_BuffersData.Clear();
}

Scene::SharedPtr AssimpImporter::GetScene(ID3D12Device* pDevice)
{
	// we cache the scene because creating it is not cheap
	if (m_Scene && !m_Dirty) return m_Scene;

	m_Scene = Scene::Create();
	AddToScene(pDevice, m_Scene.get());

	return m_Scene;
}

// SceneGraph Add Node
size_t AssimpImporter::AddNode(const Node& node)
{
	ASSERT(node.parentIndex == Scene::kInvalidNode || node.parentIndex < m_SceneGraph.size());

	size_t newNodeId = m_SceneGraph.size();
	ASSERT(newNodeId <= UINT32_MAX);
	m_SceneGraph.push_back(InternalNode(node));
	if (node.parentIndex != Scene::kInvalidNode)
		m_SceneGraph[node.parentIndex].children.push_back(newNodeId);
	m_Dirty = true;
	return newNodeId;
}

// Add MeshInstance
// SceneGraph node Add MeshInstance Id
void AssimpImporter::AddMeshInstance(size_t nodeID, size_t meshID)
{
	ASSERT(meshID < m_Meshes.size());
	m_SceneGraph.at(nodeID).meshes.push_back(meshID);
	m_Meshes.at(meshID).instances.push_back((uint32_t)nodeID);
	m_Dirty = true;
}

// Add Mesh (Differs from MeshInstance)
size_t AssimpImporter::AddMesh(const Mesh& mesh)
{
	const auto& prevMesh = m_Meshes.empty() ? MeshSpec() : m_Meshes.back();

	// create the new mesh spec
	m_Meshes.push_back(MeshSpec());
	MeshSpec& spec = m_Meshes.back();
	spec.staticVertexOffset = (uint32_t)m_BuffersData.staticData.size();
	spec.dynamicVertexOffset = (uint32_t)m_BuffersData.dynamicData.size();
	spec.vertexCount = mesh.vertexCount;
	spec.indexOffset = (uint32_t)m_BuffersData.indices.size();
	spec.indexCount = mesh.indexCount;
	spec.topology = mesh.topology;
	spec.materialId = AddMaterial(mesh.pMaterial);

	// initialize the static data
	if (mesh.indexCount == 0 || !mesh.pIndices) ASSERT(false, "Missing indices");
	m_BuffersData.indices.insert(m_BuffersData.indices.end(), mesh.pIndices, mesh.pIndices + mesh.indexCount * mesh.indexStride);

	if (mesh.vertexCount == 0) ASSERT(false, "Missing vertices");
	if (mesh.pPositions == nullptr) ASSERT(false, "Missing positions");
	if (mesh.pNormals == nullptr) Utility::Print("Missing normals");
	if (mesh.pTangents == nullptr) Utility::Print("Missing tangents");
	if (mesh.pBitangents == nullptr) Utility::Print("Missing bitangents");
	if (mesh.pUVs == nullptr) Utility::Print("Missing uvs");

	// initialize the dynamic data
	if (mesh.pBoneIDs || mesh.pBoneWeights)
	{
		if (mesh.pBoneIDs == nullptr) ASSERT(false, "Missing boneIDs");
		if (mesh.pBoneWeights == nullptr) ASSERT(false, "Missing boneWeights");
		spec.hasDynamicData = true;
	}

	for (uint32_t v = 0; v < mesh.vertexCount; ++v)
	{
		StaticVertexData sVertex;
		sVertex.position = float3((float*)&mesh.pPositions[v]);
		sVertex.normal = mesh.pNormals ? float3((float*)&mesh.pNormals[v]) : float3(0, 0, 0);
		sVertex.tangent = mesh.pTangents ? float3((float*)&mesh.pTangents[v]) : float3(0, 0, 0);
		sVertex.bitangent = mesh.pBitangents ? float3((float*)&mesh.pBitangents[v]) : float3(0, 0, 0);
		sVertex.uv = mesh.pUVs ? float2((float*)&mesh.pUVs[v]) : float2(0, 0);

		m_BuffersData.staticData.emplace_back(std::move(sVertex));

		if (mesh.pBoneWeights)
		{
			DynamicVertexData dVertex;
			dVertex.boneIDs = uint4((uint32_t*)&mesh.pBoneIDs[v]);
			dVertex.boneWeights = float4((float*)&mesh.pBoneWeights[v]);
			dVertex.staticIndex = (uint32_t)m_BuffersData.staticData.size() - 1;
			m_BuffersData.dynamicData.emplace_back(std::move(dVertex));
		}
	}

	m_Dirty = true;

	return m_Meshes.size() - 1;
}

void AssimpImporter::SetCamera(const std::shared_ptr<Math::Camera>& pCamera, size_t nodeId)
{
	// TODO..
}

bool AssimpImporter::IsBone(ImporterData& data, const std::string& nodeName)
{
	return data.localToBindPoseMatrices.find(nodeName) != data.localToBindPoseMatrices.end();
}

std::string AssimpImporter::GetNodeType(ImporterData& data, const aiNode* pNode)
{
	if (pNode->mNumMeshes > 0) return "mesh instance";
	if (IsBone(data, pNode->mName.C_Str())) return "bone";
	return "local transform";
}

Matrix4x4 AssimpImporter::GetLocalToBindPoseMatrix(ImporterData& data, const std::string& name)
{
	return IsBone(data, name) ? data.localToBindPoseMatrices[name] : Matrix4x4();
}

bool AssimpImporter::CreateSceneGraph(ImporterData& data)
{
	CreateBoneList(data);
	aiNode* rootNode = data.pScene->mRootNode;
	ASSERT(IsBone(data, rootNode->mName.C_Str()) == false);
	bool success = ProcessNodes(rootNode, data);
	// DumpSceneGraphHierarchy(data, "SceneGraph.dotfile", rootNode);	// used for debugging
	return success;
}

void AssimpImporter::CreateBoneList(ImporterData& data)
{
	const aiScene* pScene = data.pScene;
	auto& boneMatrices = data.localToBindPoseMatrices;

	for (uint32_t meshId = 0; meshId < pScene->mNumMeshes; ++meshId)
	{
		const aiMesh* curMesh = pScene->mMeshes[meshId];
		if (curMesh->HasBones() == false) continue;
		for (uint32_t boneId = 0; boneId < curMesh->mNumBones; ++boneId)
		{
			boneMatrices[curMesh->mBones[boneId]->mName.C_Str()] = aiCast(curMesh->mBones[boneId]->mOffsetMatrix);
		}
	}
}

void AssimpImporter::DumpSceneGraphHierarchy(ImporterData& data, const std::string& fileName, aiNode* pRoot)
{
	std::ofstream ofs;
	ofs.open(fileName);
	if (ofs.fail())
	{
		Utility::Printf("Couldn't open file %s.\n", fileName.c_str());
		return;
	}

	std::function<void(const aiNode* pNode)> DumpNode = [this, &ofs, &DumpNode, &data](const aiNode* pNode)
	{
		for (uint32_t i = 0; i < pNode->mNumChildren; ++i)
		{
			const aiNode* child = pNode->mChildren[i];
			std::string parent = pNode->mName.C_Str();
			std::string parentType = GetNodeType(data, pNode);
			std::string parentId = std::to_string(data.GetSceneNode(pNode));
			std::string me = child->mName.C_Str();
			std::string myType = GetNodeType(data, child);
			std::string myId = std::to_string(data.GetSceneNode(child));
			std::replace(parent.begin(), parent.end(), '.', '_');
			std::replace(me.begin(), me.end(), '.', '_');
			std::replace(parent.begin(), parent.end(), '$', '_');
			std::replace(me.begin(), me.end(), '$', '_');

			ofs << parentId << " " << parent << " (" << parentType << ") " << " -> " << myId << " " << me << " (" << myType << ") " << std::endl;

			DumpNode(child);
		}
	};

	// Header
	ofs << "digraph SceneGraph {" << std::endl;
	DumpNode(pRoot);
	// close the file
	ofs << "}" << std::endl;	// closing graphs scope
	ofs.close();
}

bool AssimpImporter::CreateMeshes(ImporterData& data)
{
	const aiScene* pScene = data.pScene;
	// pass 1
	uint32_t totalVertexNum = 0;
	uint32_t totalIndexNum = 0;
	for (uint32_t i = 0; i < pScene->mNumMeshes; ++i)
	{
		const aiMesh* curMesh = pScene->mMeshes[i];
		
		totalVertexNum += curMesh->mNumVertices;
		totalIndexNum += curMesh->mNumFaces * curMesh->mFaces[0].mNumIndices;
	}
	m_BuffersData.dynamicData.reserve(totalVertexNum);
	m_BuffersData.staticData.reserve(totalVertexNum);
	m_BuffersData.indices.reserve(totalIndexNum * m_IndexStride);
	m_Meshes.reserve(pScene->mNumMeshes);

	// pass 2
	for (uint32_t i = 0; i < pScene->mNumMeshes; ++i)
	{
		const aiMesh* curMesh = pScene->mMeshes[i];

		Mesh newMesh;
		// indices
		auto pIndexList = m_IndexStride == 2 ? CreateIndexList<uint16_t>(curMesh) : CreateIndexList<uint32_t>(curMesh);
		newMesh.pIndices = pIndexList.get();
		newMesh.indexStride = m_IndexStride;
		newMesh.indexCount = curMesh->mNumFaces * curMesh->mFaces[0].mNumIndices;

		// vertices
		newMesh.vertexCount = curMesh->mNumVertices;
		newMesh.pPositions = (Vector3*)curMesh->mVertices;
		newMesh.pNormals = (Vector3*)curMesh->mNormals;
		newMesh.pTangents = (Vector3*)curMesh->mTangents;
		newMesh.pBitangents = (Vector3*)curMesh->mBitangents;
		// texCoords is destroyed!
	#if 0
		if (curMesh->HasTextureCoords(0))
		{
			const auto& texCoords = CreateTexCoordList(curMesh->mTextureCoords[0], newMesh.vertexCount);
			newMesh.pUVs = texCoords.data();
		}
		else
	#endif
			newMesh.pUVs = nullptr;
		const auto& texCoords = curMesh->HasTextureCoords(0) ?
			CreateTexCoordList(curMesh->mTextureCoords[0], newMesh.vertexCount) : std::vector<Vector2>();
		newMesh.pUVs = !texCoords.empty() ? texCoords.data() : nullptr;

		// bones
		std::vector<UVector4> boneIds;		// can't be in 'if()', note lifetime
		std::vector<Vector4> boneWeights;
		if (curMesh->HasBones())
		{
			LoadBones(curMesh, data, boneIds, boneWeights);
			newMesh.pBoneIDs = boneIds.data();
			newMesh.pBoneWeights = boneWeights.data();
		}
		switch (curMesh->mFaces[0].mNumIndices)
		{
		case 1: newMesh.topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
		case 2: newMesh.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
		case 3: newMesh.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
		default:
			ASSERT(false, "Error when creating mesh. Unknown topology with %s\n", curMesh->mFaces[0].mNumIndices, " indices.")
		}
		newMesh.pMaterial = data.materialMap.at(curMesh->mMaterialIndex);
		uint32_t meshID = (uint32_t)AddMesh(newMesh);
		if (meshID == Scene::kInvalidNode) return false;
		data.meshMap[i] = meshID;
	}
	return true;
}

template <typename T>
std::unique_ptr<uint8_t[]> AssimpImporter::CreateIndexList(const aiMesh* curMesh)
{
	const uint32_t perFaceIndexCount = curMesh->mFaces[0].mNumIndices;
	const uint32_t indexCount = curMesh->mNumFaces * perFaceIndexCount;

	uint8_t* indices = new uint8_t[indexCount * sizeof(T)];
	T* pOffset = reinterpret_cast<T*>(indices);
	for (uint32_t i = 0; i < curMesh->mNumFaces; ++i)
	{
		aiFace curFace = curMesh->mFaces[i];
		ASSERT(curFace.mNumIndices == perFaceIndexCount);
		for (uint32_t j = 0; j < perFaceIndexCount; ++j)
		{
			*pOffset++ = curFace.mIndices[j];
		}
	}

	return std::unique_ptr<uint8_t[]>(indices);
}

std::vector<Vector2> AssimpImporter::CreateTexCoordList(const aiVector3D* curTexCoord, uint32_t count)
{
	std::vector<Vector2> uv2(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		uv2[i] = Vector2(curTexCoord[i].x, curTexCoord[i].y);
	}
	return uv2;
}

void AssimpImporter::LoadBones(const aiMesh* curMesh, const ImporterData& data, 
	std::vector<UVector4> ids, std::vector<Vector4>& weights)
{
	const uint32_t vertexCount = curMesh->mNumVertices;
	ids.resize(vertexCount);
	weights.resize(vertexCount);

	for (uint32_t boneIdx = 0; boneIdx < curMesh->mNumBones; ++boneIdx)
	{
		const auto curBone = curMesh->mBones[boneIdx];
		ASSERT(data.GetNodeInstanceCount(curBone->mName.C_Str()) == 1);
		size_t aiBoneID = data.GetSceneNode(curBone->mName.C_Str(), 0);

		// the way Assimp works, the weights holds the IDs of the vertices it affects.
		// we loop over all the weights, initializing the vertices data along the way
		for (uint32_t weightIdx = 0; weightIdx < curBone->mNumWeights; ++weightIdx)
		{
			// get the vertex the current weight affects
			const auto& curVertexWeight = curBone->mWeights[weightIdx];
			
			// get the address of the Bone ID and weight for the current vertex
			UVector4& vertexIds = ids[curVertexWeight.mVertexId];
			Vector4& vertexWeights = weights[curVertexWeight.mVertexId];

			// find the next unused slot in the bone array of the vertex, and initialize it with the current value
			bool emptySlotFount = false;
			for (uint32_t i = 0; i < Scene::kMaxBonesPerVertex; ++i)
			{
				if (vertexWeights[i] == 0)
				{
					vertexIds[i] = (uint32_t)aiBoneID;
					vertexWeights[i] = curVertexWeight.mWeight;
					emptySlotFount = true;
					break;
				}
			}
			if (emptySlotFount == false) ASSERT(false, "One of the vertices has too many bones attached to it.")
		}

		// normalize the weights for each vertex, since in some models the sum is larger than 1
		for (uint32_t i = 0; i < vertexCount; ++i)
		{
			Vector4& w = weights[i];
			float f = 0;
			for (uint j = 0; j < Scene::kMaxBonesPerVertex; ++j) f += w[j];
			w /= f;
		}
	}
}

// Add Node MeshInstance
bool AssimpImporter::AddMeshes(ImporterData& data, aiNode* pNode)
{
	size_t nodeId = data.GetSceneNode(pNode);
	for (size_t meshIdx = 0; meshIdx < pNode->mNumMeshes; ++meshIdx)
	{
		size_t meshId = data.meshMap.at(pNode->mMeshes[meshIdx]);

		if (!data.modelInstances.empty())
		{
			for (size_t instanceIdx = 0, maxIdx = data.modelInstances.size(); instanceIdx < maxIdx; ++instanceIdx)
			{
				size_t instanceNode = nodeId;
				if (data.modelInstances[instanceIdx] != Matrix4x4())
				{
					// add nodes
					Node newNode;
					newNode.name = "Node" + std::to_string(nodeId) + ".instance" + std::to_string(instanceIdx);
					newNode.parentIndex = (uint32_t)nodeId;
					newNode.transform = data.modelInstances[instanceIdx];
					instanceNode = AddNode(newNode);
				}
				AddMeshInstance(instanceNode, meshId);
			}
		}
		else
			AddMeshInstance(nodeId, meshId);
	}

	bool b = true;
	// visit the children
	for (uint32_t i = 0; i < pNode->mNumChildren; ++i)
	{
		b |= AddMeshes(data, pNode->mChildren[i]);
	}

	return b;
}

bool AssimpImporter::CreateCamera(ImporterData& data, ImportMode importMode)
{
	if (data.pScene->mNumCameras == 0) return true;
	if (data.pScene->mNumCameras > 1)
	{
		Utility::Print("Model file contains more than a single camera. We will use the first camera");
	}
	if (HasCamera())
	{
		Utility::Print("Found cameras in model file, but the scene already contains a camera.");
		return true;
	}

	const aiCamera* pAiCamera = data.pScene->mCameras[0];
	std::shared_ptr<Math::Camera> newCamera(new Math::Camera());
	auto AiVector3Cast = [](const auto &aiVec3) -> Math::Vector3
	{
		return Math::Vector3(aiVec3.x, aiVec3.y, aiVec3.z);
	};
	newCamera->SetEyeAtUp(
		AiVector3Cast(pAiCamera->mPosition),
		AiVector3Cast(pAiCamera->mPosition + pAiCamera->mLookAt),
		AiVector3Cast(pAiCamera->mUp));
	float aspect = pAiCamera->mAspect != 0 ? pAiCamera->mAspect : 16.0f / 9.0f;
	float verticalFov = 2.0f * std::atanf(std::tanf(pAiCamera->mHorizontalFOV) / aspect);
	newCamera->SetPerspectiveMatrix(verticalFov, 1.0f / aspect, pAiCamera->mClipPlaneNear, pAiCamera->mClipPlaneFar);
	newCamera->Update();

	size_t nodeId = data.GetSceneNode(pAiCamera->mName.C_Str(), 0);
	if (nodeId != Scene::kInvalidNode)
	{
		Node n;
		n.name = "Camera.BaseMatrix";
		n.parentIndex = (uint32_t)nodeId;
		n.transform = aiCast(newCamera->GetViewMatrix());
		// GLTF2 has the view direction reversed
		if (m_ImportMode == ImportMode::GLTF2) n.transform[2] = -n.transform[2];
		nodeId = AddNode(n);
	}

	// find if the camera is affected by a node
	SetCamera(newCamera, nodeId);
	return true;
}

bool AssimpImporter::CreateLights(ImporterData& data)
{
	for (uint32_t i = 0; i < data.pScene->mNumLights; ++i)
	{
		const aiLight* pAiLight = data.pScene->mLights[i];
		switch (pAiLight->mType)
		{
		case aiLightSource_DIRECTIONAL:
			if (!CreateDirLight(data, pAiLight)) return false;
			break;
		case aiLightSource_POINT:
		case aiLightSource_SPOT:
			if (!CreatePointLight(data, pAiLight)) return false;
			break;
		default:
			Utility::Printf("Unsupported Assimp light type %s.\n", std::to_string(pAiLight->mType));
			break;
		}
	}
	return true;
}

bool AssimpImporter::CreateDirLight(ImporterData& data, const aiLight* pAiLight)
{
	// TODO
	return true;
}

bool AssimpImporter::CreatePointLight(ImporterData& data, const aiLight* pAiLight)
{
	// TODO
	return true;
}

void AssimpImporter::LoadTextures(aiMaterial* curMat, Material* dstMat)
{
	const auto& textureMappings = kDefaultTextureMapping;
	for (const auto& tex : textureMappings)
	{
		std::string path(dstMat->GetTexturePath(tex.dstType));
		if (!path.empty())
		{
			std::string texName = path;
			if (path.rfind('.') != path.npos)	// `default` textures have no ".format"
			{
				texName = StringUtils::GetFileNameWithNoExtensions(path);
				texName = m_FileName + "/" + texName;
				dstMat->GetTexturePath(tex.dstType) = texName;
			}

			auto pManagedTex = Graphics::s_TextureManager.LoadFromFile(m_Device, texName,
				IsSrgbRequired(tex.dstType, dstMat->GetShadingModel()));
			if (pManagedTex->IsValid())
				SetTexture(tex.dstType, dstMat, pManagedTex->GetSRV());
		}
	}
}

uint32_t AssimpImporter::AddMaterial(const Material::SharedPtr& pMat, bool removeDuplicate)
{
	// reuse previously added materials
	auto it = std::find(m_Materials.begin(), m_Materials.end(), pMat);
	if (it != m_Materials.end())
	{
		return (uint32_t)std::distance(m_Materials.begin(), it);
	}

	// try to find previously added material with equal properties (duplicate)
	it = std::find_if(m_Materials.begin(), m_Materials.end(), [&pMat](const Material::SharedPtr& m) {
		return *m == *pMat;
		});
	if (it != m_Materials.end())
	{
		const auto& equalMat = *it;

		// Assimp sometimes creates internal copies of a material: always de-duplicate if name and properties are equal
		if (removeDuplicate || pMat->GetName() == equalMat->GetName())
		{
			return (uint32_t)std::distance(m_Materials.begin(), it);
		}
		else
		{
			Utility::Printf("Material %s is a deplicate (has equal properties) of material %s.\n",
				pMat->GetName(), equalMat->GetName());
		}
	}
	m_Materials.push_back(pMat);
	m_Dirty = true;
	return (uint32_t)m_Materials.size() - 1;
}

std::shared_ptr<StructuredBuffer> AssimpImporter::CreateVertexBuffer(ID3D12Device* pDevice, Scene *pScene)
{
	size_t staticVbSize = sizeof(StaticVertexData) * m_BuffersData.staticData.size();
	std::shared_ptr<StructuredBuffer> pVB = std::make_shared<StructuredBuffer>();
	std::wstring vbName = std::wstring(m_FileName.begin(), m_FileName.end());
	pVB->Create(pDevice, vbName + L"_VertexBuffer", (uint32_t)m_BuffersData.staticData.size(), sizeof(StaticVertexData), m_BuffersData.staticData.data());

	// layout
	VertexBufferLayout::SharedPtr pLayout = VertexBufferLayout::Create();
	pLayout->AddElement("POSITION",	DXGI_FORMAT_R32G32B32_FLOAT);
	pLayout->AddElement("NORMAL",	DXGI_FORMAT_R32G32B32_FLOAT);
	pLayout->AddElement("TANGENT",	DXGI_FORMAT_R32G32B32_FLOAT);
	pLayout->AddElement("BITANGENT",DXGI_FORMAT_R32G32B32_FLOAT);
	pLayout->AddElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT);
	pScene->m_VertexLayout = pLayout;

	pScene->m_VertexBuffer = pVB;	

	return pVB;
}

std::shared_ptr<ByteAddressBuffer> AssimpImporter::CreateIndexBuffer(ID3D12Device* pDevice, Scene* pScene)
{
	uint32_t ibSize = (uint32_t)m_BuffersData.indices.size();
	std::shared_ptr<ByteAddressBuffer> pIB = std::make_shared<ByteAddressBuffer>();
	std::wstring ibName = std::wstring(m_FileName.begin(), m_FileName.end());
	pIB->Create(pDevice, ibName + L"_IndexBuffer", ibSize / m_IndexStride, m_IndexStride, m_BuffersData.indices.data());
	
	pScene->m_IndexBuffer = pIB;

	return pIB;
}

std::shared_ptr<StructuredBuffer> AssimpImporter::CreateInstanceBuffer(ID3D12Device* pDevice, Scene* pScene, uint32_t drawCount)
{
	std::vector<uint16_t> drawIds;
	drawIds.resize(drawCount);
	for (uint16_t i = 0; i < drawCount; ++i)
		drawIds[i] = i;
	
	std::shared_ptr<StructuredBuffer> pInstanceBuffer = std::make_shared<StructuredBuffer>();
	std::wstring ibName = std::wstring(m_FileName.begin(), m_FileName.end());
	pInstanceBuffer->Create(pDevice, ibName + L"_InstanceBuffer", drawCount, sizeof(uint16_t), drawIds.data());

	// layout
	VertexBufferLayout::SharedPtr pLayout = VertexBufferLayout::Create();
	pLayout->AddElement("DRAWID", DXGI_FORMAT_R16_UINT, 0, 0, 1);
	pLayout->SetInputClass(InputType::PerInstanceData, 1);
	pScene->m_InstanceLayout = pLayout;

	pScene->m_InstanceBuffer = pInstanceBuffer;

	return pInstanceBuffer;
}

uint32_t AssimpImporter::CreateMeshData(Scene* pScene)
{
	// Meshes

	auto& meshData = pScene->m_MeshDescs;
	auto& instanceData = pScene->m_MeshInstanceData;

	uint32_t meshOffset = m_SceneMeshOffset, materialOffset = m_SceneMaterialOffset;
	auto newSize = meshOffset + m_Meshes.size();

	meshData.resize(newSize);
	pScene->m_MeshHasDynamicData.resize(newSize);

	size_t drawCount = 0;
	for (uint32_t meshIdx = 0, maxIdx = (uint32_t)m_Meshes.size(); meshIdx < maxIdx; ++meshIdx)
	{
		// mesh data
		const auto& curMesh = m_Meshes[meshIdx];

		auto meshId = (meshIdx + meshOffset);
		auto materialId = (curMesh.materialId + materialOffset);

		meshData[meshId].vertexOffset = curMesh.staticVertexOffset;
		meshData[meshId].vertexCount = curMesh.vertexCount;
		meshData[meshId].vertexStrideSize = sizeof(StaticVertexData);
		meshData[meshId].vertexByteSize = curMesh.vertexCount * sizeof(StaticVertexData);

		meshData[meshId].indexByteOffset = curMesh.indexOffset;
		meshData[meshId].indexCount = curMesh.indexCount;
		meshData[meshId].indexStrideSize = m_IndexStride;
		meshData[meshId].indexByteSize = curMesh.indexCount * m_IndexStride;

		meshData[meshId].materialID = materialId;

		drawCount += curMesh.instances.size();

		// mesh instance data
		for (const auto& instance : curMesh.instances)
		{
			instanceData.push_back(MeshInstanceData());
			auto& meshInstance = instanceData.back();
			meshInstance.globalMatrixID = instance;
			meshInstance.materialID = materialId;
			meshInstance.meshID = meshId;
		}

		if (curMesh.hasDynamicData)
		{
			pScene->m_MeshHasDynamicData[meshIdx] = true;
			for (uint32_t i = 0; i < curMesh.vertexCount; ++i)
			{
				m_BuffersData.dynamicData[curMesh.dynamicVertexOffset + i].globalMatrixID = (uint32_t)curMesh.instances[0];
			}
		}
	}

	return (uint32_t)drawCount;
}

void AssimpImporter::CreateGlobalMatricesBuffer(Scene* pScene)
{
	auto offset = pScene->m_SceneGraph.size();
	pScene->m_SceneGraph.resize(offset + m_SceneGraph.size());

	for (decltype(offset) i = 0, imax = m_SceneGraph.size(); i < imax; ++i)
	{
		auto &node = pScene->m_SceneGraph[i+offset];
		node.name = m_SceneGraph[i].name;
		node.parentIndex = m_SceneGraph[i].parentIndex;
		node.transform = m_SceneGraph[i].transform;
		node.localToBindSpace = m_SceneGraph[i].localToBindSpace;
	}
}

// calculate mesh bounding boxes
void AssimpImporter::CalculateMeshBoundingBoxes(Scene* pScene)
{
	auto offset = pScene->m_MeshBBs.size();
	pScene->m_MeshBBs.resize(m_Meshes.size() + offset);

	const float3 *vertex = nullptr;
	for (uint32_t i = 0, imax = (uint32_t)m_Meshes.size(); i < imax; ++i)
	{
		const auto& curMesh = m_Meshes[i];
		Vector3 boxMin(FLT_MAX), boxMax(FLT_MIN);

		const auto* pStaticData = &m_BuffersData.staticData[curMesh.staticVertexOffset];
		for (uint32_t v = 0; v < curMesh.vertexCount; ++v)
		{
			vertex = &(pStaticData[v].position);

			boxMin.x = MMATH::min(vertex->x, boxMin.x);
			boxMin.y = MMATH::min(vertex->y, boxMin.y);
			boxMin.z = MMATH::min(vertex->z, boxMin.z);

			boxMax.x = MMATH::max(vertex->x, boxMax.x);
			boxMax.y = MMATH::max(vertex->y, boxMax.y);
			boxMax.z = MMATH::max(vertex->z, boxMax.z);
		}
		pScene->m_MeshBBs[i+offset] = BoundingBox(boxMin, boxMax);
	}
}

template
std::unique_ptr<uint8_t[]> AssimpImporter::CreateIndexList<uint16_t>(const aiMesh* curMesh);

template
std::unique_ptr<uint8_t[]> AssimpImporter::CreateIndexList<uint32_t>(const aiMesh* curMesh);
