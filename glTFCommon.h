#pragma once
#include <string>
#include <vector>

#define GLMath

#if defined(GLMath)
#define GLM_FORCE_CTOR_INIT		// 需要初始化
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace glTF
{
	typedef glm::vec3 Vector3;
	typedef glm::vec4 Vector4;
	typedef glm::quat Quaternion;
	typedef glm::mat3x3 Matrix3x3;
	typedef glm::mat4x4 Matrix4x4;
}

#elif defined(DXMath)
#include "pch.h"
namespace glTF
{
	typedef Math::Vector3 Vector3;
	typedef Math::Vector4 Vector4;
	typedef Math::Quaternion Quaternion;
	typedef Math::Matrix3 Matrix3x3;
	typedef Math::Matrix4 Matrix4x4;
}
#endif

#define NOMINMAX	// d3d12.h 里面预定义了min, max (#define min/max ...)
#include <d3d12.h>

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0
namespace glTF
{
	// 以下enum来自 glTF Specification 2.0, TinyGLTF
	enum class glDataType
	{
		UNKNOWN = -1,

		BYTE = 5120,
		UNSIGNED_BYTE = 5121,
		SHORT = 5122,
		UNSIGNED_SHORT = 5123,
		INT = 5124,
		UNSIGNED_INT = 5125,
		FLOAT = 5126,
		DOUBLE = 5130
	};

	enum class glTopology
	{
		UNKNOWN = -1,

		POINTS = 0,
		LINES = 1,
		LINE_LOOP = 2,
		LINE_STRIP = 3,
		TRIANGLES = 4,
		TRIANGLE_STRIP = 5,
		TRIANGLE_FAN = 6
	};

	enum class glTextureFilter
	{
		UNKNOWN = -1,

		NEAREST = 9728,
		LINEAR = 9729,
		NEAREST_MIPMAP_NEAREST = 9984,
		LINEAR_MIPMAP_NEAREST = 9985,
		NEAREST_MIPMAP_LINEAR = 9986,
		LINEAR_MIPMAP_LINEAR = 9987
	};

	enum class glTextureWrapMode
	{
		UNKNOWN = -1,

		REPEAT = 10497,
		CLAMP_TO_EDGE = 33071,
		MIRRORED_REPEAT = 33648
	};

	enum class glParameterType
	{
		UNKNOWN = -1,

		FLOAT_VEC2 = 35664,
		FLOAT_VEC3 = 35665,
		FLOAT_VEC4 = 35666,

		INT_VEC2 = 35667,
		INT_VEC3 = 35668,
		INT_VEC4 = 35669,

		BOOL = 35670,
		BOOL_VEC2 = 35671,
		BOOL_VEC3 = 35672,
		BOOL_VEC4 = 35673,

		FLOAT_MAT2 = 35674,
		FLOAT_MAT3 = 35675,
		FLOAT_MAT4 = 35676,

		SAMPLER_2D = 35678
	};

	enum class glType
	{
		UNKNOWN = -1,

		VEC2 = 2,
		VEC3 = 3,
		VEC4 = 4,
		MAT2 = 32 + 2,
		MAT3 = 32 + 3,
		MAT4 = 32 + 4,
		SCALAR = 64 + 1,
		VECTOR = 64 + 4,
		MATRIX = 64 + 16
	};

	enum class glTextureFormat
	{
		UNKNOWN = -1,

		ALPHA = 6406,
		RGB = 6407,
		RGBA = 6408,
		LUMINANCE = 6409,
		LUMINANCE_ALPHA = 6410,
	};

	// 加前缀k，标注枚举，（某些地方可能 预定义枚举变量, #define...）
	enum class glAlphaMode
	{
		UNKNOWN = -1,

		kOPAQUE,	// the alpha value is ignored and the rendered output is fully opaque
		kMASK,	// the rendered output is either fully opaque or fully transparent depending on the alpha value and
				// the specified alpha cutoff value
		kBLEND	// the alpha value is used to composite the source and destination areas. 
				// The rendered output is combined with the background using the normal painting operation 
				// (i.e. the Porter and Duff over operator).
	};

