#include "Model.h"
#include "TextureManager.h"
#include "Graphics.h"
#include <DirectXPackedVector.h>

// assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#ifdef _DEBUG
#pragma comment(lib, "assimp-vc142-mtd.lib")
#else
#pragma comment(lib, "assimp-vc142-mt.lib")
#endif

namespace MyDirectX
{
	using namespace Math;
	using namespace DirectX::PackedVector;

	// 去除扩展名
	inline std::string ModifyFilePath(const char* str)
	{
		if (*str == '\0')
			return std::string();

		const uint32_t Size = 256;
		char path[Size] = "Models/";

		// 类似 textures\\xxx.png
		char* pStart = strrchr((char*)str, '\\');
		if (pStart == nullptr)
			pStart = (char*)str;
		else
			++pStart;

		strncat_s(path, pStart, Size - 1);
		
		// 加载DDS图片需要
		// 删除文件扩展名
		char* pch = strrchr(path, '.');
		while (pch != nullptr && *pch != 0) *(pch++) = 0;

		return std::string(path);
	}

	/**
		XMCOLOR - ARGB Color; 8-8-8-8 bit unsigned normalized integer components packed into a 32 bit integer
		XMXDECN4 - 10-10-10-2 bit normalized components packed into a 32 bit integer
		XMFLOAT4 - DXGI_FORMAT_R32G32B32A32_FLOAT
	*/
	struct Vertex
	{
		XMFLOAT3 position;
		XMCOLOR color;
		XMFLOAT2 uv;
	};

	Model::Model()
		: m_pMesh(nullptr), m_pMaterial(nullptr),
		m_pVertexData(nullptr), m_pIndexData(nullptr),
		m_SRVs(nullptr)
	{
		Cleanup();
	}

	Model::~Model()
	{
		Cleanup();
	}

	void Model::Cleanup()
	{
		m_VertexBuffer.Destroy();
		m_IndexBuffer.Destroy();

		if (m_pMesh)
		{
			delete[] m_pMesh;
			m_pMesh = nullptr;
		}
		if (m_pMaterial)
		{
			delete[] m_pMaterial;
			m_pMaterial = nullptr;
		}
		if (m_pVertexData)
		{
			delete[] m_pVertexData;
			m_pVertexData = nullptr;
		}
		if (m_pIndexData)
		{
			delete[] m_pIndexData;
			m_pIndexData = nullptr;
		}
		if (m_SRVs)
		{
			delete[] m_SRVs;
			m_SRVs = nullptr;
		}
	}

