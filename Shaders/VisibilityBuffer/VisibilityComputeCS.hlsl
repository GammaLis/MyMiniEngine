// Ref:
// TheForge - visibilityBuffer_shade.frag.fsl

#define SHADER_CS

#include "VisibilityBufferCommon.hlsli"

#define GROUP_SIZE 8

// 2D interpolation results for texture gradient values
struct GradientInterpolationResults
{
	float2 interp;
	float2 dx;
	float2 dy;
};

// Barycentric coordinates and gradients
struct BarycentricDeriv
{
	float3 m_lambda;
	float3 m_ddx;
	float3 m_ddy;
};

#if PACK_UNORM
#define VB_FORMAT Texture2D<float4>
#else
#define VB_FORMAT Texture2D<uint>
#endif

StructuredBuffer<FVertex> _VertexBuffer 	: register(t4, space1);
ByteAddressBuffer _Indices 		: register(t5, space1);

Texture2D<float> _DepthBuffer 	: register(t1);
VB_FORMAT _VisibilityBuffer 	: register(t7);

Texture2D _MaterialTextures[]	: register(t0, space2);	// unbounded t(4)~t(+Inf)

RWTexture2D<float4> RWColorBuffer;

uint3 Load3x16BitIndices(uint offsetBytes)
{
	const uint dwordAlignedOffset = offsetBytes & ~3;
	const uint2 four16BitIndices = _Indices.Load2(dwordAlignedOffset);

	uint3 indices;

	if (dwordAlignedOffset == offsetBytes)
	{
		indices.x = four16BitIndices.x & 0xFFFF;
		indices.y = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.z = four16BitIndices.y & 0xFFFF;
	}
	else
	{
		indices.x = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.y = four16BitIndices.y & 0xFFFF;
		indices.z = (four16BitIndices.y >> 16) & 0xFFFF;
	}

	return indices;
}

FVertex FetchIntersectionVertex(uint vertexIndex)
{
	return _VertexBuffer[vertexIndex];
}

// Calculates the local (barycentric coordinates) position of a ray hitting a triangle (Muller-Trumbore algorithm)
// p0, p1, p2 - world space coordinates of triangle
// ro - origin of ray in world space (Mainly view camera here)
// rd - unit vector direction of ray from origin
float3 RayTriangleIntersection(float3 p0, float3 p1, float3 p2, float3 ro, float3 rd)
{
	float3 v0v1 = p1-p0;
	float3 v0v2 = p2-p0;
	float3 pvec = cross(rd,v0v2);
	float det = dot(v0v1,pvec);
	float invDet = 1/det;
	float3 tvec = ro - p0;
	float u = dot(tvec,pvec) * invDet;
	float3 qvec = cross(tvec,v0v1);
	float v = dot(rd,qvec) *invDet;
	float w = 1.0f - v - u;
	return float3(w,u,v);
}

// Computes the partial derivatives of a triangle from the projected screen space vertices
BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	BarycentricDeriv ret;

	float3 invW = 1.0f / float3(pt0.w, pt1.w, pt2.w);
	// Project points on screen to calculate post projection positions in 2D
	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.x;
	float2 ndc2 = pt2.xy * invW.x;

	// Computing partial derivatives and prospective correct attribute interpolation with barycentric coordinates
	// Equation for calculation taken from Appendix A of DAIS paper:
	// https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf
	
	// Calculating inverse of determinant(rcp of area of triangle)
	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));

	// Determining the partial derivatives
	// ddx[i] = (y[i+1] - y[i-1]) / Determinant
	ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	// Sum of the partial derivatives
	float ddxSum = dot(ret.m_ddx, float3(1,1,1));
	float ddySum = dot(ret.m_ddy, float3(1,1,1));

	// Delta vector from pixel's screen position to vertex 0 of the triangle
	float2 deltaVec = pixelNdc - ndc0;

	// Calculating interpolated W at point
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);
	// The barycentric coordinate (m_lambda) is determined by perspective-correct interpolation
	// Equation taken from DAIS paper
	ret.m_lambda.x = interpInvW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
	ret.m_lambda.y = interpInvW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
	ret.m_lambda.z = interpInvW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

	// Scaling from NDC to pixel units
	ret.m_ddx *= (2.0f/winSize.x);
	ret.m_ddy *= (2.0f/winSize.y);
	ddxSum 	  *= (2.0f/winSize.x);
	ddySum 	  *= (2.0f/winSize.y);

	ret.m_ddy *= -1.0f;
	ddySum 	  *= -1.0f;

	// This part fixes the derivatives error for the projected triangles
	// Instead of calculating the derivatives constantly across the 2D triangle we use a projected version
	// of the gradients, this is more accurate and closely matches GPU raster behavior
	// Final gradient equation: ddx = (((lambda/w) + ddx) / (w + |ddx|)) - lambda
	
	// Calculating interpW at partial derivatives position sum
	float interpW_ddx = 1.0f / (interpInvW + ddxSum);
	float interpW_ddy = 1.0f / (interpInvW + ddySum);

	// Calculating perspective projected derivatives
	ret.m_ddx = interpW_ddx * (ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
	ret.m_ddy = interpW_ddy * (ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;

	return ret;
}

// Helper functions to interpolate vertex attributes using derivatives

// Interpolate a float3 vector
float InterpolateWithDeriv(BarycentricDeriv deriv, float3 v)
{
	return dot(v, deriv.m_lambda);
}

// Interpolate single values over triangle vertices
float InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
	float3 mergedV = float3(v0, v1, v2);
	return dot(deriv.m_lambda, mergedV);
}

