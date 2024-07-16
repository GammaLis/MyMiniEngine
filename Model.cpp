#include "Model.h"
#include "TextureManager.h"
#include "Graphics.h"
#include <DirectXPackedVector.h>

// assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#ifdef _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

namespace MyDirectX
{
	using namespace Math;
	using namespace DirectX::PackedVector;

	// Remove file extensions
	inline std::string ModifyFilePath(const char* str, const std::string &name)
	{
		if (*str == '\0')
			return std::string();
		
		constexpr uint32_t Size = 256;
		char path[Size];
		uint32_t i = 0;
		for (auto imax = name.size(); i < imax; ++i)
		{
			path[i] = name[i];
		}
		path[i++] = '/';
		path[i++] = '\0';

		// e.g. textures\\xxx.png
		const char* pStart = strrchr(str, '\\');
		if (pStart == nullptr)
			pStart = strrchr(str, '/');

		if (pStart == nullptr)
			pStart = str;
		else
			++pStart;

		strncat_s(path, pStart, Size - 1);
		
		// Loading DDS needs remove the file extension
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
		: m_Meshes(nullptr), m_Materials(nullptr),
		m_VertexData(nullptr), m_IndexData(nullptr),
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
		m_HitShaderMeshInfoBuffer.Destroy();

		m_Meshes = nullptr;
		m_Materials = nullptr;

		m_VertexData = nullptr;
		m_IndexData = nullptr;

		m_SRVs = nullptr;
	}

	void Model::Create(ID3D12Device* pDevice)
	{
		// vertex buffer & index buffer
		// _declspec(align(16)) - before VS 2015 or C++ 11
		alignas(16)	Vertex vertices[] =
		{
			Vertex{XMFLOAT3( .0f, +.6f, 0.f), XMCOLOR(1.0f ,0.0f, 0.0f, 1.0f), XMFLOAT2(0.5f, 0.0f)},
			Vertex{XMFLOAT3(-.6f, -.6f, 0.f), XMCOLOR(0.0f, 1.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f)},
			Vertex{XMFLOAT3(+.6f, -.6f, 0.f), XMCOLOR(0.0f, 0.0f, 1.0f, 1.0f), XMFLOAT2(1.0f, 1.0f)},
		};

		// _declspec(align(16)) 
		alignas(16) uint16_t indices[] = { 0, 1, 2 };

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
			// Vertex buffer & index buffer
			{
				m_VertexBuffer.Create(pDevice, L"VertexBuffer", m_VertexDataByteSize / m_VertexStride, m_VertexStride, m_VertexData.get());
				m_IndexBuffer.Create(pDevice, L"IndexBuffer", m_IndexDataByteSize / kIndexStride, kIndexStride, m_IndexData.get());

				m_VertexData = nullptr;
				m_IndexData = nullptr;
			}

			// Textures
			{
				// Load DDS textures default , need remove the file extension
				LoadTextures(pDevice);
				
				// Load textures using std_image (default png texture)
				// LoadTexturesBySTB_IMAGE(pDevice);
			}

			// RayTracing
			{
				InitializeRayTraceSceneInfo();
				m_HitShaderMeshInfoBuffer.Create(pDevice, L"RayTraceMeshInfo", (UINT)m_MeshInfoData.size(), sizeof(m_MeshInfoData[0]), m_MeshInfoData.data());
				m_MeshInfoSRV = m_HitShaderMeshInfoBuffer.GetSRV();
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
			aiProcess_CalcTangentSpace
			| aiProcess_FlipUVs
			| aiProcess_Triangulate
			// | aiProcess_JoinIdenticalVertices
			// | aiProcess_SortByPType
			);

		if (!scene)
		{
			Utility::Print(importer.GetErrorString());
			return false;
		}

		name = StringUtils::GetFileNameWithNoExtensions(fileName);

		// now we can access the file's contents
		// do the scene processing (scene)
		PrepareDataFromScene(scene);

		// we're done. everything will be cleaned up by the importer destructor
		return true;
	}