	void Model::Create(ID3D12Device* pDevice)
	{
		// vertex buffer & index buffer
		Vertex vertices[] =
		{
			Vertex{XMFLOAT3( .0f, +.6f, 0.f), XMCOLOR(1.0f ,0.0f, 0.0f, 1.0f), XMFLOAT2(0.5f, 0.0f)},
			Vertex{XMFLOAT3(-.6f, -.6f, 0.f), XMCOLOR(0.0f, 1.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f)},
			Vertex{XMFLOAT3(+.6f, -.6f, 0.f), XMCOLOR(0.0f, 0.0f, 1.0f, 1.0f), XMFLOAT2(1.0f, 1.0f)},
		};

		uint16_t indices[] = { 0, 1, 2 };

		uint32_t vertexCount = _countof(vertices);
		uint32_t vertexByteSize = sizeof(vertices);
		uint32_t vertexStride = sizeof(Vertex);
		m_VertexStride = vertexStride;

		uint32_t indexCount = _countof(indices);
		uint32_t indexStride = sizeof(uint16_t);
		m_IndexCount = indexCount;

		m_VertexBuffer.Create(pDevice, L"VertexBuffer", vertexCount, vertexStride, &vertices);
		m_IndexBuffer.Create(pDevice, L"IndexBuffer", indexCount, indexStride, &indices);

		// textures
		const ManagedTexture* defaultTex = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, L"Default_Anim.PNG");
		if (defaultTex->IsValid())
		{
			m_DefaultSRV = defaultTex->GetSRV();
		}
	}

	void Model::CreateFromAssimp(ID3D12Device* pDevice, const std::string& fileName)
	{
		if (LoadFromAssimp(fileName))
		{
			// vertex buffer & index buffer
			{
				m_VertexBuffer.Create(pDevice, L"VertexBuffer", m_VertexDataByteSize / m_VertexStride, m_VertexStride, m_pVertexData);
				m_IndexBuffer.Create(pDevice, L"IndexBuffer", m_IndexDataByteSize / sizeof(uint16_t), sizeof(uint16_t), m_pIndexData);

				delete[] m_pVertexData;
				m_pVertexData = nullptr;

				delete[] m_pIndexData;
				m_pIndexData = nullptr;
			}

			// textures
			{
				// 默认加载DDS图片,需要删去图片路径扩展名
				LoadTextures(pDevice);
				
				// 采用stb_image加载图片（默认png格式） 
				// LoadTexturesBySTB_IMAGE(pDevice);
			}
		}
	}
	
	bool Model::LoadFromAssimp(const std::string& fileName)
	{
		// create an instance of the Importer class
		Assimp::Importer importer;

		// and have it read the given file with some example postprocessing
		// usually - if speed is not the most important aspect for you - you'll
		// probably to request more postprocessing than we do in this example.
		const aiScene* scene = importer.ReadFile(fileName,
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

		// now we can access the file's contents
		// do the scene processing (scene)
		PrepareDataFromScene(scene);

		// we're done. everything will be cleaned up by the importer destructor
		return true;
	}

	void Model::PrepareDataFromScene(const aiScene* scene)
	{
		if (scene->HasTextures())
		{
			// embedded textures...
		}

		if (scene->HasAnimations())
		{
			// to do
		}

		/**
			Material
		*/
		{
			m_MaterialCount = scene->mNumMaterials;
			m_pMaterial = new Material[m_MaterialCount];
			memset(m_pMaterial, 0, sizeof(Material) * m_MaterialCount);
			for (size_t matIdx = 0; matIdx < m_MaterialCount; ++matIdx)
			{
				const aiMaterial* srcMat = scene->mMaterials[matIdx];
				Material* dstMat = m_pMaterial + matIdx;

				// constants
				aiColor3D diffuse(1.0f, 1.0f, 1.0f);
				aiColor3D specular(1.0f, 1.0f, 1.0f);
				aiColor3D ambient(1.0f, 1.0f, 1.0f);
				aiColor3D emissive(1.0f, 1.0f, 1.0f);
				aiColor3D transparent(1.0f, 1.0f, 1.0f);
				float opacity = 1.0f;
				float shininess = 0.0f;
				float specularStrength = 1.0f;

				// textures
				aiString texDiffusePath;
				aiString texSpecularPath;
				aiString texNormalPath;
				aiString texEmissivePath;
				aiString texLightmapPath;
				aiString texReflectionPath;

				srcMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
				srcMat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
				srcMat->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
				srcMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
				srcMat->Get(AI_MATKEY_COLOR_TRANSPARENT, transparent);
				
				srcMat->Get(AI_MATKEY_OPACITY, opacity);
				srcMat->Get(AI_MATKEY_SHININESS, shininess);
				srcMat->Get(AI_MATKEY_SHININESS_STRENGTH, specularStrength);

				// my test
				float transparencyFactor = 0.0f;
				srcMat->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparencyFactor);

				// test end

				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), texDiffusePath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), texSpecularPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), texNormalPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSIVE, 0), texEmissivePath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0), texLightmapPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_REFLECTION, 0), texReflectionPath);

				dstMat->diffuse = Vector3(diffuse.r, diffuse.g, diffuse.b);
				dstMat->specular = Vector3(specular.r, specular.g, specular.b);
				dstMat->ambient = Vector3(ambient.r, ambient.g, ambient.b);
				dstMat->emissive = Vector3(emissive.r, emissive.g, emissive.b);
				dstMat->transparent = Vector3(transparent.r, transparent.g, transparent.b);
				dstMat->opacity = opacity;
				dstMat->shininess = shininess;
				dstMat->specularStrength = specularStrength;

				// texture path
				dstMat->texDiffusePath = ModifyFilePath(texDiffusePath.C_Str());
				dstMat->texSpecularPath = ModifyFilePath(texSpecularPath.C_Str());
				dstMat->texNormalPath = ModifyFilePath(texNormalPath.C_Str());
				dstMat->texEmissivePath = ModifyFilePath(texEmissivePath.C_Str());
				dstMat->texLightmapPath = ModifyFilePath(texLightmapPath.C_Str());
				dstMat->texReflectionPath = ModifyFilePath(texReflectionPath.C_Str());

				aiString matName;
				srcMat->Get(AI_MATKEY_NAME, matName);
				dstMat->name = std::string(matName.C_Str());
			}
		}

		/**
			Mesh
		*/
		m_MeshCount = scene->mNumMeshes;
		m_pMesh = new Mesh[m_MeshCount];
		memset(m_pMesh, 0, sizeof(Mesh) * m_MeshCount);
		{
			// first pass, count everything
			for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
			{
				const aiMesh* srcMesh = scene->mMeshes[meshIndex];
				Mesh* dstMesh = m_pMesh + meshIndex;

				assert(srcMesh->mPrimitiveTypes == aiPrimitiveType::aiPrimitiveType_TRIANGLE);

				dstMesh->materialIndex = srcMesh->mMaterialIndex;

				// just store everything as float. Can quantize in Model::optimize()
				// 0.position
				// if (srcMesh->HasPositions())	// always true
				{
					dstMesh->attribsEnabled |= (unsigned int)AttribMask::attrib_mask_position;

					auto& attrib_pos = dstMesh->attrib[(unsigned int)Attrib::attrib_position];
					attrib_pos.offset = dstMesh->vertexStride;
					attrib_pos.normalized = 0;
					attrib_pos.components = 3;
					attrib_pos.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh->vertexStride += sizeof(float) * 3;
				}

				// 1.texcoord0
				// if (srcMesh->HasTextureCoords(0))	// 默认含有，统一格式
				{
					dstMesh->attribsEnabled |= (unsigned int)AttribMask::attrib_mask_texcoord0;

					auto& attrib_texcoord0 = dstMesh->attrib[(unsigned int)Attrib::attrib_texcoord0];
					attrib_texcoord0.offset = dstMesh->vertexStride;
					attrib_texcoord0.normalized = 0;
					attrib_texcoord0.components = 2;
					attrib_texcoord0.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh->vertexStride += sizeof(float) * 2;
				}

				// 2.normal
				// if (srcMesh->HasNormals())	// 默认含有，统一格式
				{
					dstMesh->attribsEnabled |= (unsigned int)AttribMask::attrib_mask_normal;

					auto& attrib_normal = dstMesh->attrib[(unsigned int)Attrib::attrib_normal];
					attrib_normal.offset = dstMesh->vertexStride;
					attrib_normal.normalized = 0;
					attrib_normal.components = 3;
					attrib_normal.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh->vertexStride += sizeof(float) * 3;
				}

				// 3.tangent & bitangent
				// if (srcMesh->HasTangentsAndBitangents())
				{
					// tangent
					dstMesh->attribsEnabled |= (unsigned int)AttribMask::attrib_mask_tangent;
					
					auto &attrib_tangent = dstMesh->attrib[(unsigned int)Attrib::attrib_tangent];
					attrib_tangent.offset = dstMesh->vertexStride;
					attrib_tangent.normalized = 0;
					attrib_tangent.components = 3;
					attrib_tangent.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh->vertexStride += sizeof(float) * 3;

					// bitangent
					dstMesh->attribsEnabled |= (unsigned int)AttribMask::attrib_mask_bitangent;

					auto& attrib_bitangent = dstMesh->attrib[(unsigned int)Attrib::attrib_bitangent];
					attrib_bitangent.offset = dstMesh->vertexStride;
					attrib_bitangent.normalized = 0;
					attrib_bitangent.components = 3;
					attrib_bitangent.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh->vertexStride += sizeof(float) * 3;
				}

				dstMesh->vertexDataByteOffset = m_VertexDataByteSize;
				dstMesh->vertexCount = srcMesh->mNumVertices;
				m_VertexDataByteSize += dstMesh->vertexStride * dstMesh->vertexCount;

				dstMesh->indexDataByteOffset = m_IndexDataByteSize;
				dstMesh->indexCount = srcMesh->mNumFaces * 3;
				m_IndexDataByteSize += sizeof(uint16_t) * dstMesh->indexCount;
				
			}
		}

		{
			// 认为所有 submesh vertexstride 一致（这样才能放在一个VertexBuffer里）
			m_VertexStride = m_pMesh[0].vertexStride;

			// allocate storage
			m_pVertexData = new unsigned char[m_VertexDataByteSize];
			m_pIndexData = new unsigned char[m_IndexDataByteSize];
			
			// -mf
			memset(m_pVertexData, 0, m_VertexDataByteSize);
			memset(m_pIndexData, 0, m_IndexDataByteSize);

			// second pass, fill in vertex and index data
			for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
			{
				const aiMesh* srcMesh = scene->mMeshes[meshIndex];
				Mesh* dstMesh = m_pMesh + meshIndex;
				
				// 0.position
				if (srcMesh->HasPositions() && srcMesh->mVertices)	// HasPositions -> always true
				{
					float* dstPos = (float*)(m_pVertexData + dstMesh->vertexDataByteOffset +
						dstMesh->attrib[(unsigned int)Attrib::attrib_position].offset);
					for (unsigned int v = 0, vMax = dstMesh->vertexCount; v < vMax; ++v)
					{
						dstPos[0] = srcMesh->mVertices[v].x;
						dstPos[1] = srcMesh->mVertices[v].y;
						dstPos[2] = srcMesh->mVertices[v].z;

						dstPos = (float*)((unsigned char*)(dstPos) + dstMesh->vertexStride);
					}
				}

				// 1.texcoord0
				if (srcMesh->HasTextureCoords(0) && srcMesh->mTextureCoords[0])
				{
					float *dstTexcoord0 = (float*)(m_pVertexData + dstMesh->vertexDataByteOffset +
						dstMesh->attrib[(unsigned int)Attrib::attrib_texcoord0].offset);
					for (unsigned int v = 0, vMax = dstMesh->vertexCount; v < vMax; ++v)
					{
						dstTexcoord0[0] = srcMesh->mTextureCoords[0][v].x;
						dstTexcoord0[1] = srcMesh->mTextureCoords[0][v].y;

						dstTexcoord0 = (float*)((unsigned char*)(dstTexcoord0) + dstMesh->vertexStride);
					}
				}

				// 2.normal
				if (srcMesh->HasNormals() && srcMesh->mNormals)
				{
					float* dstNormal = (float*)(m_pVertexData + dstMesh->vertexDataByteOffset +
						dstMesh->attrib[(unsigned int)Attrib::attrib_normal].offset);
					for (unsigned int v = 0, vMax = dstMesh->vertexCount; v < vMax; ++v)
					{
						dstNormal[0] = srcMesh->mNormals[v].x;
						dstNormal[1] = srcMesh->mNormals[v].y;
						dstNormal[2] = srcMesh->mNormals[v].z;

						dstNormal = (float*)((unsigned char*)(dstNormal) + dstMesh->vertexStride);
					}
				}

				// 3.tangent & bitangent
				if (srcMesh->HasTangentsAndBitangents())
				{
					// tangent
					float* dstTangent = (float*)(m_pVertexData + dstMesh->vertexDataByteOffset +
						dstMesh->attrib[(unsigned int)Attrib::attrib_tangent].offset);
					for (unsigned int v = 0, vMax = dstMesh->vertexCount; v < vMax; ++v)
					{
						if (srcMesh->mTangents)
						{
							dstTangent[0] = srcMesh->mTangents[v].x;
							dstTangent[1] = srcMesh->mTangents[v].y;
							dstTangent[2] = srcMesh->mTangents[v].z;
						}
						else
						{
							// TO DO: generate tangents/bitangents if missing
							dstTangent[0] = 1.0f;
							dstTangent[1] = 0.0f;
							dstTangent[2] = 0.0f;
						}

						dstTangent = (float*)((unsigned char*)(dstTangent) + dstMesh->vertexStride);
					}

					// bitangent
					float* dstBitangent = (float*)(m_pVertexData + dstMesh->vertexDataByteOffset +
						dstMesh->attrib[(unsigned int)Attrib::attrib_bitangent].offset);
					for (unsigned int v = 0, vMax = dstMesh->vertexCount; v < vMax; ++v)
					{
						if (srcMesh->mBitangents)
						{
							dstBitangent[0] = srcMesh->mBitangents[v].x;
							dstBitangent[1] = srcMesh->mBitangents[v].y;
							dstBitangent[2] = srcMesh->mBitangents[v].z;
						}
						else
						{
							// TO DO: generate tangents/bitangents if missing
							dstBitangent[0] = 1.0f;
							dstBitangent[1] = 0.0f;
							dstBitangent[2] = 0.0f;
						}

						dstBitangent = (float*)((unsigned char*)(dstBitangent) + dstMesh->vertexStride);
					}
				}

				// index
				uint16_t* dstIndex = (uint16_t*)(m_pIndexData + dstMesh->indexDataByteOffset);
				for (unsigned int f = 0, fMax = srcMesh->mNumFaces; f < fMax; ++f)
				{
					assert(srcMesh->mFaces[f].mNumIndices == 3);

					*dstIndex++ = srcMesh->mFaces[f].mIndices[0];
					*dstIndex++ = srcMesh->mFaces[f].mIndices[1];
					*dstIndex++ = srcMesh->mFaces[f].mIndices[2];
				}
			}
		}

		ComputeAllBoundingBoxes();
	}

	// assuming at least 3 floats for position
	void Model::ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox& bbox) const
	{
		const Mesh* mesh = m_pMesh + meshIndex;

		if (mesh->vertexCount > 0)
		{
			unsigned int vertexStride = mesh->vertexStride;

			const float* p = (float*)(m_pVertexData + mesh->vertexDataByteOffset +
				mesh->attrib[(unsigned int)Attrib::attrib_position].offset);
			const float* pEnd = (float*)(m_pVertexData + mesh->vertexDataByteOffset +
				mesh->vertexCount * mesh->vertexStride + mesh->attrib[(unsigned int)Attrib::attrib_position].offset);

			bbox.min = Vector3(FLT_MAX);
			bbox.max = Vector3(-FLT_MAX);

			while (p < pEnd)
			{
				Vector3 pos(*(p + 0), *(p + 1), *(p + 2));

				bbox.min = Min(bbox.min, pos);
				bbox.max = Max(bbox.max, pos);

				(*(uint8_t**)&p) += vertexStride;
			}
		}
		else
		{
			bbox.min = Vector3(0.0f);
			bbox.max = Vector3(0.0f);
		}
	}

	void Model::ComputeGlobalBoundingBox(BoundingBox& bbox) const
	{
		if (m_MeshCount > 0)
		{
			bbox.min = Vector3(FLT_MAX);
			bbox.max = Vector3(-FLT_MAX);
			for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
			{
				const Mesh* mesh = m_pMesh + meshIndex;
				
				bbox.min = Min(bbox.min, mesh->boundingBox.min);
				bbox.max = Max(bbox.max, mesh->boundingBox.max);
			}
		}
		else
		{
			bbox.min = Vector3(0.0f);
			bbox.max = Vector3(0.0f);
		}
	}

	void Model::ComputeAllBoundingBoxes()
	{
		for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
		{
			Mesh* mesh = m_pMesh + meshIndex;
			ComputeMeshBoundingBox(meshIndex, mesh->boundingBox);
		}
		ComputeGlobalBoundingBox(m_BoundingBox);
	}

	void Model::ReleaseTextures()
	{

	}

	void Model::LoadTextures(ID3D12Device* pDevice)
	{
		ReleaseTextures();

		m_SRVs = new D3D12_CPU_DESCRIPTOR_HANDLE[m_MaterialCount * 6];

		const ManagedTexture* matTextures[6] = {};

		for (unsigned int materialIdx = 0; materialIdx < m_MaterialCount; ++materialIdx)
		{
			const Material& mat = m_pMaterial[materialIdx];

			// load diffuse
			matTextures[0] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texDiffusePath, true);
			if (!matTextures[0]->IsValid())
			{
				matTextures[0] = Graphics::s_TextureManager.LoadFromFile(pDevice, "default", true);
			}

			// load specular
			matTextures[1] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texSpecularPath, true);
			if (!matTextures[1]->IsValid())
			{
				matTextures[1] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texDiffusePath + "_specular", true);
				if (!matTextures[1]->IsValid())
					matTextures[1] = Graphics::s_TextureManager.LoadFromFile(pDevice, "default_specular", true);
			}

			// load normal
			matTextures[2] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texNormalPath, false);
			if (!matTextures[2]->IsValid())
			{
				matTextures[2] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texDiffusePath + "_normal", false);
				if (!matTextures[2]->IsValid())
					matTextures[2] = Graphics::s_TextureManager.LoadFromFile(pDevice, "default_normal", false);
			}

			// load emissive
			// matTextures[3] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texEmissivePath, true);

			// load lightmap
			// matTextures[4] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texLightmapPath, true);

			// load reflection
			// matTextures[5] = Graphics::s_TextureManager.LoadFromFile(pDevice, mat.texReflectionPath, true);

			m_SRVs[materialIdx * 6 + 0] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 1] = matTextures[1]->GetSRV();
			m_SRVs[materialIdx * 6 + 2] = matTextures[2]->GetSRV();
			m_SRVs[materialIdx * 6 + 3] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 4] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 5] = matTextures[0]->GetSRV();
		}
	}
	void Model::LoadTexturesBySTB_IMAGE(ID3D12Device* pDevice)
	{
		ReleaseTextures();

		m_SRVs = new D3D12_CPU_DESCRIPTOR_HANDLE[m_MaterialCount * 6];

		const ManagedTexture* matTextures[6] = {};

		for (unsigned int materialIdx = 0; materialIdx < m_MaterialCount; ++materialIdx)
		{
			const Material& mat = m_pMaterial[materialIdx];

			// load diffuse
			bool bValid = !mat.texDiffusePath.empty();
			if (bValid)
			{
				matTextures[0] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texDiffusePath, true);
				bValid = matTextures[0]->IsValid();
			}
			if (!bValid)
			{
				matTextures[0] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, "default.PNG", true);
			}

			// load specular
			bValid = !mat.texDiffusePath.empty();
			if (bValid)
			{
				matTextures[1] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texSpecularPath, true);
				bValid = matTextures[1]->IsValid();
			}
			if (!bValid)
			{
				matTextures[1] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, "default_specular.PNG", true);
			}

			// load normal
			bValid = !mat.texNormalPath.empty();
			if (bValid)
			{
				matTextures[2] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texNormalPath, false);
				bValid = matTextures[2]->IsValid();
			}
			if (!bValid)
			{
				matTextures[2] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, "default_normal.PNG", false);
			}

			// load emissive
			// matTextures[3] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texEmissivePath, true);

			// load lightmap
			// matTextures[4] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texLightmapPath, true);

			// load reflection
			// matTextures[5] = Graphics::s_TextureManager.LoadBySTB_IMAGE(pDevice, mat.texReflectionPath, true);

			m_SRVs[materialIdx * 6 + 0] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 1] = matTextures[1]->GetSRV();
			m_SRVs[materialIdx * 6 + 2] = matTextures[2]->GetSRV();
			m_SRVs[materialIdx * 6 + 3] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 4] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * 6 + 5] = matTextures[0]->GetSRV();
		}
	}
}