	enum class glBufferType
	{
		UNKNOWN = -1,

		ARRAY_BUFFER = 34962,	// 顶点数组
		ELEMENT_ARRAY_BUFFER = 34963	// 索引数组
	};

	enum class glShaderType
	{
		UNKNOWN = -1,

		VERTEX = 35633,
		FRAGMENT = 35632
	};

	// the glTF JSON may contain scenes (with an optional default scene). Each scene can contain an array of 
	// indices of nodes
	struct glScene
	{
		std::string name;
		std::vector<int> nodes;
	};

	struct glCamera;

	// Each of the nodes can contain an array of indices of its children.
	// A node may contain a local transform. (a column-major matrix array or separate translation, rotation, scale properties)
	// Each node may refer t oa mesh or a camera, using indices that point into the meshes and cameras arrays.

	// The translation, rotation and scale properties of a node may also be the target of an animation: the animation
	// then describes how one property changes over time.
	// Nodes are also used in vertex skinning: a node hierarchy can define the skeleton of an animated character.
	// the node then refers to a mesh and a skin. The skin contains further information about how the mesh is deformed
	// based on the current skeleton pose.
	struct glNode
	{
		std::string name;

		Vector3 translation;
		Quaternion rotation;		// the node's unit quaternion rotation in the order (x, y, z, w), where w is the scalar
		Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
		Matrix4x4 transform;	// 16 floats, in column-major order

		std::vector<int> children;	// 子节点
		std::vector<float> weights;	// the weights of the instantiated Morph Target. Number of elements must match number of Morph Targets of used mesh

		int meshIdx = -1;			// 索引 mesh
		int cameraIdx = -1;			// 索引 camera
		int skinIdx = -1;			// the index of the skin referenced by this node. When a skin is referenced by
			// a node within a scene, all joints used by the skin must belong the same scene

		// mf	-20-3-2
		int parentIdx = -1;		// 父节点index
		bool bDirty = true;	// 是否重新计算transform缓存
		Matrix4x4 parentTransCache;	// 父节点transform缓存
	};

