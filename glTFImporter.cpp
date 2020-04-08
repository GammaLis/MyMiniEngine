#include "glTFImporter.h"
#include <iostream>
#include <fstream>
#include <regex>
#include <cassert>
#include <stack>
#include <deque>
#include <algorithm>

#include "Graphics.h"
#include "FileUtility.h"
#include "TextureManager.h"

#define MATRIX_SIZE 16

namespace glTF
{
	using namespace rapidjson;

	static int GetComponentSizeInBytes(glDataType componentType)
	{
		switch (componentType)
		{
		case glDataType::BYTE:
			return 1;
		case glDataType::UNSIGNED_BYTE:
			return 1;
		case glDataType::SHORT:
			return 2;
		case glDataType::UNSIGNED_SHORT:
			return 2;
		case glDataType::INT:
			return 4;
		case glDataType::UNSIGNED_INT:
			return 4;
		case glDataType::FLOAT:
			return 4;
		case glDataType::DOUBLE:
			return 8;
		default:
			return -1;
		}
	}

	static int GetNumComponentsInType(glType type)
	{
		switch (type)
		{
		case glType::SCALAR:
			return 1;
		case glType::VEC2:
			return 2;
		case glType::VEC3:
			return 3;
		case glType::VEC4:
			return 4;
		case glType::MAT2:
			return 4;
		case glType::MAT3:
			return 9;
		case glType::MAT4:
			return 16;
		default:
			return -1;
		}
	}

#if defined(GLMath)
	static void Mat2TRS(const Matrix4x4& mat, Vector3& translate, Quaternion& rotation, Vector3& scale)
	{
		// T
		translate = glm::column(mat, 3);
		// S
		Matrix3x3 mat3 = Matrix3x3(mat);
		Matrix3x3 s2 = (glm::transpose(mat3) * mat3);
		scale.x = sqrt(s2[0][0]);
		scale.y = sqrt(s2[1][1]);
		scale.z = sqrt(s2[2][2]);
		// R
		s2 = Matrix3x3(
			1.0f / scale.x, 0.0f, 0.0f,
			0.0f, 1.0f / scale.y, 0.0f,
			0.0f, 0.0f, 1.0f / scale.z );
		mat3 *= s2;
		rotation = Quaternion(mat3);
	}