/**
	>> Assimp Data Structures
	the assimp library returns the imported data in a collection of structures. aiScene forms
the root of the data, from here you gain access to all the nodes, meshes, materials, animations
or textures that were read from the imported file. 
	by default, all 3D data is provided in a right-handed coordinate system such as OpenGL uses.
If you need the imported data to be in a left-handed coordinate system, supply the #aiProcess_MakeLeftHanded
flag to the ReadFile() function call.
	
	the output face winding is counter clockwise. Use #aiProcess_FlipWindingOrder to get CW data.

	the output UV coordinate system has its origin in the lower-left corder:
	(0, 1)	- (1, 1)
	(0, 0)	- (1, 0)
	use teh #aiProcess_FlipUVs flag to get UV coordinates with the upper-left corner as origin.

	>> the Node-Hierarchy
	nodes are little named entities in the scene that have a place and orientation relative
to their parents. Starting from the scene's root node all nodes can have 0 to x child nodes,
thus forming a hierarchy. They form the base on which the scene is built on: a node can refer to
0..x meshes, can be referred to by a bone of a mesh or can be animated by a key sequence of 
an animation. DirectX calls them "frames", others call them ""objects, we call them aiNode.
	a node can potentially refer to single or multiple meshes. The meshes are not stored inside
the node, but instead in an array of aiMesh inside the aiScene. A node only refers to them 
by their array index. This also means that multiple nodes can refer to the same mesh, which
provides a simple form of instancing. A mesh referred to by this way lives in the node's local
coordinate system. If you want the mesh's orientation in global space, you'd have to concatenate（连接）
the transformations from the referring node and all of its parents.

	>> Meshes
	all meshes of an imported scene are stored in an array of aiMesh* inside the aiScene. Nodes refer to them
by their index in the array and providing the coordinate system for them, too. One mesh uses only a single
material everywhere - if parts of the model use a different material, this part is moved to a separate mesh
at the same node. The mesh refers to its material in the same way as the node refers to its meshes: materials
are stored in an array inside the aiScene, the mesh stores only an index into this array.
	an aiMesh is defined by a series of data channels. The presence of these data channels is defined by
the contents of the imported file: by default there are only those data channels present in the mesh that
were also found in the file. The only channels guaranteed to be always present are aiMesh::mVertices and 
aiMesh::mFaces. 
	at the moment, a single aiMesh may contain a set of triangles and polygons. A single vertex does always
have a position. In addition it may have one normal, one tangent and bitangent, zero to AI_MAX_NUMBER_OF_TEXTURECOORDS
(4 at the moment) texture coords and zero to AI_MAX_NUMBER_OF_COLOR_SETS(4) vertex colors. In addition a mesh
may or may not have a set of bones described by an array of aiBone structures.

	>> Bones
	a mesh may have a set of bones in the form of aiBone objects. Bones are a means to deform a mesh according
to the movement of a skeleton. Each bone has a name and a set of vertices on which it has influence. Its offset 
matrix declares the transformation needed to transform from mesh space to the local space of this bone.
	Using the bones name you can find the corresponding node in the node hierarchy. This node in relation to 
the other bones' nodes defines teh skeleton of the mesh.
	
	>> Textures
	normally textures used by assets are stored in separate files, however, there are file formats embedding
their textures directly into the model file. Such textures are loaded into an aiTexture structure.


	...
	>> Material- System
	all materials are stored in an array of aiMaterial inside the aiScene.
	each aiMesh refers to one material by its index in the array. Due to the vastly diverging definitions and 
usages of material parameters there is no hard definition of a material structure. Instead a material is defined
by a set of properties accessible by their names.
	
	-- Textuers
	textures are organized in stacks, each stack being evaluated independently. The final color value from a particular
texture stack is used in the shading equation. For example, the computed color value of the diffuse texture stack
(aiTextureType_DIFFUSE) is multiplied with the amount of incoming diffuse light to obtain the final diffuse color
of a pixel.
	-- Constants
	all material key constants start with "AI_MATKEY" as a prefix
	COLOR_DIFFUSE, COLOR_SPECULAR, COLOR_AMBIENT, COLOR_EMISSIVE, COLOR_TRANSPARENT, COLOR_REFLECTIVE,

	C++ API
	retrieving a property from a material is done using various utility functions. For C++ it's simply 
calling aiMaterial::Get()
	aiColor3D color;
	mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);

*/