	// vertex attributes
	// 目前8个
	enum AttribMask
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
		attrib_mask_texcoord1 = attrib_mask_2,
		attrib_mask_normal = attrib_mask_3,
		attrib_mask_tangent = attrib_mask_4,
		attrib_mask_color0 = attrib_mask_5,
		attrib_mask_joints0 = attrib_mask_6,
		attrib_mask_weights0 = attrib_mask_7,
	};

	enum Attrib
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
		attrib_position = attrib_0,		// POSITION,VEC3 float
		attrib_texcoord0 = attrib_1,	// TEXCOORD_0,	VEC2, float | ubyte normalized | ushort normalized
		attrib_texcoord1 = attrib_2,	// TEXCOORD_1
		attrib_normal = attrib_3,		// NORMAL,	VEC3 float
		attrib_tangent = attrib_4,		// TANGENT,	VEC4 float, xyzw vertex tangent where the w component is a sign (-1 or 1) indicating handedness of the tangent basis
		attrib_color0 = attrib_5,		// COLOR_0,	VEC3|VEC4	float | ubyte normalized | ushort normalized
		attrib_joints0 = attrib_6,		// JOINTS_0,	VEC4, ubyte | ushort	Skinned Mesh Attributes		<"boneIndices">
		attrib_weights0 = attrib_7,		// WEIGHTS_0,	VEC4, float | ubyte normalized | ushort normalized	<"boneWeights">

		maxAttrib = attrib_color0 + 1	// 8 （暂时只用上述6个	-20-3-5）
	};

	// in glTF, a dictionary object, where each key corresponds to mesh attribute semantic and 
	// each value is the index of the accessor containing attributes's data
	struct vAttribute
	{
		std::string semanticName;
		int semanticIndex = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		int alignedByteOffset = 0;
		int byteLen = 0;
		int bufferIdx = 0;		// 源buffer
		int bufferOffset = 0;	// 源buffer offset
		int bufferByteStride = 0;	// 对应glBufferView byteStride
		int accessor = -1;
	};

	// the mesh may contain multiple mesh primitives.
	// each mesh primitive has a rendering mode, which is a constant indicating whether it should be 
	// rendered as POINTS, LINES, or TRIANGLES. The primitive also refers to indices and the attributes
	// of the vertices, using the indices of the accessors for this data. 
	struct glPrimitive
	{
		// std::vector<vAttribute> attributes;
		// 改成固定数量attributes
		vAttribute attributes[Attrib::maxAttrib];
		int indexAccessor = -1;	// the index of the accessor that contains the indices. When this is not defined,
								// the primitives should be rendered without indices
		int materialIdx = -1;	// the index of the material to apply to this primitive when rendering
		glTopology mode = glTopology::TRIANGLES;		// the type of primitives to render
		// 暂不支持 Morph Target (What's this ???)	-20-3-2
		// targets // an array of Morph Targets, each Morph Target is a dictionary mapping attributes (only
		// `POSITION`, `NORMAL`, `TANGENT` supported ) to their deriations in the Morph Target
	};

	struct glMesh
	{
		std::string name;
		std::vector<glPrimitive> primitives;
		// 暂不支持
		// std::vector<float> weights;	// array of weights to be applied to the Morph Targets
	};

	enum class CameraType
	{
		Orthographics,
		Perspective
	};
	struct glCamera
	{
		glCamera()
		{
			type = CameraType::Perspective;
			perspective = { 1.0f, 1.5f, 100.0f, 0.01f };
		}

		std::string name;
		CameraType type;	// perspective or orthographic
		union
		{
			// 这里不能定义初始值? -20-3-1
			struct
			{
				float aspectRatio;	// the floating-point aspect ratio of the field of view
				float yfov;		// the floating-point vertical field of view in radians
				float zfar;
				float znear;
			} perspective;
			struct
			{
				float xmag;	// the floating-point horizontal magnification of the view, must not be 0
				float ymag;	// the floating-point vertical magnification of the view, must not be 0
				float zfar;
				float znear;
			} orthographic;
		};
	};

	/// buffers bufferViews, accessors 
	//
	// the buffers contain the data that is used for the geometry of 3D models, animation, and skinning.
	// each of the buffers refers to a binary data file, using a URI. It is the source of one block of raw data
	// with the given byteLength
	struct glBuffer
	{
		std::string name;
		std::string uri;
		int byteLength = 0;	// required, min = 1
	};

	// the bufferViews add structural information to this data.
	// each of the bufferViews refers to one buffer. It has a byteOffset and a byteLength, defining the part
	// of the buffer that belongs to the bufferView, and an optional OpenGL buffer target.
	// The data of multiple accessors may be interleaved inside a bufferView. In this case, the bufferView will 
	// have a byteStride property that says how many bytes are between the start of one element of an accessor,
	// and the start of the next
	// "a view into a buffer generally representing a subset of the buffer"
	struct glBufferView
	{
		std::string name;
		int bufferIdx = 0;
		int byteOffset = 0;	// min 0, default 0
		int byteLength = 0;	// min = 1, 0 = invalid
		int byteStride = 0;	// min 4, max 252 (multiple of 4), default 0 = understood to be tightly packed
		glBufferType target = glBufferType::UNKNOWN;	// ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER for vertex attributes or indices
	};

	// the accessors define how the data of a bufferView is interpreted. They may define an additional byteOffset
	// referring to the start of the bufferView, and contain information about the type and layout of the bufferView data.
	//		Sparse accessors - when only few elements of an accessors differ from a default value, then the data can be
	// given in a very compact form using a sparse data description:	The accessor defines the type of the data,
	// and the total element count
	// the sparse data block contains the count of sparse data elements: The values refer to the bufferView that 
	// contains the sparse data values. The target indices for the sparse data values are defined with a reference
	// to a bufferView and the componentType
	struct glSparseData
	{
		int count = 0;	// number of entries stored in the sparse array, min = 1
		bool bSparse = false;

		struct
		{
			int bufferViewIdx = -1;	// required, the index of the bufferView with sparse indices. Referenced bufferView can't have ARRAY_BUFFER or ELEMENT_ARRAY_BUFFER target
			int byteOffset = 0;		// min 0, default 0
			glDataType componentType = glDataType::FLOAT;	// required

		} indices;

		struct
		{
			int bufferViewIdx = -1;	// required
			int byteOffset = 0;
		} values;
	};

	struct glAccessor
	{
		std::string name;
		int bufferViewIdx = 0;	// the index of the buffer View, when not defined, accessor must be initialized with 0s, 'sparse' property or extensions could override 0s with actual values
		int byteOffset = 0;
		glDataType componentType = glDataType::FLOAT;	// required, the datatype of components in the attribute
		glType type = glType::UNKNOWN;	// required, specifies if the attribute is a scalar, vector or matrix
		int count = 0;	// required
		bool normalized = false;	// optional, specifies whether integer data values should be normalized
									// this property is defined only for accessors that contain vertex attributes or animation output data
		// min max - 用float数组替代
		float min[16];	// max value of each component in this attribute, maxItems = 16
		float max[16];	// min value of each component in this attribute, maxItems = 16

		glSparseData sparseData;
	};

	/// materials
	// each mesh primitive may refer to one of the materials that are contained in a glTF asset.
	// the default material model is the Metallic-Roughness-Model. Values between 0.0 and 1.0 are 
	// used to describe how much the material characteristics resemble that of a metal, and how rough
	// the surface of the object is.
	struct glTexureInfo
	{
		int index = -1;		// 默认为空 the index of the texture
		int texCoord = 0;	// the set index of texture's TEXCOORD attribute used for texture coordinate mapping
	};
	struct glNormalTexInfo
	{
		int index = -1;
		int texCoord = 0;
		float scale = 1.0f;	// the scalar multiplier applied to each normal vector of the normal texture
		// scaled normal = normalize( (<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0) )
	};
	struct glOcclusionTexInfo
	{
		int index = -1;
		int texCoord = 0;
		float strength = 1.0f;	// 0.0 - 1.0
	};
	// metallic-roughness material model
	struct glPbrMetallicRoughness
	{
		float baseColorFactor[4] = { 1, 1, 1, 1 };	// default [1,1,1,1]
		glTexureInfo baseColorTex;
		float metallic = 1.0f;
		float roughness = 1.0f;
		glTexureInfo metallicRoughnessTex;	// the metallic-roughness texture, B - metalness, G - roughness
			// The metalness values are sampled from the B channel. The roughness values are sampled
			// from the G channel. These values are linear. If other channels are present (R or A),
			// they are ignored for metallic-roughness calculations."
	};
	struct glMaterial
	{
		std::string name;

		glPbrMetallicRoughness metallicRoughnessTex;
		glNormalTexInfo normalTex;	// a tangent space normal map
			// The texture contains RGB components in linear space. Each texel represents the XYZ components of 
			// a normal vector in tangent space. Red [0 to 255] maps to X [-1 to 1]. 
			// Green [0 to 255] maps to Y [-1 to 1]. Blue [128 to 255] maps to Z [1/255 to 1]. 
			// The normal vectors use OpenGL conventions where +X is right and +Y is up. +Z points toward the viewer. In GLSL, this vector would be unpacked like so: `float3 normalVector = tex2D(<sampled normal map texture value>, texCoord) * 2 - 1`. Client implementations should normalize the normal vectors before using them in lighting equations."

		glOcclusionTexInfo occlusionTex;	// the occlusion map texture, R channel
			// The occlusion values are sampled from the R channel. Higher values indicate areas that 
			// should receive full indirect lighting and lower values indicate no indirect lighting.
			// These values are linear. If other channels are present (GBA), they are ignored for
			// occlusion calculations."

		glTexureInfo emissvieTex;	// The emissive map controls the color and intensity of the light being 
			// emitted by the material. This texture contains RGB components encoded with the sRGB transfer 
			// function. If a fourth component (A) is present, it is ignored.
		float emissiveFactor[3] = { 0, 0, 0 };	// default [0, 0, 0]

		// the alpha rendering mode of the material
		std::string strAlphaMode;	// default "OPAQUE"
		glAlphaMode eAlphaMode = glAlphaMode::kOPAQUE;
		float alphaCutoff = 0.5f;	// specifies the cutoff threshold when in `MASK` mode
		bool doubleSided = false;	// specifies wehter the material is double sided.
			// when this value is false, back-face is enabled. When this value is true, back-face culling is disabled and
			// double sided lighting is enabled. The back-face must have its normals reversed before the lighting equation
			// is evaluated.

	};

	// image
	struct glImage
	{
		std::string name;
		std::string uri;	// relative paths are relative to the .gltf file. The image format must be jpg or png
		std::string mimeType;	// the image's MIME type (required if `bufferView` is defined) ["image/jpeg", "image/png"]
		int bufferViewIdx = -1;		// (required if no uri)
	};

	// sampler
	struct glSampler
	{
		std::string name;
		glTextureFilter magFilter = glTextureFilter::LINEAR;	// NEAREST or LINEAR
		glTextureFilter minFilter = glTextureFilter::LINEAR_MIPMAP_LINEAR;	// 
		glTextureWrapMode wrapS = glTextureWrapMode::REPEAT;
		glTextureWrapMode wrapT = glTextureWrapMode::REPEAT;
	};

	// a texture and its sampler
	struct glTexture
	{
		std::string name;
		int samplerIdx = -1;	// the index of the sampler used by this texture. When undefined,
		// a sampler with repeat wrapping and auto filtering should be used

		int sourceIdx = -1;	// the index of the image used by this texture. when undefined,
		// it is expected that an extension or other mechanism will supply an alternate texture source,
		// otherwise behavior is undefined
	};

	DXGI_FORMAT GetDXFormat(glDataType componentType, glType type, bool normalized = true);
}