	static void TRS2Mat(Matrix4x4& mat, const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
	{
		Matrix3x3 R = Matrix3x3(rotation);
		Matrix3x3 S = Matrix3x3(
			scale.x, 0.0f, 0.0f,
			0.0f, scale.y, 0.0f,
			0.0f, 0.0f, scale.z
		);
		Matrix3x3 mm = R * S;
		mat = mm;
		mat[3] = Vector4(translation, 1.0f);
	}
#elif defined(DXMath)
	static void Mat2TRS(const Matrix4x4& mat, Vector3& translate, Quaternion& rotation, Vector3& scale)
	{
		// T
		translate = glm::column(mat, 3);
		// S
		Matrix3x3 mat3 = Matrix3x3(mat);
		Matrix3x3 s2 = (glm::transpose(mat3) * mat3);
		scale.x = sqrt(s2[0][0]);
		scale.y = sqrt(s2[1][1]);
		scale.z = sqrt(s2[2][2]);
		// R
		s2 = Matrix3x3(
			1.0f / scale.x, 0.0f, 0.0f,
			0.0f, 1.0f / scale.y, 0.0f,
			0.0f, 0.0f, 1.0f / scale.z);
		mat3 *= s2;
		rotation = Quaternion(mat3);
	}

	static void TRS2Mat(Matrix4x4& mat, const Vector3& translate, const Quaternion& rotation, const Vector3& scale)
	{
		Matrix3x3 R = Matrix3x3(rotation);
		Matrix3x3 S = Matrix3x3(
			scale.x, 0.0f, 0.0f,
			0.0f, scale.y, 0.0f,
			0.0f, 0.0f, scale.z
		);
		Matrix3x3 mm = R * S;
		mat = mm;
		mat[3] = Vector4(translate, 1.0f);
	}
#endif

	static glType Str2GLType(const std::string& strType, const std::string &errorInfo = "")
	{
		if (strType == "SCALAR")
			return glType::SCALAR;
		else if (strType == "VEC2")
			return glType::VEC2;
		else if (strType == "VEC3")
			return glType::VEC3;
		else if (strType == "VEC4")
			return glType::VEC4;
		else if (strType == "MAT2")
			return glType::MAT2;
		else if (strType == "MAT3")
			return glType::MAT3;
		else if (strType == "MAT4")
			return glType::MAT4;
		else
		{
			std::cout << "Unsupported type for accessor object. Got \"" << errorInfo <<  strType << "\"" << std::endl;
			return glType::UNKNOWN;
		}
	}

	static glAlphaMode Str2GLAlphaMode(const std::string &strAlphaMode, const std::string& errorInfo = "")
	{
		if (strAlphaMode == "OPAQUE")
			return glAlphaMode::kOPAQUE;
		else if (strAlphaMode == "MASK")
			return glAlphaMode::kMASK;
		else if (strAlphaMode == "BLEND")
			return glAlphaMode::kBLEND;
		else
		{
			std::cout << "Unsupported alpha mode \"" << errorInfo << strAlphaMode << "\"" << std::endl;
			return glAlphaMode::kOPAQUE;
		}
	}

	static CameraType Str2CameraType(const std::string& type)
	{
		if (type == "perspective")
			return CameraType::Perspective;
		else if (type == "orthographic")
			return CameraType::Orthographics;
		else
		{
			std::cout << "Unknown camera type \"" << type << "\". Set to default perspective." << std::endl;
			return CameraType::Perspective;
		}
	}

	// 获取float value（1 - 视作int，无法直接GetFloat()获取）
	static bool GetNumberValue(float &f, const Value& val, const char *name = "")
	{
		bool bValid = false;
		if (val.IsFloat())
		{
			f = val.GetFloat();
			bValid = true;
		}
		else if (val.IsInt())
		{
			f = (float)val.GetInt();
			bValid = true;
		}
		else if (val.HasMember(name))
		{
			if (val[name].IsFloat())
			{
				f = val[name].GetFloat();
				bValid = true;
			}
			else if (val[name].IsInt())
			{
				f = (float)val[name].GetInt();
				bValid = true;
			}
		}
		return bValid;
	}

	/// glTFImporter
	//
	glTFImporter::glTFImporter()
	{
	}

	glTFImporter::glTFImporter(const std::string & glTFFilePath)
	{
		Load(glTFFilePath);
	}

	bool glTFImporter::Load(const std::string & glTFFilePath)
	{
		m_FileDir = GetBaseDir(glTFFilePath);
		m_FileName = GetFileNameWithNoExtensions(glTFFilePath);

		std::regex reg(".gltf$", std::regex_constants::icase);
		bool bValid = std::regex_search(glTFFilePath, reg);	// regex_match - 全词匹配	regex_search - 部分匹配
		if (!bValid)
		{
			std::cout << "File format is not gltf" << std::endl;
			return false;
		}

		std::ifstream ifs(glTFFilePath);
		if (!ifs.is_open())
		{
			std::cout << "Failed to open file " << glTFFilePath << std::endl;
			return false;
		}

		m_glTFJson = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		rapidjson::Document dom;
		if (dom.Parse(m_glTFJson.c_str()).HasParseError())
		{
			std::cout << "Parse glTF file error" << dom.GetParseError() << std::endl;
			return false;
		}

		Parse(dom);

		return true;
	}

	bool glTFImporter::Create(ID3D12Device* pDevice)
	{
		m_pDevice = pDevice;
		bool bValid = BuildScenes();
		if (bValid)
		{
			m_VertexBuffer.Create(pDevice, L"VertexBuffer", m_VertexByteLength / m_VertexStride, m_VertexStride, m_VertexData.get());
			m_IndexBuffer.Create(pDevice, L"IndexBuffer", m_IndexByteLength / sizeof(uint16_t), sizeof(uint16_t), m_IndexData.get());

			m_VertexData.reset();
			m_IndexData.reset();

			m_BinData.clear();
		}
		else
		{
			Utility::Print("Create Model Vertex&Index Buffer Failed!");
		}

		return bValid;
	}

	void glTFImporter::Clear()
	{
		m_BinData.clear();

		m_VertexData.reset();
		m_IndexData.reset();

		m_VertexBuffer.Destroy();
		m_IndexBuffer.Destroy();
	}

	Matrix4x4 glTFImporter::GetMeshTransform(const Mesh& mesh) const
	{
		if (mesh.nodeIndex >= 0 && mesh.nodeIndex < m_Nodes.size())
		{
			const auto& curNode = m_Nodes[mesh.nodeIndex];
			return curNode.parentTransCache * curNode.transform;
		}
		else
		{
			Utility::Print("Mesh's node index is invalid!");
			return Matrix4x4();
		}		
	}

	void glTFImporter::Parse(const rapidjson::Document& dom)
	{
		// scenes
		{
			if (dom.HasMember("scenes") && dom["scenes"].IsArray())
			{
				const Value& scenes = dom["scenes"];
				for (unsigned i = 0, imax = scenes.Size(); i < imax; ++i)
				{
					glScene newScene;
					
					const Value& curScene = scenes[i];
					if (curScene.HasMember("name") && curScene["name"].IsString())
						newScene.name = curScene["name"].GetString();
					if (curScene.HasMember("nodes") && curScene["nodes"].IsArray())
					{
						const Value& nodes = curScene["nodes"];
						for (unsigned j = 0, jmax = nodes.Size(); j < jmax; ++j)
						{
							if (nodes[j].IsInt())
								newScene.nodes.emplace_back(nodes[j].GetInt());
						}
					}
					m_Scenes.emplace_back(newScene);
				}
			}

			if (dom.HasMember("scene") && dom["scene"].IsInt())
				m_DefaultScene = dom["scene"].GetInt();
		}

		// nodes
		{
			if (dom.HasMember("nodes") && dom["nodes"].IsArray())
			{
				const Value& nodes = dom["nodes"];
				for (int i = 0, imax = nodes.Size(); i < imax; ++i)
				{
					glNode newNode;

					// name
					const Value& curNode = nodes[i];
					if (curNode.HasMember("name") && curNode["name"].IsString())
						newNode.name = curNode["name"].GetString();
					// children
					if (curNode.HasMember("children") && curNode["children"].IsArray())
					{
						const Value& children = curNode["children"];
						for (int j = 0, jmax = children.Size(); j < jmax; ++j) 
						{
							if (children[j].IsInt())
								newNode.children.emplace_back(children[j].GetInt());
						}
					}
					// transform
					// matrix
					if (curNode.HasMember("matrix") && curNode["matrix"].IsArray())
					{
						const Value& matrix = curNode["matrix"];
						assert(matrix.Size() == MATRIX_SIZE);
						float arr[MATRIX_SIZE] = { 0 };
						for (int j = 0, jmax = matrix.Size(); j < jmax; ++j)
						{
							GetNumberValue(arr[j], matrix[j]);
						}

						Matrix4x4 mat(
							arr[0],	 arr[1],  arr[2],  arr[3],
							arr[4],	 arr[5],  arr[6],  arr[7],
							arr[8],	 arr[9],  arr[10], arr[11],
							arr[12], arr[13], arr[14], arr[15]);
						newNode.transform = mat;

						Mat2TRS(mat, newNode.translation, newNode.rotation, newNode.scale);
					}
					else	// T R S
					{
						bool bNewTrans = false;
						if (curNode.HasMember("translation") && curNode["translation"].IsArray())
						{
							const Value& translation = curNode["translation"];
							float arr[3] = { 0 };
							for (int j = 0; j < 3; ++j)
							{
								GetNumberValue(arr[j], translation[j]);
							}
							newNode.translation = Vector3(arr[0], arr[1], arr[2]);
							bNewTrans = true;
						}
						if (curNode.HasMember("rotation") && curNode["rotation"].IsArray())
						{
							const Value& rotation = curNode["rotation"];
							float arr[4] = { 0 };
							for (int j = 0; j < 4; ++j)
							{
								GetNumberValue(arr[j], rotation[j]);
							}
							// quaternion - (w, x, y, z)
							newNode.rotation = Quaternion(arr[3], arr[0], arr[1], arr[2]);
							bNewTrans = true;
						}
						if (curNode.HasMember("scale") && curNode["scale"].IsArray())
						{
							const Value& scale = curNode["scale"];
							float arr[3] = { 0 };
							for (int j = 0; j < 3; ++j)
							{
								GetNumberValue(arr[j], scale[j]);
							}
							newNode.scale = Vector3(arr[0], arr[1], arr[2]);
							bNewTrans = true;
						}
						if (bNewTrans)
							TRS2Mat(newNode.transform, newNode.translation, newNode.rotation, newNode.scale);
					}

					// mesh
					if (curNode.HasMember("mesh") && curNode["mesh"].IsInt())
						newNode.meshIdx = curNode["mesh"].GetInt();

					// camera
					if (curNode.HasMember("camera") && curNode["camera"].IsInt())
						newNode.cameraIdx = curNode["camera"].GetInt();

					// skin
					if (curNode.HasMember("skin") && curNode["skin"].IsInt())
						newNode.skinIdx = curNode["skin"].GetInt();

					// weights
					if (curNode.HasMember("weights") && curNode["weights"].IsArray())
					{
						const Value& weights = curNode["weights"];
						for (int j = 0, jmax = weights.Size(); j < jmax; ++j)
						{
							float weight;
							GetNumberValue(weight, weights[j]);
							
							newNode.weights.emplace_back(weight);
						}
					}
					
					m_Nodes.emplace_back(newNode);
				}
			}
		}

		// meshes
		{
			if (dom.HasMember("meshes") && dom["meshes"].IsArray())
			{
				const Value& meshes = dom["meshes"];
				for (int i = 0, imax = meshes.Size(); i < imax; ++i)
				{
					glMesh newMesh;

					const Value& curMesh = meshes[i];
					if (curMesh.HasMember("name") && curMesh["name"].IsString())
						newMesh.name = curMesh["name"].GetString();

					// primitives
					if (curMesh.HasMember("primitives") && curMesh["primitives"].IsArray())
					{
						const Value& primitives = curMesh["primitives"];
						for (int j = 0, jmax = primitives.Size(); j < jmax; ++j)
						{
							glPrimitive newPrimitive;

							const Value& curPrim = primitives[j];
							// attributes
							if (curPrim.HasMember("attributes"))
							{
								const Value& attributes = curPrim["attributes"];

								// POSITION
								if (attributes.HasMember("POSITION") && attributes["POSITION"].IsInt())
								{
									auto& attrib_pos = newPrimitive.attributes[Attrib::attrib_position];
									attrib_pos.accessor = attributes["POSITION"].GetInt();
									attrib_pos.semanticName = "POSITION";
									attrib_pos.semanticIndex = 0;
								}
								// TEXCOORD_0
								if (attributes.HasMember("TEXCOORD_0") && attributes["TEXCOORD_0"].IsInt())
								{
									auto& attrib_uv0 = newPrimitive.attributes[Attrib::attrib_texcoord0];
									attrib_uv0.accessor = attributes["TEXCOORD_0"].GetInt();
									attrib_uv0.semanticName = "TEXCOORD";
									attrib_uv0.semanticIndex = 0;
								}
								// TEXCOORD_1
								if (attributes.HasMember("TEXCOORD_1") && attributes["TEXCOORD_1"].IsInt())
								{
									auto& attrib_uv1 = newPrimitive.attributes[Attrib::attrib_texcoord1];
									attrib_uv1.accessor = attributes["TEXCOORD_1"].GetInt();
									attrib_uv1.semanticName = "TEXCOORD";
									attrib_uv1.semanticIndex = 1;
								}
								// NORMAL
								if (attributes.HasMember("NORMAL") && attributes["NORMAL"].IsInt())
								{
									auto& attrib_normal = newPrimitive.attributes[Attrib::attrib_normal];
									attrib_normal.accessor = attributes["NORMAL"].GetInt();
									attrib_normal.semanticName = "NORMAL";
									attrib_normal.semanticIndex = 0;
								}
								// TANGENT
								if (attributes.HasMember("TANGENT") && attributes["TANGENT"].IsInt())
								{
									auto& attrib_tangent = newPrimitive.attributes[Attrib::attrib_tangent];
									attrib_tangent.accessor = attributes["TANGENT"].GetInt();
									attrib_tangent.semanticName = "TANGENT";
									attrib_tangent.semanticIndex = 0;
								}
								// COLOR_0
								if (attributes.HasMember("COLOR_0") && attributes["COLOR_0"].IsInt())
								{
									auto& attrib_color0 = newPrimitive.attributes[Attrib::attrib_color0];
									attrib_color0.accessor = attributes["COLOR_0"].GetInt();
									attrib_color0.semanticName = "COLOR";
									attrib_color0.semanticIndex = 0;
								}
								// JOINTS_0

								// WEIGHTS_0
								// ...
							}

							// indices
							if (curPrim.HasMember("indices") && curPrim["indices"].IsInt())
								newPrimitive.indexAccessor = curPrim["indices"].GetInt();

							// material
							if (curPrim.HasMember("material") && curPrim["material"].IsInt())
								newPrimitive.materialIdx = curPrim["material"].GetInt();

							// mode
							if (curPrim.HasMember("mode") && curPrim["mode"].IsInt())
								newPrimitive.mode = (glTopology)curPrim["mode"].GetInt();

							// targets
							// ..

							newMesh.primitives.emplace_back(newPrimitive);
						}
					}
					m_Meshes.emplace_back(newMesh);
				}
			}
		}

		// cameras
		if (dom.HasMember("cameras") && dom["cameras"].IsArray())
		{
			const Value& cameras = dom["cameras"];

			// 只保存一个camera
			glCamera &newCamera = m_MainCamera;

			const Value& curCamera = cameras[0];
			if (curCamera.HasMember("name") && curCamera["name"].IsString())
				newCamera.name = curCamera["name"].GetString();
			if (curCamera.HasMember("type") && curCamera["type"].IsString())
				newCamera.type = Str2CameraType(curCamera["type"].GetString());
			// perspective
			if (newCamera.type == CameraType::Perspective)
				if (curCamera.HasMember("perspective"))
				{
					auto& newPerspective = newCamera.perspective;

					const Value& perspectiveCam = curCamera["perspective"];
					GetNumberValue(newPerspective.aspectRatio, perspectiveCam, "aspectRatio");
					GetNumberValue(newPerspective.yfov, perspectiveCam, "yfov");
					GetNumberValue(newPerspective.zfar, perspectiveCam, "zfar");
					GetNumberValue(newPerspective.znear, perspectiveCam, "znear");
				}
				else
					std::cout << "WARNING::Camera type is perspective, but doesn't have perspective properties";
			else if (newCamera.type == CameraType::Orthographics)
				if (curCamera.HasMember("orthographic"))
				{
					auto& newOrtho = newCamera.orthographic;

					const Value& orthoCam = curCamera["orthographic"];
					GetNumberValue(newOrtho.xmag, orthoCam, "xmag");
					GetNumberValue(newOrtho.ymag, orthoCam, "ymag");
					GetNumberValue(newOrtho.zfar, orthoCam, "zfar");
					GetNumberValue(newOrtho.znear, orthoCam, "znear");
				}
				else
					std::cout << "WARNING::Camera type is orthographic, but doesn't have orthographic properties";
		}

		// buffers minItems: 1
		if (dom.HasMember("buffers") && dom["buffers"].IsArray())
		{
			const Value& buffers = dom["buffers"];
			for (int i = 0, imax = buffers.Size(); i < imax; ++i)
			{
				glBuffer newBuffer;

				const Value& curBuffer = buffers[i];
				if (curBuffer.HasMember("name") && curBuffer["name"].IsString())
					newBuffer.name = curBuffer["name"].GetString();
				if (curBuffer.HasMember("byteLength") && curBuffer["byteLength"].IsInt())
					newBuffer.byteLength = curBuffer["byteLength"].GetInt();
				if (curBuffer.HasMember("uri") && curBuffer["uri"].IsString())
					newBuffer.uri = curBuffer["uri"].GetString();

				m_Buffers.emplace_back(newBuffer);
			}
		}
		// bufferViews minItems: 1
		if (dom.HasMember("bufferViews") && dom["bufferViews"].IsArray())
		{
			const Value& bufferViews = dom["bufferViews"];
			for (int i = 0, imax = bufferViews.Size(); i < imax; ++i)
			{
				glBufferView newBufferView;

				const Value& curBufferView = bufferViews[i];
				if (curBufferView.HasMember("buffer") && curBufferView["buffer"].IsInt())
					newBufferView.bufferIdx = curBufferView["buffer"].GetInt();
				if (curBufferView.HasMember("byteLength") && curBufferView["byteLength"].IsInt())
					newBufferView.byteLength = curBufferView["byteLength"].GetInt();
				if (curBufferView.HasMember("byteOffset") && curBufferView["byteOffset"].IsInt())
					newBufferView.byteOffset = curBufferView["byteOffset"].GetInt();
				if (curBufferView.HasMember("byteStride") && curBufferView["byteStride"].IsInt())
					newBufferView.byteStride = curBufferView["byteStride"].GetInt();
				if (curBufferView.HasMember("target") && curBufferView["target"].IsInt())
					newBufferView.target = (glBufferType)curBufferView["target"].GetInt();
				if (curBufferView.HasMember("name") && curBufferView["name"].IsString())
					newBufferView.name = curBufferView["name"].GetString();

				m_BufferViews.emplace_back(newBufferView);
			}
		}
		// accessors, minItems: 1
		if (dom.HasMember("accessors") && dom["accessors"].IsArray())
		{
			std::string errorInfo{ "accessors::" };
			const Value& accessors = dom["accessors"];
			for (int i = 0, imax = accessors.Size(); i < imax; ++i)
			{
				glAccessor newAccessor;

				const Value& curAccessor = accessors[i];
				if (curAccessor.HasMember("name") && curAccessor["name"].IsString())
					newAccessor.name = curAccessor["name"].GetString();
				if (curAccessor.HasMember("bufferView") && curAccessor["bufferView"].IsInt())
					newAccessor.bufferViewIdx = curAccessor["bufferView"].GetInt();
				if (curAccessor.HasMember("byteOffset") && curAccessor["byteOffset"].IsInt())
					newAccessor.byteOffset = curAccessor["byteOffset"].GetInt();
				if (curAccessor.HasMember("componentType") && curAccessor["componentType"].IsInt())
					newAccessor.componentType = (glDataType)curAccessor["componentType"].GetInt();
				if (curAccessor.HasMember("count") && curAccessor["count"].IsInt())
					newAccessor.count = curAccessor["count"].GetInt();
				if (curAccessor.HasMember("type") && curAccessor["type"].IsString())
					newAccessor.type = Str2GLType(curAccessor["type"].GetString(), errorInfo);
				if (curAccessor.HasMember("normalized") && curAccessor["normalized"].IsBool())
					newAccessor.normalized = curAccessor["normalized"].GetBool();
				if (curAccessor.HasMember("max") && curAccessor["max"].IsArray())
				{
					const Value& maxElement = curAccessor["max"];
					for (int j = 0, jmax = maxElement.Size(); j < jmax; ++j)
					{
						GetNumberValue(newAccessor.max[j], maxElement[j]);
					}
				}
				if (curAccessor.HasMember("min") && curAccessor["min"].IsArray())
				{
					const Value& minElement = curAccessor["min"];
					for (int j = 0, jmax = minElement.Size(); j < jmax; ++j)
					{
						GetNumberValue(newAccessor.min[j], minElement[j]);
					}
				}

				// parse accessor
				if (curAccessor.HasMember("sparse"))
				{
					glSparseData sparseData;

					const Value& sparseVal = curAccessor["sparse"];
					if (sparseVal.HasMember("count") && sparseVal["count"].IsInt())
						sparseData.count = sparseVal["count"].GetInt();
					if (sparseVal.HasMember("values"))
					{
						const Value& temp = sparseVal["values"];
						if (temp.HasMember("bufferView") && temp["bufferView"].IsInt())
							sparseData.values.bufferViewIdx = temp["bufferView"].GetInt();
						if (temp.HasMember("byteOffset") && temp["byteOffset"].IsInt())
							sparseData.values.byteOffset = temp["byteOffset"].GetInt();
					}
					if (sparseVal.HasMember("indices"))
					{
						const Value& temp = sparseVal["indices"];
						if (temp.HasMember("bufferView") && temp["bufferView"].IsInt())
							sparseData.indices.bufferViewIdx = temp["bufferView"].GetInt();
						if (temp.HasMember("componentType") && temp["componentType"].IsInt())
							sparseData.indices.componentType= (glDataType)temp["componentType"].GetInt();
						if (temp.HasMember("byteOffset") && temp["byteOffset"].IsInt())
							sparseData.indices.byteOffset = temp["byteOffset"].GetInt();
					}
					newAccessor.sparseData = sparseData;
				}

				m_Accessors.emplace_back(newAccessor);
			}
		}

		/// material, texture, sampler, image
		// material
		if (dom.HasMember("materials") && dom["materials"].IsArray())
		{
			std::string errorInfo{ "materials::" };
			const Value& materials = dom["materials"];
			for (int i = 0, imax = materials.Size(); i < imax; ++i)
			{
				glMaterial newMat;

				const Value& curMat = materials[i];
				if (curMat.HasMember("name") && curMat["name"].IsString())
					newMat.name = curMat["name"].GetString();
				// pbrMetallicRoughness
				if (curMat.HasMember("pbrMetallicRoughness"))
				{
					glPbrMetallicRoughness &metallicRoughnessTex = newMat.metallicRoughnessTex;

					const Value& pbrMetallicRoughnessDom = curMat["pbrMetallicRoughness"];
					if (pbrMetallicRoughnessDom.HasMember("baseColorFactor") && pbrMetallicRoughnessDom["baseColorFactor"].IsArray())
					{
						const Value& baseColorFactorDom = pbrMetallicRoughnessDom["baseColorFactor"];
						for (int j = 0, jmax = baseColorFactorDom.Size(); j < jmax; ++j)
						{
							GetNumberValue(metallicRoughnessTex.baseColorFactor[j], baseColorFactorDom[j]);
						}
					}
					if (pbrMetallicRoughnessDom.HasMember("baseColorTexture"))
					{
						const Value& baseColorTexDom = pbrMetallicRoughnessDom["baseColorTexture"];
						if (baseColorTexDom.HasMember("index") && baseColorTexDom["index"].IsInt())
							metallicRoughnessTex.baseColorTex.index = baseColorTexDom["index"].GetInt();
						if (baseColorTexDom.HasMember("texCoord") && baseColorTexDom["texCoord"].IsInt())
							metallicRoughnessTex.baseColorTex.texCoord = baseColorTexDom["texCoord"].GetInt();
					}
					if (pbrMetallicRoughnessDom.HasMember("metallicRoughnessTexture"))
					{
						const Value& metallicTexDom = pbrMetallicRoughnessDom["metallicRoughnessTexture"];
						if (metallicTexDom.HasMember("index") && metallicTexDom["index"].IsInt())
							metallicRoughnessTex.metallicRoughnessTex.index = metallicTexDom["index"].GetInt();
						if (metallicTexDom.HasMember("texCoord") && metallicTexDom["texCoord"].IsInt())
							metallicRoughnessTex.metallicRoughnessTex.texCoord = metallicTexDom["texCoord"].GetInt();
					}
					//if (pbrMetallicRoughnessDom.HasMember("metallicFactor") && pbrMetallicRoughnessDom["metallicFactor"].IsFloat())
					//	metallicRoughnessTex.metallic = pbrMetallicRoughnessDom["metallicFactor"].GetFloat();
					GetNumberValue(metallicRoughnessTex.metallic, pbrMetallicRoughnessDom, "metallicFactor");
					//if (pbrMetallicRoughnessDom.HasMember("roughnessFactor") && pbrMetallicRoughnessDom["roughnessFactor"].IsFloat())
					//	metallicRoughnessTex.roughness = pbrMetallicRoughnessDom["roughnessFactor"].GetFloat();
					GetNumberValue(metallicRoughnessTex.roughness, pbrMetallicRoughnessDom, "roughnessFactor");
				}
				if (curMat.HasMember("normalTexture"))
				{
					glNormalTexInfo& normalTex = newMat.normalTex;

					const Value& normalTexDom = curMat["normalTexture"];
					if (normalTexDom.HasMember("index") && normalTexDom["index"].IsInt())
						normalTex.index = normalTexDom["index"].GetInt();
					if (normalTexDom.HasMember("texCoord") && normalTexDom["texCoord"].IsInt())
						normalTex.texCoord = normalTexDom["texCoord"].GetInt();
					//if (normalTexDom.HasMember("scale") && normalTexDom["scale"].IsFloat())
					//	normalTex.scale = normalTexDom["scale"].GetFloat();
					GetNumberValue(normalTex.scale, normalTexDom, "scale");
				}
				if (curMat.HasMember("occlusionTexture"))
				{
					glOcclusionTexInfo& occlusionTex = newMat.occlusionTex;

					const Value& occlusionTexDom = curMat["occlusionTexture"];
					if (occlusionTexDom.HasMember("index") && occlusionTexDom["index"].IsInt())
						occlusionTex.index = occlusionTexDom["index"].GetInt();
					if (occlusionTexDom.HasMember("texCoord") && occlusionTexDom["texCoord"].IsInt())
						occlusionTex.texCoord = occlusionTexDom["texCoord"].GetInt();
					//if (occlusionTexDom.HasMember("strength") && occlusionTexDom["strength"].IsFloat())
					//	occlusionTex.strength = occlusionTexDom["strength"].GetFloat();
					GetNumberValue(occlusionTex.strength, occlusionTexDom, "strength");
				}
				if (curMat.HasMember("emissiveTexture"))
				{
					glTexureInfo& emissiveTex = newMat.emissiveTex;

					const Value& emissiveTexDom = curMat["emissiveTexture"];
					if (emissiveTexDom.HasMember("index") && emissiveTexDom["index"].IsInt())
						emissiveTex.index = emissiveTexDom["index"].GetInt();
					if (emissiveTexDom.HasMember("texCoord") && emissiveTexDom["texCoord"].IsInt())
						emissiveTex.texCoord = emissiveTexDom["texCoord"].GetInt();
				}
				if (curMat.HasMember("emissiveFactor") && curMat["emissiveFactor"].IsArray())
				{
					const Value& emissiveFactorDom = curMat["emissiveFactor"];
					for (int j = 0, jmax = emissiveFactorDom.Size(); j < jmax; ++j)
					{
						GetNumberValue(newMat.emissiveFactor[j], emissiveFactorDom[j]);
					}	
				}
				if (curMat.HasMember("alphaMode") && curMat["alphaMode"].IsString())
				{
					newMat.strAlphaMode = curMat["alphaMode"].GetString();
					newMat.eAlphaMode = Str2GLAlphaMode(newMat.strAlphaMode, errorInfo);
				}
				//if (curMat.HasMember("alphaCutoff") && curMat["alphaCutoff"].IsFloat())
				//	newMat.alphaCutoff = curMat["alphaCutoff"].GetFloat();
				GetNumberValue(newMat.alphaCutoff, curMat, "alphaCutoff");
				if (curMat.HasMember("doubleSided") && curMat["doubleSided"].IsBool())
					newMat.doubleSided = curMat["doubleSided"].GetBool();

				m_Materials.emplace_back(newMat);
			}
		}
		if (dom.HasMember("textures") && dom["textures"].IsArray())
		{
			std::string errorInfo{ "textures::" };
			const Value& textures = dom["textures"];
			for (int i = 0, imax = textures.Size(); i < imax; ++i)
			{
				glTexture newTex;

				const Value& curTex = textures[i];
				if (curTex.HasMember("name") && curTex["name"].IsString())
					newTex.name = curTex["name"].GetString();
				if (curTex.HasMember("source") && curTex["source"].IsInt())
					newTex.sourceIdx = curTex["source"].GetInt();
				if (curTex.HasMember("sampler") && curTex["sampler"].IsInt())
					newTex.samplerIdx = curTex["sampler"].GetInt();

				m_Textures.emplace_back(newTex);
			}
		}
		if (dom.HasMember("images") && dom["images"].IsArray())
		{
			std::string errorInfo{ "images::" };
			const Value& images = dom["images"];
			for (int i = 0, imax = images.Size(); i < imax; ++i)
			{
				glImage newImage;

				const Value& curImage = images[i];
				if (curImage.HasMember("name") && curImage["name"].IsString())
					newImage.name = curImage["name"].GetString();
				if (curImage.HasMember("uri") && curImage["uri"].IsString())
					newImage.uri = curImage["uri"].GetString();
				else
				{
					if (curImage.HasMember("mimeType") && curImage["mimeType"].IsString())
						newImage.mimeType = curImage["mimeType"].GetString();
					if (curImage.HasMember("bufferView") && curImage["bufferView"].IsInt())
						newImage.bufferViewIdx = curImage["bufferView"].GetInt();
				}				

				m_Images.emplace_back(newImage);
			}
		}
		if (dom.HasMember("samplers") && dom["samplers"].IsArray())
		{
			std::string errorInfo{ "samplers::" };
			const Value& samplers = dom["samplers"];
			for (int i = 0, imax = samplers.Size(); i < imax; ++i)
			{
				glSampler newSampler;

				const Value& curSampler = samplers[i];
				if (curSampler.HasMember("name") && curSampler["name"].IsString())
					newSampler.name = curSampler["name"].GetString();
				if (curSampler.HasMember("magFilter") && curSampler["magFilter"].IsInt())
					newSampler.magFilter = (glTextureFilter)curSampler["magFilter"].GetInt();
				if (curSampler.HasMember("minFilter") && curSampler["minFilter"].IsInt())
					newSampler.minFilter= (glTextureFilter)curSampler["minFilter"].GetInt();
				if (curSampler.HasMember("wrapS") && curSampler["wrapS"].IsInt())
					newSampler.wrapS = (glTextureWrapMode)curSampler["wrapS"].GetInt();
				if (curSampler.HasMember("wrapT") && curSampler["wrapT"].IsInt())
					newSampler.wrapT = (glTextureWrapMode)curSampler["wrapT"].GetInt();

				m_Samplers.emplace_back(newSampler);
			}
		}
	}

	void glTFImporter::ReadBuffers()
	{
		for (size_t i = 0, imax = m_Buffers.size(); i < imax; ++i)
		{
			const auto& curBuffer = m_Buffers[i];
			if (!curBuffer.uri.empty())
			{
				std::string fileName = curBuffer.uri;
				if (imax == 1)	// 默认文件名为scene.bin，修改为Dir/fileName.bin
				{
					size_t rpos = fileName.rfind('.');
					fileName.replace(0, rpos, m_FileName.c_str());
				}

				fileName = m_FileDir + fileName;
				ReadFromFile(fileName, curBuffer.byteLength);
			}
		}
	}

	bool glTFImporter::ReadFromFile(const std::string& fileName, uint32_t bufferLength)
	{
		std::ifstream ifs(fileName, std::ifstream::in | std::ifstream::binary);
		if (!ifs.is_open())
		{
			std::cout << "Failed to load glTF model: " << ifs.failbit << std::endl;
			return false;
		}

		size_t byteLength = 0;

		ifs.seekg(0, ifs.end);
		byteLength = static_cast<size_t>(ifs.tellg());	// tellg - returns the input position indicator
		if (bufferLength > 0 && bufferLength != byteLength)
		{
			std::cout << "Load byte length: " << byteLength << " is not equal to the given length: " << bufferLength << std::endl;
			ifs.close();
			return false;
		}

		ifs.seekg(0, ifs.beg);

		unsigned char* newBytes = new unsigned char[byteLength];
		ifs.read(reinterpret_cast<char*>(newBytes), byteLength);

		m_BinData.emplace_back(std::unique_ptr<unsigned char[]>(newBytes));

		ifs.close();

		// 
		// 
		// Utility::ReadFileSync();
		return true;
	}

	void glTFImporter::BuildNodeTree()
	{
		for (size_t i = 0, imax = m_Nodes.size(); i < imax; ++i)
		{
			auto& curNode = m_Nodes[i];
			curNode.bDirty = true;
			const auto& children = curNode.children;
			std::for_each(children.begin(), children.end(), [&](int index) {
				m_Nodes[index].parentIdx = i;
				});
		}
		// 缓存根节点
		for (int i = 0, imax = m_Nodes.size(); i < imax; ++i)
		{
			auto& curNode = m_Nodes[i];
			if (curNode.parentIdx == -1)
				m_RootNodes.push_back(i);
		}
	}

	void glTFImporter::CacheTransform()
	{
		if (m_bDirty)
		{
			Matrix4x4 mat;
			std::deque<int> nodeQueue;
			for (size_t i = 0, imax = m_RootNodes.size(); i < imax; ++i)
			{
				int curRootIdx = m_RootNodes[i];
				auto& rootNode = m_Nodes[curRootIdx];

				nodeQueue.clear();
				nodeQueue.push_back(curRootIdx);
				while (!nodeQueue.empty())
				{
					int idx = nodeQueue.front();
					nodeQueue.pop_front();
					auto& curNode = m_Nodes[idx];
					const auto& children = curNode.children;

					if (children.empty())
						continue;

					if (curNode.bDirty)
					{
						mat = curNode.parentTransCache * curNode.transform;
						for (size_t j = 0, jmax = children.size(); j < jmax; ++j)
						{
							int childIdx = children[j];
							m_Nodes[childIdx].parentTransCache = mat;
							nodeQueue.push_back(childIdx);
						}
					}
					else
					{
						for (size_t j = 0, jmax = children.size(); j < jmax; ++j)
						{
							int childIdx = children[j];
							nodeQueue.push_back(childIdx);
						}
					}
					curNode.bDirty = false;
				}
			}
			m_bDirty = false;
		}
	}

	bool glTFImporter::BuildScenes()
	{
		BuildNodeTree();
		CacheTransform();

		ReadBuffers();

		InitVAttribFormats();

		return BuildMeshes() && BuildMaterials();
	}

	bool glTFImporter::BuildMeshes()
	{
		m_ActiveNodes.clear();
		m_ActiveMeshes.clear();
		m_ActiveMaterials.clear();

		std::stack<int> nodeStack;
		// main scene
		if (m_DefaultScene != -1)
		{
			auto& curScene = m_Scenes[m_DefaultScene];
			auto& sceneNodes = curScene.nodes;
			for (size_t j = 0, jmax = sceneNodes.size(); j < jmax; ++j)
				nodeStack.push(sceneNodes[j]);
		}
		else
		{
			for (size_t i = 0, imax = m_RootNodes.size(); i < imax; ++i)
				nodeStack.push(m_RootNodes[i]);
		}

		/// pass 1
		// 遍历node，存储mesh
		uint32_t curVertexByteLength = 0;
		uint32_t curIndexByteLength = 0;
		int curActiveNum = 0;
		while (!nodeStack.empty())
		{
			int curNodeIdx = nodeStack.top();
			nodeStack.pop();
			m_ActiveNodes.emplace_back(curNodeIdx);

			auto& curNode = m_Nodes[curNodeIdx];
			if (curNode.meshIdx >= 0)
			{
				m_ActiveMeshes.emplace_back(curNode.meshIdx);

				// build mesh
				auto& curMesh = m_Meshes[curNode.meshIdx];

				// primitives
				auto& primitives = curMesh.primitives;
				for (size_t i = 0, imax = primitives.size(); i < imax; ++i)
				{
					// 新建mesh
					Mesh newMesh;
					newMesh.nodeIndex = curNodeIdx;
					newMesh.vertexDataByteOffset = curVertexByteLength;
					newMesh.indexDataByteOffset = curIndexByteLength;

					auto& curPrimitive = primitives[i];

					uint32_t curMeshVertexCount = 0;
					uint32_t curMeshVertexByteOffset = 0;
					uint32_t enabledAttribs = 0;

					// attributes 顶点属性
					auto& attributes = curPrimitive.attributes;
					for (size_t j = 0, jmax = Attrib::maxAttrib; j < jmax; ++j)
					{
						auto& curAttrib = attributes[j];
						const auto& cachedAttrib = m_VertexAttributes[j];
						curAttrib.alignedByteOffset = cachedAttrib.alignedByteOffset;
						curAttrib.byteLen = cachedAttrib.byteLen;
						curAttrib.format = cachedAttrib.format;

						int attribByteSize = 0;
						if (curAttrib.accessor >= 0)
						{
							enabledAttribs |= (1 << j);

							const auto& curAccessor = m_Accessors[curAttrib.accessor];

							// 顶点总数
							if (curMeshVertexCount == 0)
								curMeshVertexCount = curAccessor.count;
							else
								assert(curMeshVertexCount == curAccessor.count);

							// 属性字节长度，可以直接从缓存的顶点属性中读取，这里还是直接从源数据获取
							int componentSize = GetComponentSizeInBytes(curAccessor.componentType);
							ASSERT(componentSize > 0);
							int numComponents = GetNumComponentsInType(curAccessor.type);
							ASSERT(numComponents > 0);
							attribByteSize = componentSize * numComponents;

							// 源数据信息
							int bufferViewIdx = curAccessor.bufferViewIdx;
							const auto& curBufferView = m_BufferViews[bufferViewIdx];
							curAttrib.bufferIdx = curBufferView.bufferIdx;
							curAttrib.bufferOffset = curBufferView.byteOffset + curAccessor.byteOffset;
							curAttrib.bufferByteStride = curBufferView.byteStride;
							if (curAttrib.bufferByteStride == 0)
								curAttrib.bufferByteStride = attribByteSize;

							// bounding box
							if (j == Attrib::attrib_position)
							{
								auto& mins = curAccessor.min;
								auto& maxs = curAccessor.max;
								newMesh.boundingBox.min = Vector3(mins[0], mins[1], mins[2]);
								newMesh.boundingBox.max = Vector3(maxs[0], maxs[1], maxs[2]);
							}
						}
						else
							attribByteSize = m_VertexAttributes[j].byteLen;

						ASSERT(curMeshVertexCount > 0);	// 这里应该已经初始化了，必须含有POSITION属性
						int totalByteLegnth = curMeshVertexCount * attribByteSize;	// curMeshVertexCount
						curMeshVertexByteOffset += totalByteLegnth;

						newMesh.attribs[j] = curAttrib;
					}
					newMesh.enabledAttribs = enabledAttribs;
					newMesh.vertexCount = curMeshVertexCount;
					newMesh.vertexStride = m_VertexStride;

					curVertexByteLength += curMeshVertexByteOffset;

					// index 索引
					int indexAccessor = curPrimitive.indexAccessor;
					{
						newMesh.indexAccessor = indexAccessor;
						// -1 - 没有索引
						if (indexAccessor >= 0)
						{
							auto& curAccessor = m_Accessors[indexAccessor];
							newMesh.indexCount = curAccessor.count;
							curIndexByteLength += curAccessor.count * sizeof(unsigned short);
						}
					}

					// 材质索引
					int matIdx = curPrimitive.materialIdx;
					newMesh.materialIndex = matIdx;

					if (m_ActiveMaterials.find(matIdx) == m_ActiveMaterials.end())
						m_ActiveMaterials[matIdx] = curActiveNum++;

					// glTopology mode 默认采用TRIANGLE

					// targets - Morph Targets 暂不支持	-20-3-4

					m_oMeshes.emplace_back(newMesh);
				}
			}
			else if (curNode.cameraIdx >= 0)
			{
				// build camera
			}
			else if (curNode.skinIdx >= 0)
			{
				// build skin
			}
			else if (!curNode.children.empty())
			{
				for (int i = curNode.children.size() - 1; i >= 0; --i)	// 逆序添加(其实没有意义，只是符合以前习惯，先左后右)
					nodeStack.push(curNode.children[i]);
			}
		}

		/// pass 2
		bool bValid = true;
		{
			if (curVertexByteLength > 0)	// 必须>0
			{
				m_VertexData.reset(new unsigned char[curVertexByteLength] {0});
				m_VertexByteLength = curVertexByteLength;
			}
			if (curIndexByteLength > 0)	// 索引可能为0
			{
				m_IndexData.reset(new unsigned char[curIndexByteLength] {0});
				m_IndexByteLength = curIndexByteLength;
			}

			for (size_t i = 0, imax = m_oMeshes.size(); i < imax; ++i)
			{
				const auto& curMesh = m_oMeshes[i];

				// vertices
				{
					uint32_t curVertexCount = curMesh.vertexCount;
					uint32_t curVertexByteOffset = curMesh.vertexDataByteOffset;
					uint32_t curVertexStride = curMesh.vertexStride;
					uint32_t enabledAttribs = curMesh.enabledAttribs;
					unsigned char* dstPos = m_VertexData.get() + curVertexByteOffset;

					for (size_t j = 0, jmax = Attrib::maxAttrib; j < jmax; ++j)
					{
						bool bEnabled = (enabledAttribs & (1 << j)) != 0;
						if (bEnabled && curMesh.attribs[j].accessor >= 0)
						{
							const auto& curAttrib = curMesh.attribs[j];
							uint32_t curAttribLen = curAttrib.byteLen;
							unsigned char* dstAttriPos = dstPos + curAttrib.alignedByteOffset;

							uint32_t bufferIdx = curAttrib.bufferIdx;
							uint32_t bufferOffset = curAttrib.bufferOffset;
							uint32_t srcStride = curAttrib.bufferByteStride;
							unsigned char* srcPos = m_BinData[bufferIdx].get() + bufferOffset;
							for (size_t k = 0; k < curVertexCount; ++k)
							{
								memcpy_s(dstAttriPos, curAttribLen, srcPos, curAttribLen);
								// memcpy(dstAttriPos, srcPos, curAttribLen);
								dstAttriPos += curVertexStride;
								srcPos += srcStride;
							}
						}
					}
				}

				// indices
				{
					// indices默认采用unsigned char格式
					int indexAccessor = curMesh.indexAccessor;
					if (indexAccessor >= 0)
					{
						uint32_t curIndexCount = curMesh.indexCount;
						uint32_t curIndexByteLength = curIndexCount * sizeof(unsigned short);
						uint32_t curIndexByteOffset = curMesh.indexDataByteOffset;
						unsigned char* dstPos = m_IndexData.get() + curIndexByteOffset;

						const auto& accessor = m_Accessors[indexAccessor];
						const auto& bufferView = m_BufferViews[accessor.bufferViewIdx];
						uint32_t bufferIdx = bufferView.bufferIdx;
						uint32_t bufferOffset = bufferView.byteOffset + accessor.byteOffset;
						uint32_t bufferStride = bufferView.byteStride;
						unsigned char* srcPos = m_BinData[bufferIdx].get() + bufferOffset;

						if (bufferStride == 0)	// indices are packed tightly
						{
							if (accessor.componentType == glDataType::UNSIGNED_SHORT)	// 格式匹配
								memcpy_s(dstPos, curIndexByteLength, srcPos, curIndexByteLength);
							else
							{
								int componentSize = GetComponentSizeInBytes(accessor.componentType);
								if (componentSize < 0)
									return false;
								int numComponents = GetNumComponentsInType(accessor.type);
								if (numComponents < 0)
									return false;

								bufferStride = componentSize * numComponents;
							}
						}

						if (bufferStride > 0)
						{
							unsigned short* shDstPos = (unsigned short*)(dstPos);

							if (accessor.componentType == glDataType::UNSIGNED_SHORT)
							{
								unsigned short* shSrcPos = (unsigned short*)srcPos;
								for (size_t j = 0; j < curIndexCount; ++j)
								{
									*(shDstPos++) = *shSrcPos;
									shSrcPos = (unsigned short*)((unsigned char*)shSrcPos + bufferStride);
								}
							}
							// 好像很多都是UNSIGNED_INT格式
							else if (accessor.componentType == glDataType::UNSIGNED_INT)
							{
								unsigned int* uiSrcPos = (unsigned int*)srcPos;
								for (size_t j = 0; j < curIndexCount; ++j)
								{
									*(shDstPos++) = *uiSrcPos;
									uiSrcPos = (unsigned int*)((unsigned char*)uiSrcPos + bufferStride);
								}
							}
						}

						// debug
						//unsigned short* psh = (unsigned short*)dstPos;
						//for (int k = 0; k < curIndexCount && k < 100; ++k)
						//{
						//	std::cout << *(psh++) << (((k + 1) % 12 > 0) ? " " : "\n");
						//}
						//std::cout << "Mesh " << i << std::endl;
					}
				}
			}
		}

		ComputeBoundingBox();

		return true;
	}

	bool glTFImporter::BuildMaterials()
	{
		m_ActiveImages.clear();

		int activeMatNum = m_ActiveMaterials.size();
		if (activeMatNum > 0)
			m_oMaterials.resize(activeMatNum);
		else
			return true;

		InitTextures();

		for (auto iter = m_ActiveMaterials.begin(); iter != m_ActiveMaterials.end(); ++iter)
		{
			Material newMat;

			int curMatIdx = (*iter).first;
			int activeMatIdx = (*iter).second;
			const auto& curMat = m_Materials[curMatIdx];
			
			// pbrMetallicRoughness
			const auto& pbrMetallicRoughness = curMat.metallicRoughnessTex;
			memcpy_s(newMat.baseColorFactor, 4 * sizeof(float), pbrMetallicRoughness.baseColorFactor, 4 * sizeof(float));
			newMat.metallic = pbrMetallicRoughness.metallic;
			newMat.roughness = pbrMetallicRoughness.roughness;
			
			const auto& baseColorTex = pbrMetallicRoughness.baseColorTex;
			newMat.texBaseColorPath = GetImagePath(baseColorTex.index, m_DefaultBaseColor);
			const auto& metallicRoughnessTex = pbrMetallicRoughness.metallicRoughnessTex;
			newMat.texMetallicRoughnessPath = GetImagePath(metallicRoughnessTex.index);

			// normal
			const auto& normalTex = curMat.normalTex;
			newMat.normalScale = normalTex.scale;
			newMat.texNormalPath = GetImagePath(normalTex.index, m_DefaultNormal);

			// occlusion
			const auto& occlusionTex = curMat.occlusionTex;
			newMat.occlusionStrength = occlusionTex.strength;
			newMat.texOcclusionPath = GetImagePath(occlusionTex.index, m_DefaultOcclusion);

			// emissive
			memcpy_s(newMat.emissiveFactor, 3 * sizeof(float), curMat.emissiveFactor, 3 * sizeof(float));
			const auto& emissiveTex = curMat.emissiveTex;
			newMat.texEmissivePath = GetImagePath(emissiveTex.index, m_DefaultEmissive);

			newMat.texcoords[0] = baseColorTex.texCoord;
			newMat.texcoords[1] = metallicRoughnessTex.texCoord;
			newMat.texcoords[2] = normalTex.texCoord;
			newMat.texcoords[3] = occlusionTex.texCoord;
			newMat.texcoords[4] = emissiveTex.texCoord;

			// others
			newMat.alphaCutoff = curMat.alphaCutoff;

			// settings
			newMat.eAlphaMode = curMat.eAlphaMode;
			newMat.doubleSided = curMat.doubleSided;
		
			// m_oMaterials.emplace_back(newMat);
			m_oMaterials[activeMatIdx] = newMat;
		}

		LoadTextures(m_pDevice);

		return true;
	}

	// 暂时不考虑变换
	void glTFImporter::ComputeBoundingBox()
	{
		m_BoundingBox.min = Vector3(10000.0f, 10000.0f, 10000.0f);
		m_BoundingBox.max = Vector3(-10000.0f, -10000.0f, -10000.0f);
		for (size_t i = 0, imax = m_oMeshes.size(); i < imax; ++i)
		{
			const auto& curMesh = m_oMeshes[i];
			m_BoundingBox.min = glm::min(m_BoundingBox.min, curMesh.boundingBox.min);
			m_BoundingBox.max = glm::max(m_BoundingBox.max, curMesh.boundingBox.max);
		}
	}

	std::string glTFImporter::GetImagePath(int curTexIdx, const std::string& defaultPath)
	{
		if (curTexIdx >= 0)
		{
			const auto& curTex = m_Textures[curTexIdx];
			int imageIdx = curTex.sourceIdx;
			if (imageIdx >= 0)
			{
				m_ActiveImages.insert(imageIdx);
				const auto& curImage = m_Images[imageIdx];
				if (!curImage.uri.empty())
					return curImage.uri;
			}
		}
		return defaultPath;
	}

	void glTFImporter::LoadTextures(ID3D12Device* pDevice)
	{
		using namespace MyDirectX;

		uint32_t activeMatCount = m_ActiveMaterials.size();
		if (activeMatCount > 0)
		{
			m_SRVs.reset(new D3D12_CPU_DESCRIPTOR_HANDLE[activeMatCount * Material::TextureNum]);

			const ManagedTexture* matTextures[Material::TextureNum] = {};

			for (size_t i = 0; i < activeMatCount; ++i)
			{
				const auto& curMat = m_oMaterials[i];
				// base color
				bool bValid = !curMat.texBaseColorPath.empty();
				if (bValid)
				{
					matTextures[0] = Graphics::s_TextureManager.LoadFromFile(pDevice, curMat.texBaseColorPath);
					bValid = matTextures[0]->IsValid();
				}
				if (!bValid)
				{
					matTextures[0] = dynamic_cast<const ManagedTexture*>(&TextureManager::GetWhiteTex2D());
				}

				// metallic roughness
				bValid = !curMat.texMetallicRoughnessPath.empty();
				if (bValid)
				{
					matTextures[1] = Graphics::s_TextureManager.LoadFromFile(pDevice, curMat.texMetallicRoughnessPath);
					bValid = matTextures[1]->IsValid();
				}
				if (!bValid)
				{
					matTextures[1] = dynamic_cast<const ManagedTexture*>(&TextureManager::GetWhiteTex2D());
				}

				// normal
				bValid = !curMat.texNormalPath.empty();
				if (bValid)
				{
					matTextures[2] = Graphics::s_TextureManager.LoadFromFile(pDevice, curMat.texNormalPath);
					bValid = matTextures[2]->IsValid();
				}
				if (!bValid)
				{
					matTextures[2] = dynamic_cast<const ManagedTexture*>(&TextureManager::GetWhiteTex2D());
				}

				// occlusion
				bValid = !curMat.texOcclusionPath.empty();
				if (bValid)
				{
					matTextures[3] = Graphics::s_TextureManager.LoadFromFile(pDevice, curMat.texOcclusionPath);
					bValid = matTextures[3]->IsValid();
				}
				if (!bValid)
				{
					matTextures[3] = dynamic_cast<const ManagedTexture*>(&TextureManager::GetWhiteTex2D());
				}

				// emissive
				bValid = !curMat.texEmissivePath.empty();
				if (bValid)
				{
					matTextures[4] = Graphics::s_TextureManager.LoadFromFile(pDevice, curMat.texEmissivePath);
					bValid = matTextures[4]->IsValid();
				}
				if (!bValid)
				{
					matTextures[4] = dynamic_cast<const ManagedTexture*>(&TextureManager::GetBlackTex2D());
				}

				uint32_t ind = i * Material::TextureNum;
				m_SRVs[ind + 0] = matTextures[0]->GetSRV();
				m_SRVs[ind + 1] = matTextures[1]->GetSRV();
				m_SRVs[ind + 2] = matTextures[2]->GetSRV();
				m_SRVs[ind + 3] = matTextures[3]->GetSRV();
				m_SRVs[ind + 4] = matTextures[4]->GetSRV();
			}
		}
	}

	void glTFImporter::InitVAttribFormats()
	{
		int attribCounter = 0;
		for (size_t i = 0, imax = m_Meshes.size(); i < imax; ++i)
		{
			const auto& curMesh = m_Meshes[i];
			const auto& curPrimitives = curMesh.primitives;
			for (size_t j = 0, jmax = curPrimitives.size(); j < jmax; ++j)
			{
				const auto& curPrim = curPrimitives[j];
				const auto& attributes = curPrim.attributes;
				for (size_t k = 0; k < Attrib::maxAttrib; ++k)
				{
					if (attribCounter == Attrib::maxAttrib)
						goto Finished;

					auto& cachedAttrib = m_VertexAttributes[k];
					if (cachedAttrib.format == DXGI_FORMAT_UNKNOWN)	// 当前尚未缓存该属性格式
					{
						const auto& curAttrib = attributes[k];
						if (curAttrib.accessor >= 0)
						{
							const auto& curAccessor = m_Accessors[curAttrib.accessor];

							int componentSize = GetComponentSizeInBytes(curAccessor.componentType);
							if (componentSize < 0)
								continue;
							int numComponents = GetNumComponentsInType(curAccessor.type);
							if (numComponents < 0)
								continue;

							auto& cachedAttrib = m_VertexAttributes[k];
							cachedAttrib.accessor = curAttrib.accessor;
							cachedAttrib.byteLen = componentSize * numComponents;
							cachedAttrib.format = GetDXFormat(curAccessor.componentType, curAccessor.type, curAccessor.normalized);
							++attribCounter;
						}
					}
				}
			}
		}

	Finished:
		int byteOffset = 0;	// 顶点属性相对偏移

		int byteLength = 0;	// 各顶点属性字节长度
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		for (size_t i = 0; i < Attrib::maxAttrib; ++i)
		{
			auto& cachedAttrib = m_VertexAttributes[i];
			if (cachedAttrib.format == Unknown)
			{
				switch (i)
				{
				case Attrib::attrib_texcoord0:
				case Attrib::attrib_texcoord1:
					format = DXGI_FORMAT_R32G32_FLOAT;
					byteLength = 2 * sizeof(float);
					break;
				case Attrib::attrib_position:
				case Attrib::attrib_normal:
				case Attrib::attrib_color0:
					format = DXGI_FORMAT_R32G32B32_FLOAT;
					byteLength = 3 * sizeof(float);
					break;
				case Attrib::attrib_tangent:
				//case Attrib::attrib_joints0:
				//case Attrib::attrib_weights0:
					format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					byteLength = 4 * sizeof(float);
					break;
				}

				cachedAttrib.format = format;
				cachedAttrib.byteLen = byteLength;
			}
			cachedAttrib.alignedByteOffset = byteOffset;
			byteOffset += cachedAttrib.byteLen;
		}
		m_VertexStride = byteOffset;
	}

	void glTFImporter::InitTextures()
	{
		m_DefaultBaseColor = "default";
		m_DefaultMetallicRoughness = "";
		m_DefaultNormal = "default_normal";
		m_DefaultOcclusion = "";
		m_DefaultEmissive = "";

		// 将文件夹名称修改为model名称
		// 删除扩展名 （后续加载DDS图片不需要扩展名）
		if (!m_Images.empty())
		{
			std::for_each(m_Images.begin(), m_Images.end(), [this](glImage &curImage) {
				if (!curImage.uri.empty())
				{
					auto& filePath = curImage.uri;
					filePath = filePath.substr(0, filePath.rfind('.'));
					auto rpos = filePath.find_last_of("/\\");
					if (rpos != filePath.npos)
					{
						filePath.replace(0, rpos, m_FileName);
					}
					else
						filePath.insert(0, m_FileName);
				}
				});
		}
	}

	
}
/**
	Binary glTF files
	in the standard glTF format, there are 2 options for including external binary resources like buffer
data and textures: they may be referenced via URIs, or embedded in the JSON part of the glTF using data URIs.
When they are referenced via URIs, then each external resource implies a new download request. When they are
embedded as data URIs, the base 64 encoding of the binary data will increase the file size consideably.

	binary glTF file *.glb*, it contains a header, which gives basic information about the the version and 
structure of the data, and one or more chunks that contain the actual data. The first chunk always contains
the JSON data. The remaining chunks contain the binary data.
	|----12-byte header---- |----	chunk 0(JSON)   ----|chunk 1(binary buffer)---- |...
	|magic	|version|length	|length	|type	|data...	|length	|type	|data...
	|uint32	|uint32	|uint32	|uint32	|uint32	|uchar[]	|uint32	|uint32	|uchar[]...
	#magic -0x46546c67 - ASCII ""glTF
	#version -now is 2
	#length -the total length of the file, in bytes

	#chunkLength -the length of the chunkData
	#chunkType -defines what type of data is contained in the chunkData. 
	It may be 
		0x4E4F534A - ASCII "JSON",for JSON data,or 
		0x004E4942 - ASCII "BIN", for binary data

*/