// Interpolate a float3 attribute for each vertex of the triangle
// Attribute parameters: a 3x3 matrix (row denotes attributes per vertex)
float3 InterpolateWithDeriv(BarycentricDeriv deriv, float3x3 attributes)
{
	float3 attr0 = attributes[0];
	float3 attr1 = attributes[1];
	float3 attr2 = attributes[2];
	return float3(dot(attr0, deriv.m_lambda), dot(attr1, deriv.m_lambda), dot(attr2, deriv.m_lambda));
}

// Interpolate 2D attributes using the partial derivatives and generates dx and dy for texture sampling
// Attributes parameters: a 3x2 matrix of float2 attributes (column denotes attributes per vertex)
GradientInterpolationResults Interpolate2DWithDeriv(BarycentricDeriv deriv, float3x2 attributes)
{
	GradientInterpolationResults ret;

	// TODO...

	return ret;
}

// Calculate ray differentials for a point in world-space
// Parameters: pt0,pt1,pt2 -> world space coordinates of the triangle currently visible on the pixel
// position -> world-space calculated position of the current pixel by reconstructing Z value
// positionDX,positionDY -> world-space positions a pixel footprint right and down of the calculated position w.r.t traingle
BarycentricDeriv CalcRayBary(float3 pt0, float3 pt1, float3 pt2, float3 position,float3 positionDX, float3 positionDY, float3 camPos)
{
	BarycentricDeriv ret ;

	// Calculating unit vector directions of all 3 rays
	float3 curRay = position   - camPos;
	float3 rayDX  = positionDX - camPos;
	float3 rayDY  = positionDY - camPos;
	// Calculating barycentric coordinates of each rays hitting the triangle
	float3 H  = RayTriangleIntersection(pt0,pt1,pt2,camPos,normalize(curRay));
	float3 Hx = RayTriangleIntersection(pt0,pt1,pt2,camPos,normalize(rayDX));
	float3 Hy = RayTriangleIntersection(pt0,pt1,pt2,camPos,normalize(rayDY));
	ret.m_lambda = H;
	// Ray coordinates differential
	ret.m_ddx = Hx-H;
	ret.m_ddy = Hy-H;
	return ret;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID, uint gtindex : SV_GroupIndex)
{
	const uint2 RTSize = (uint2)_View.bufferSizeAndInvSize.xy;
	if (any(dtid >= RTSize))
		return;

	const float2 pixelSS = float2(dtid.xy + 0.5);
	const float2 pixelNDC = pixelSS * _View.bufferSizeAndInvSize.zw * float2(+2.0, -2.0) + float2(-1.0, +1.0);

	// >> DEBUG
	float4 cc = 0;
#if PACK_UNORM
	float4 visRaw = _VisibilityBuffer[dtid];
	uint packedVisibility = PackUnorm4x8(visRaw);

	cc = visRaw;
#else
	uint packedVisibility = _VisibilityBuffer[dtid];
#endif
	// Unpack float4 into uint to extract data
	uint alphaBit_drawID_triID = packedVisibility;

	// Early exit if this pixel doesn't contain triangle data
	if (alphaBit_drawID_triID == ~0u)
		return;

	// Extract packed data
	const uint drawId = (alphaBit_drawID_triID >> 23) & 0x000000FF;
	const uint triId = alphaBit_drawID_triID & 0x007FFFFF;
	const uint alphaBit = (alphaBit_drawID_triID >> 31);

	MeshInstanceData meshInstance = _MeshInstanceBuffer[drawId];
	uint meshId = meshInstance.meshID;
	MeshDesc meshDesc = _MeshBuffer[meshId];

	// Fetch the indices of the current triangle
	const uint indexByteOffset = meshDesc.indexByteOffset + triId * 3 * 2; // 16-bit index
	const uint vertexOffset = meshDesc.vertexOffset;
	uint3 triangleIndices = Load3x16BitIndices(indexByteOffset) + vertexOffset;

	FVertex v0 = FetchIntersectionVertex(triangleIndices.x);
	FVertex v1 = FetchIntersectionVertex(triangleIndices.y);
	FVertex v2 = FetchIntersectionVertex(triangleIndices.z);

	// Transform vertices
	uint globalMatrixID = meshInstance.globalMatrixID;
	GlobalMatrix globalMatrix = _MatrixBuffer[globalMatrixID];
	matrix _WorldMat = globalMatrix.worldMat;
	matrix _InvWorldMat = globalMatrix.invWorldMat;

	float4 posWS0 = mul(float4(v0.position, 1.0), _WorldMat);
	float4 posWS1 = mul(float4(v1.position, 1.0), _WorldMat);
	float4 posWS2 = mul(float4(v2.position, 1.0), _WorldMat);

	float4 posHS0 = mul(posWS0, _View.viewProjMat);
	float4 posHS1 = mul(posWS1, _View.viewProjMat);
	float4 posHS2 = mul(posWS2, _View.viewProjMat);

	// Calculate the inverse of w
	float3 one_over_w = 1.0f / float3(posHS0.w, posHS1.w, posHS2.w);
	// Project vertex positions to calculate 2D post-perspective positions
	posHS0 *= one_over_w.x;
	posHS1 *= one_over_w.y;
	posHS2 *= one_over_w.z;

	// Compute partial derivatives and barycentric coordinates
	BarycentricDeriv baryDeriv = CalcFullBary(posHS0, posHS1, posHS2, pixelNDC, _View.bufferSizeAndInvSize.xy);

	// Interpolated 1/w (one_over_w) for all three vertices of the triangle
	// using the barycentric coordinates and the delta vector
	float w = 1.0 / dot(one_over_w, baryDeriv.m_lambda);

	// Reconstruct the Z value at this point performing only the necessary matrix * vector multiplication
	// operations that involve computing Z
	// TODO...
	
	// UV
	
	float2 uv0 = v0.uv0;
	float2 uv1 = v1.uv0;
	float2 uv2 = v2.uv0;

	uv0 *= one_over_w.x;
	uv1 *= one_over_w.y;
	uv2 *= one_over_w.z;

	float3 uv_x = float3(uv0.x, uv1.x, uv2.x), uv_y = float3(uv0.y, uv1.y, uv2.y);

	float2 uv   = float2(dot(uv_x, baryDeriv.m_lambda), dot(uv_y, baryDeriv.m_lambda));
	float2 uvdx = float2(dot(uv_x, baryDeriv.m_ddx)	  , dot(uv_y, baryDeriv.m_ddx));
	float2 uvdy = float2(dot(uv_x, baryDeriv.m_ddy)   , dot(uv_y, baryDeriv.m_ddy));

	uv   *= w;
	uvdx *= w;
	uvdy *= w;

	// Materials
	
	const uint materialID = meshInstance.materialID;

	MaterialData material = _MaterialBuffer[materialID];
	float4 _BaseColorFactor = material.baseColor;
	uint _Flags = material.flags;

	uint texbase = MATERIAL_TEXTURE_NUM * materialID;
	Texture2D _TexBaseColor = _MaterialTextures[texbase + 0];

	uint shadingModel = EXTRACT_SHADING_MODEL(_Flags);
	uint diffuseType = EXTRACT_DIFFUSE_TYPE(_Flags);

	// base color
	float4 baseColor = _BaseColorFactor;
	if (diffuseType == ChannelTypeTexture)
#if 1
		baseColor = _TexBaseColor.SampleGrad(s_LinearRepeatSampler, uv, uvdx, uvdy);
#else
		baseColor = _TexBaseColor.SampleLevel(s_LinearRepeatSampler, uv, 0);
#endif

	float3 color = 0;

	// debug drawId/triId
	color = IntToColor(triId);
	// debug bary
	color = baryDeriv.m_lambda;
	// debug corrected bary
	float3 correctedLambda = (one_over_w * baryDeriv.m_lambda) * w;
	color = correctedLambda;
	// debug uv
	// color = float3(uv, 0);

	if (dtid.x < RTSize.x/2)
	color = baseColor.rgb;

	// color = cc.xyz;

	RWColorBuffer[dtid] = float4(color, 1.0);
}