	bool IsCutoutMaterial(const std::string &strTexDiffusePath)
	{
		return strTexDiffusePath.find("thorn") != std::string::npos ||
			strTexDiffusePath.find("plant") != std::string::npos ||
			strTexDiffusePath.find("chain") != std::string::npos;
	}
	void Model::PrepareDataFromScene(const aiScene* scene)
	{
		if (scene->HasTextures())
		{
			// Embedded textures...
		}

		if (scene->HasAnimations())
		{
			// TODO...
		}

		// Materials
		{
			m_MaterialCount = scene->mNumMaterials;
			m_Materials.reset( new Material[m_MaterialCount] );
			m_MaterialIsCutout.resize(m_MaterialCount);
			memset(m_Materials.get(), 0, sizeof(Material) * m_MaterialCount);
			for (size_t matIdx = 0; matIdx < m_MaterialCount; ++matIdx)
			{
				const aiMaterial* srcMat = scene->mMaterials[matIdx];
				Material& dstMat = m_Materials[matIdx];

				// Constants
				aiColor3D diffuse(1.0f, 1.0f, 1.0f);
				aiColor3D specular(1.0f, 1.0f, 1.0f);
				aiColor3D ambient(1.0f, 1.0f, 1.0f);
				aiColor3D emissive(1.0f, 1.0f, 1.0f);
				aiColor3D transparent(1.0f, 1.0f, 1.0f);
				float opacity = 1.0f;
				float shininess = 0.0f;
				float specularStrength = 1.0f;

				// Textures
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

				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), texDiffusePath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), texSpecularPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), texNormalPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSIVE, 0), texEmissivePath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0), texLightmapPath);
				srcMat->Get(AI_MATKEY_TEXTURE(aiTextureType_REFLECTION, 0), texReflectionPath);

				dstMat.diffuse = Vector3(diffuse.r, diffuse.g, diffuse.b);
				dstMat.specular = Vector3(specular.r, specular.g, specular.b);
				dstMat.ambient = Vector3(ambient.r, ambient.g, ambient.b);
				dstMat.emissive = Vector3(emissive.r, emissive.g, emissive.b);
				dstMat.transparent = Vector3(transparent.r, transparent.g, transparent.b);
				dstMat.opacity = opacity;
				dstMat.shininess = shininess;
				dstMat.specularStrength = specularStrength;

				// texture path
				dstMat.texDiffusePath = ModifyFilePath(texDiffusePath.C_Str(), name);
				dstMat.texSpecularPath = ModifyFilePath(texSpecularPath.C_Str(), name);
				dstMat.texNormalPath = ModifyFilePath(texNormalPath.C_Str(), name);
				dstMat.texEmissivePath = ModifyFilePath(texEmissivePath.C_Str(), name);
				dstMat.texLightmapPath = ModifyFilePath(texLightmapPath.C_Str(), name);
				dstMat.texReflectionPath = ModifyFilePath(texReflectionPath.C_Str(), name);

				// is cutout ?
				m_MaterialIsCutout[matIdx] = IsCutoutMaterial(dstMat.texDiffusePath);

				aiString matName;
				srcMat->Get(AI_MATKEY_NAME, matName);
				dstMat.name = std::string(matName.C_Str());
			}
		}

		// Mesh
		m_MeshCount = scene->mNumMeshes;
		m_VertexDataByteSize = 0;
		m_IndexDataByteSize = 0;
		m_Meshes.reset(new Mesh[m_MeshCount]);
		memset(m_Meshes.get(), 0, sizeof(Mesh) * m_MeshCount);
		{
			// First pass, count everything
			for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
			{
				const aiMesh* srcMesh = scene->mMeshes[meshIndex];
				Mesh& dstMesh = m_Meshes[meshIndex];

				// Clear first
				dstMesh.attribsEnabled = 0;
				dstMesh.vertexStride = 0;

				ASSERT(srcMesh->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE);

				dstMesh.materialIndex = srcMesh->mMaterialIndex;

				// Just store everything as float. Can quantize in Model::optimize()
				// 0.Position
				// if (srcMesh->HasPositions())	// always true
				{
					dstMesh.attribsEnabled |= (unsigned int)AttribMask::attrib_mask_position;

					auto& attrib_pos = dstMesh.attrib[(unsigned int)Attrib::attrib_position];
					attrib_pos.offset = dstMesh.vertexStride;
					attrib_pos.normalized = 0;
					attrib_pos.components = 3;
					attrib_pos.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh.vertexStride += sizeof(float) * 3;
				}

				// 1.Texcoord0
				// if (srcMesh->HasTextureCoords(0))	// default
				{
					dstMesh.attribsEnabled |= (unsigned int)AttribMask::attrib_mask_texcoord0;

					auto& attrib_texcoord0 = dstMesh.attrib[(unsigned int)Attrib::attrib_texcoord0];
					attrib_texcoord0.offset = dstMesh.vertexStride;
					attrib_texcoord0.normalized = 0;
					attrib_texcoord0.components = 2;
					attrib_texcoord0.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh.vertexStride += sizeof(float) * 2;
				}

				// 2.Normal
				// if (srcMesh->HasNormals())	// default
				{
					dstMesh.attribsEnabled |= (unsigned int)AttribMask::attrib_mask_normal;

					auto& attrib_normal = dstMesh.attrib[(unsigned int)Attrib::attrib_normal];
					attrib_normal.offset = dstMesh.vertexStride;
					attrib_normal.normalized = 0;
					attrib_normal.components = 3;
					attrib_normal.format = (unsigned int)AttribFormat::attrib_format_float;
					dstMesh.vertexStride += sizeof(float) * 3;
				}

				// 3.Tangent & bitangent
				// if (srcMesh->HasTangentsAndBitangents())
				{
					// Tangent
					dstMesh.attribsEnabled |= (unsigned int)AttribMask::attrib_mask_tangent;
					{
						auto &attrib_tangent = dstMesh.attrib[(unsigned int)Attrib::attrib_tangent];
						attrib_tangent.offset = dstMesh.vertexStride;
						attrib_tangent.normalized = 0;
						attrib_tangent.components = 3;
						attrib_tangent.format = (unsigned int)AttribFormat::attrib_format_float;
					}
					dstMesh.vertexStride += sizeof(float) * 3;

					// Bitangent
					dstMesh.attribsEnabled |= (unsigned int)AttribMask::attrib_mask_bitangent;
					{
						auto& attrib_bitangent = dstMesh.attrib[(unsigned int)Attrib::attrib_bitangent];
						attrib_bitangent.offset = dstMesh.vertexStride;
						attrib_bitangent.normalized = 0;
						attrib_bitangent.components = 3;
						attrib_bitangent.format = (unsigned int)AttribFormat::attrib_format_float;
					}
					dstMesh.vertexStride += sizeof(float) * 3;
				}

				dstMesh.vertexDataByteOffset = m_VertexDataByteSize;
				dstMesh.vertexCount = srcMesh->mNumVertices;
				m_VertexDataByteSize += dstMesh.vertexStride * dstMesh.vertexCount;

				dstMesh.indexDataByteOffset = m_IndexDataByteSize;
				dstMesh.indexCount = srcMesh->mNumFaces * 3;
				m_IndexDataByteSize += kIndexStride * dstMesh.indexCount;

			}
		}

		{
			// All submeshes vertex strides are the same (They can be put in one VertexBuffer)
			m_VertexStride = m_Meshes[0].vertexStride;

			// Allocate storage
			/**
				https://docs.microsoft.com/en-us/cpp/cpp/align-cpp?view=vs-2019
				Note that ordinary allocators—for example, malloc, C++ operator new, and the Win32 allocators—
			return memory that is usually not sufficiently aligned for __declspec(align(#)) structures or arrays of
			structures.
			*/
			m_VertexData.reset( new uint8_t[m_VertexDataByteSize] );
			m_IndexData.reset( new uint8_t[m_IndexDataByteSize] );
			// new is memory aligned by default ???
			// (Note: C++17 supports void *operator new (std::size_t count, std::align_val al))
			// or
			// _aligned_malloc&_aligned_free, memory is aligned	-20-2-26
			// m_pVertexData = (unsigned char*)_aligned_malloc(m_VertexDataByteSize, 16);
			// m_pIndexData = (unsigned char*)_aligned_malloc(m_IndexDataByteSize, 16);
			
#if 0
			memset(m_VertexData.get(), 0, m_VertexDataByteSize);
			memset(m_IndexData.get(), 0, m_IndexDataByteSize);
#endif
			// 改用 SIMD指令
			// 好像不能使用new分配的内存，报错；采用_aligned_malloc没有问题	-20-2-26
			// __m128 val = _mm_set1_ps(0);
			// SIMDMemFill(m_pVertexData, val, Math::DivideByMultiple(m_VertexDataByteSize, 16));
			// SIMDMemFill(m_pIndexData, val, Math::DivideByMultiple(m_IndexDataByteSize, 16));

			// second pass, fill in vertex and index data
			for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
			{
				const aiMesh* srcMesh = scene->mMeshes[meshIndex];
				Mesh& dstMesh = m_Meshes[meshIndex];
				
				// 0.position
				if (srcMesh->HasPositions() && srcMesh->mVertices)	// HasPositions -> always true
				{
					auto *data = GetVertexData<Attrib::attrib_position>(dstMesh);
					for (unsigned int v = 0, vMax = dstMesh.vertexCount; v < vMax; ++v)
					{
						data[0] = srcMesh->mVertices[v].x;
						data[1] = srcMesh->mVertices[v].y;
						data[2] = srcMesh->mVertices[v].z;

						data = (float*)((uint8_t*)(data) + dstMesh.vertexStride);
					}
				}

				// 1.texcoord0
				if (srcMesh->HasTextureCoords(0) && srcMesh->mTextureCoords[0])
				{
					auto* data = GetVertexData<Attrib::attrib_texcoord0>(dstMesh);
					for (unsigned int v = 0, vMax = dstMesh.vertexCount; v < vMax; ++v)
					{
						data[0] = srcMesh->mTextureCoords[0][v].x;
						data[1] = srcMesh->mTextureCoords[0][v].y;

						data = (float*)((uint8_t*)(data) + dstMesh.vertexStride);
					}
				}

				// 2.normal
				if (srcMesh->HasNormals() && srcMesh->mNormals)
				{
					auto* data = GetVertexData<Attrib::attrib_normal>(dstMesh);
					for (unsigned int v = 0, vMax = dstMesh.vertexCount; v < vMax; ++v)
					{
						data[0] = srcMesh->mNormals[v].x;
						data[1] = srcMesh->mNormals[v].y;
						data[2] = srcMesh->mNormals[v].z;

						data = (float*)((uint8_t*)(data) + dstMesh.vertexStride);
					}
				}

				// 3.tangent & bitangent
				if (srcMesh->HasTangentsAndBitangents())
				{
					// tangent
					auto* data = GetVertexData<Attrib::attrib_tangent>(dstMesh);
					for (unsigned int v = 0, vMax = dstMesh.vertexCount; v < vMax; ++v)
					{
						if (srcMesh->mTangents)
						{
							data[0] = srcMesh->mTangents[v].x;
							data[1] = srcMesh->mTangents[v].y;
							data[2] = srcMesh->mTangents[v].z;
						}
						else
						{
							// TODO: generate tangents/bitangents if missing
							data[0] = 1.0f;
							data[1] = 0.0f;
							data[2] = 0.0f;
						}

						data = (float*)((uint8_t*)(data) + dstMesh.vertexStride);
					}

					// bitangent
					data = GetVertexData<Attrib::attrib_bitangent>(dstMesh);
					for (unsigned int v = 0, vMax = dstMesh.vertexCount; v < vMax; ++v)
					{
						if (srcMesh->mBitangents)
						{
							data[0] = srcMesh->mBitangents[v].x;
							data[1] = srcMesh->mBitangents[v].y;
							data[2] = srcMesh->mBitangents[v].z;
						}
						else
						{
							// TODO: generate tangents/bitangents if missing
							data[0] = 1.0f;
							data[1] = 0.0f;
							data[2] = 0.0f;
						}

						data = (float*)((uint8_t*)(data) + dstMesh.vertexStride);
					}
				}

				// index
				uint16_t* dstIndex = (uint16_t*)(m_IndexData.get() + dstMesh.indexDataByteOffset);
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
		const auto &mesh = m_Meshes[meshIndex];

		if (mesh.vertexCount > 0)
		{
			unsigned int vertexStride = mesh.vertexStride;

			const auto p = GetVertexData<Attrib::attrib_position>(mesh);
			const auto pEnd = GetVertexData<Attrib::attrib_position>(mesh, mesh.vertexCount);

			bbox.min = Vector3( FLT_MAX);
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
				const Mesh& mesh = m_Meshes[meshIndex];
				
				bbox.min = Min(bbox.min, mesh.boundingBox.min);
				bbox.max = Max(bbox.max, mesh.boundingBox.max);
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
			Mesh& mesh = m_Meshes[meshIndex];
			ComputeMeshBoundingBox(meshIndex, mesh.boundingBox);
		}
		ComputeGlobalBoundingBox(m_BoundingBox);
	}

	void Model::ReleaseTextures()
	{

	}

	void Model::LoadTextures(ID3D12Device* pDevice)
	{
		ReleaseTextures();

		m_SRVs.reset( new D3D12_CPU_DESCRIPTOR_HANDLE[m_MaterialCount * kTexturesPerMaterial] );

		const ManagedTexture* matTextures[6] = {};

		for (unsigned int materialIdx = 0; materialIdx < m_MaterialCount; ++materialIdx)
		{
			const Material& mat = m_Materials[materialIdx];

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

		m_SRVs.reset( new D3D12_CPU_DESCRIPTOR_HANDLE[m_MaterialCount * kTexturesPerMaterial] );

		const ManagedTexture* matTextures[kTexturesPerMaterial] = {};

		for (unsigned int materialIdx = 0; materialIdx < m_MaterialCount; ++materialIdx)
		{
			const Material& mat = m_Materials[materialIdx];

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

			m_SRVs[materialIdx * kTexturesPerMaterial + 0] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * kTexturesPerMaterial + 1] = matTextures[1]->GetSRV();
			m_SRVs[materialIdx * kTexturesPerMaterial + 2] = matTextures[2]->GetSRV();
			m_SRVs[materialIdx * kTexturesPerMaterial + 3] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * kTexturesPerMaterial + 4] = matTextures[0]->GetSRV();
			m_SRVs[materialIdx * kTexturesPerMaterial + 5] = matTextures[0]->GetSRV();
		}
	}

	void Model::InitializeRayTraceSceneInfo()
	{
		m_MeshInfoData.resize(m_MeshCount);
		for (unsigned int meshIndex = 0; meshIndex < m_MeshCount; ++meshIndex)
		{
			const Mesh &mesh = m_Meshes[meshIndex];
			auto& meshInfoData = m_MeshInfoData[meshIndex];

			meshInfoData.IndexOffsetBytes = mesh.indexDataByteOffset;
			meshInfoData.UVAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[(unsigned)Attrib::attrib_texcoord0].offset;
			meshInfoData.NormalAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[(unsigned)Attrib::attrib_normal].offset;
			meshInfoData.PositionAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[(unsigned)Attrib::attrib_position].offset;
			meshInfoData.TangentAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[(unsigned)Attrib::attrib_tangent].offset;
			meshInfoData.BitangentAttributeOffsetBytes = mesh.vertexDataByteOffset + mesh.attrib[(unsigned)Attrib::attrib_bitangent].offset;
			meshInfoData.AttributeStrideBytes = mesh.vertexStride;
			meshInfoData.MaterialInstanceId = mesh.materialIndex;
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