namespace glTF
{
	struct BoundingBox
	{
		Vector3 min;
		Vector3 max;
	};

	struct Mesh
	{
		int vertexCount = 0;
		int vertexDataByteOffset = 0;
		int vertexStride = 0;
		uint32_t enabledAttribs = 0;
		vAttribute attribs[Attrib::maxAttrib];

		int indexCount = 0;
		int indexDataByteOffset = 0;
		int indexAccessor = -1;

		int materialIndex = -1;
		int nodeIndex = 0;	// 所属节点
		BoundingBox boundingBox;
	};

	struct Material
	{
		static const uint32_t TextureNum = 5;

		// properties
		float baseColorFactor[4] = { 1, 1, 1, 1 };
		float metallic = 1.0f;
		float roughness = 1.0f;

		float normalScale = 1.0f;
		float occlusionStrength = 1.0f;

		float emissiveFactor[3] = { 0 };
		float alphaCoutoff = 0.5f;
		
		uint32_t texcoords[8] = { 0 };		// 贴图对应的uv (uv0, uv1, ...)

		// textures
		std::string texBaseColorPath;
		std::string texMetallicRoughnessPath;
		std::string texNormalPath;
		std::string texOcclusionPath;
		std::string texEmissivePath;

		// settings
		glAlphaMode eAlphaMode = glAlphaMode::kOPAQUE;
		bool doubleSided = false;

	};

#pragma region FilePath
	inline std::string GetBaseDir(const std::string& filePath)
	{
		// find_last_of - Searches the string for the last character that matches any of the characters specified in its arguments.
		size_t rpos = filePath.find_last_of("/\\");
		if (rpos != std::string::npos)
			return filePath.substr(0, rpos+1);
		return "";
	}
	inline std::string GetBaseFileName(const std::string& filePath)
	{
		return filePath.substr(filePath.find_last_of("/\\") + 1);
	}	
	inline std::string GetFileNameWithNoExtensions(const std::string filePath)
	{
		std::string baseFileName = filePath.substr(filePath.find_last_of("/\\") + 1);
		return baseFileName.substr(0, baseFileName.rfind('.'));
	}
#pragma endregion


}
