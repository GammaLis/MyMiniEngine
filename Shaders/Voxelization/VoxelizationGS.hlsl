
// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)

cbuffer CBConstants	: register(b0)
{
	uint _VoxelGridSize;
	float3 _Constants;
};
cbuffer CBPerCamera	: register(b1)
{
	matrix _OrthoProjMat;
	float3 _CamPos;
};
cbuffer CBMiscs	: register(b3)
{
	uint _VoxelGridRes;		// 256
	float3 _VoxelGridCenterPos;	// grid center potisiton
	float3 _RcpVoxelSize;	// 1.0 / voxelSize, voxelSize = gridExtent / gridRes
};

struct VSOutput
{
	// float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;

	uint drawId	: DRAWID;
};

struct GSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;

	uint drawId : DRAWID;
};

[maxvertexcount(3)]
void main(
	triangle VSOutput input[3], 
	inout TriangleStream< GSOutput > output
)
{
	// normal
	float3 shNormal = normalize(input[0].normal + input[1].normal + input[2].normal);

	float3 e1 = normalize(input[1].worldPos - input[0].worldPos);
	float3 e2 = normalize(input[2].worldPos - input[0].worldPos);
	float3 geoNormal = cross(e1, e2);

	float3 normal = abs(shNormal);

	uint maxComp = 2;
	maxComp = normal[0] > normal[1] ? 0 : 1;
	maxComp = normal[2] > normal[maxComp] ? 2 : maxComp;

	GSOutput elements[3];
	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		elements[i].pos = 1.0;
		elements[i].pos.xyz = (input[i].worldPos - _VoxelGridCenterPos);

		// project onto dominant axis
		if (maxComp == 0)
			elements[i].pos.xyz = elements[i].pos.zyx;	// elements[i].pos.yzx
		else if (maxComp == 1)
			elements[i].pos.xyz = elements[i].pos.xzy;

		// projected pos
		float3 gridExtent = _VoxelGridRes / _RcpVoxelSize;
		elements[i].pos.xy /= (gridExtent.xy / 2.0);
		elements[i].pos.z = 1.0;	// don't care Z

		// it is important to pass the vertices' world position to the pixel shader,
		// because we will use that directly to index into the voxel grid data structure and
		// write into it. We will also need texture coordinates and normals for correct 
		// diffuse color and lighting
		elements[i].worldPos = input[i].worldPos;
		elements[i].uv0 = input[i].uv0;
		elements[i].normal = input[i].normal;
		elements[i].tangent = input[i].tangent;
		elements[i].bitangent = input[i].bitangent;
	}

	// conservative rasterization 
	// increase the size of triangle in normalized device coordinates by 
	// the texel size of the currently bound render-target
	float2 side0N = normalize(elements[1].pos.xy - elements[0].pos.xy);
	float2 side1N = normalize(elements[2].pos.xy - elements[1].pos.xy);
	float2 side2N = normalize(elements[0].pos.xy - elements[2].pos.xy);
	
	float texelSize = 1.0 / _VoxelGridRes;
	elements[0].pos.xy += normalize(-side0N + side2N) * texelSize;
	elements[1].pos.xy += normalize(-side1N + side0N) * texelSize;
	elements[2].pos.xy += normalize(-side2N + side1N) * texelSize;

	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		output.Append(elements[i]);
	}
	output.RestartStrip();
}

/**
 * 	https://wickedengine.net/2017/08/30/voxel-based-global-illumination/
 *  
 *  GPU Pro4 - 7. Rasterized Voxel-Based Dynamic Global Illumination
 *  	http://hd-prg.com/RVGlobalIllumination.html
 */

/**
 * 	https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-geometry-shader
 * 	Geometry-Shader Object
 * 	a geometry-shader object processes entire primitives. 
 * 	[maxvertexcount(NumVerts)]
 * 	void ShaderName (
 * 		PrimitiveType DataType Name [NumElements],
 * 		inout StreamOutputObject)
 *
 * 	Params:
 * 	[maxvertexcount(NumVerts)]
 * 		[in] declaration for the maximum number of vertices to create
 * 		- [maxvertexcount] - required keyword
 * 		- NumVerts - an integer number representing the number of vertices
 * 	ShaderName
 * 		PrimitiveType DataType Name[NumElements] 	
 * 		PrimitiveType - primitive type, which determines the order of the primitive data
 * 		point		- point list
 * 		line		- line list or line strip
 * 		triangle 	- triangle list or triangle strip
 * 		lineadj		- line list with adjacency or line strip with adjacency
 * 		triangleadj	- triangle list with adjacency or triangle with adjacency
 *
 * 		DataType - [in] an input data type, can be any HLSL data type
 * 		NumElements - array size of the input, which depends in the PrimitiveType 
 * 		point		- [1] you operate on only one point at a time
 * 		line 		- [2] a line requires 2 vertices
 * 		triangle 	- [3] a triangle requires 3 vertices
 * 		lineadj 	- [4] a lineadj has 2 ends, therefore it requires 4 vertices
 * 		triangleadj - [6] a triangle borders 3 more triangle, therefore it requries 6 vertices
 */

/**
 * 	https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-so-type
 * 	Stream-Output Object
 * 	a stream-output object is a templated object that streams data out of the geometry-shader stage.
 * 	inout StreamOutputObject<DataType> Name;
 *
 * 	Stream-Output Object Types
 * 	PointStream		a sequence of point primitives
 * 	LineStream		a sequence of line primitives
 * 	TriangleStream	a sequence of triangle primitives
 *
 * 	Methods:
 * 		Append 			- append output data to an existing stream
 * 		RestartStrip 	- end the current primitive strip and start a new primitive strip * 
 */

/**
 * 	https://docs.microsoft.com/en-us/windows/win32/direct3d11/geometry-shader-stage
 * 	Geometry Shader Stage
 * 	the geometry-shader (GS) stage runs application-specified shader code with vertices as input
 * and the ability to generate vertices on output.
 *
 * 	The Geometry Shader
 * 	unlike vertex shaders, which operate on a single vertex, the geometry shader's inputs are 
 * the vertices for a full primitive (2 vertices for lines, 3 vertices for triangle or signle vertex
 * for point.) 
 * 	The geometry-shader stage can consume the SV_PrimitiveID system-generated value that is auto-
 * generated by the IA. This allows per-primitive data to be fetched or computed if desired.
 * 	
 * 	The geometry-shader stage is capable of outputting multiple vertices forming a single selected
 * topology (GS stage output topologies available are: tristrip, linestrip, and pointlist). The number
 * of primitives emitted can vary freely within any invocation of the geometry shader, though the maximum
 * number of vertices that could be emitted must be declared statically. Strip emitted from a geometry
 * shader invocation can be arbitrary, and new strips can be created via the `RestartStrip` HLSL function.
 *
 * 	Geometry shader output may be fed to the rasterizer stage and/or to a vertex buffer in memory via
 * the streamoutput stage. Output fed to memory is expanded to individual point/line/triangle lists(exactly
 * as they would be passed to the rasterizer).
 *
 * 	A geometry shader outputs data one vertex at a time by appending vertices to an output stream object.
 * The topology of the stream is determined by a fixed declaration, choosing one of: PointStream, LineStream,
 * or TriangleStream as the output of the GS stage. There are 3 types of stream objects available,
 * `PointStream`, `LineStream`, `TriangleStream` which are all templated objects.		Execution of a geometry
 * shader instance is atomic from other invocations, except that data added to the streams is serial.
 * The outputs of a given invocation of a geometry shader are independent of other invocations (though
 * ordering is respected). A geometry shader generating triangle strips will start a ne strip on every
 * invocation.
 *
 * 	When a geometry shader output is identified as a System Interpreted Value (e.g. SV_RenderTargetArrayIndex
 * or SV_Position), hardware looks at this data and performs some behavior dependent on the value, in addtion
 * to being able to pass the data itself to the next shader stage for input.
 * 	The geometry shader can perform load and texture sampling operations when screen-space derivatives
 * are not required (sampleLevel, sampleCmpLevelZero, sampleGrad)
 * 	Algorithms that can be implemented in the geometry shader include:
 * 		Point Sprite Expansion
 * 		Dyanmic Particle Systems
 * 		Fur/Fin Generation
 * 		Shader Volume Generation
 * 		Single Pass Render-to-Cubemap
 * 		Per-Primitive Material Swapping
 * 		...
 */
