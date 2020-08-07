#ifndef VOXELUTILITY_HLSLI
#define VOXELUTILITY_HLSLI

#define GRID_SIZE 128

// normalized directions of 4 faces of a regular tetrahedron
static const float3 faceVectors[4] = 
{
	float3(0.0f, -0.57735026f,  0.81649661f),
	float3(0.0f, -0.57735026f, -0.81649661f),
  	float3(-0.81649661f, 0.57735026f, 0.0f),
	float3( 0.81649661f, 0.57735026f, 0.0f) 
};

// get index into the grid for the specified position
uint GetGridIndex(uint3 gridPos, uint gridRes = GRID_SIZE)
{
	return (gridPos.z * gridRes * gridRes + gridPos.y * gridRes + gridPos.x);
}

// encode specified color (range 0.0f-1.0f), so that each channel is stored in 8 bits of an unsigned integer
uint EncodeColor(float3 color)
{
	uint3 iColor = uint3(color * 255.0f);
	uint colorMask = ((iColor.r & 0xff) << 16u) | ((iColor.g & 0xff) << 8u) | (iColor.b & 0xff);
	return colorMask;
}

// decode specified mask into a float3 color (range 0.0f-1.0f)
float3 DecodeColor(uint colorMask)
{
	float3 color;
	color.r = float((colorMask >> 16) & 0xff);
	color.g = float((colorMask >> 8 ) & 0xff);
	color.b = float(colorMask & 0xff);
	color /= 255.0f;
	return color;
}

// encode specified normal (normalized) into an unsigned integer. Each axis of 
// the normal is encoded into 9 bits (1 for the sign / 8 for the value)
uint EncodeNormal(float3 normal)
{
	int3 iNormal = int3(normal * 255.0f);
	uint3 iNormalSigns;
	iNormalSigns.x = (iNormal.x >>  5) & 0x04000000;
	iNormalSigns.y = (iNormal.y >> 14) & 0x00020000;
	iNormalSigns.z = (iNormal.z >> 23) & 0x00000100;
	iNormal = abs(iNormal);
	uint normalMask = (iNormalSigns.x) | (iNormal.x << 18) | (iNormalSigns.y) | (iNormal.y << 9) | 
		(iNormalSigns.z) | (iNormal.z);
	return normalMask;
}

// decode specified mask into a float3 normal (normalized)
float3 DecodeNormal(uint normalMask)
{
	int3 iNormal;
	iNormal.x = (normalMask >> 18) & 0xff;
	iNormal.y = (normalMask >>  9) & 0xff;
	iNormal.z = normalMask & 0xff;
	int3 iNormalSigns;
	iNormalSigns.x = (normalMask >> 25) & 0x02;
	iNormalSigns.y = (normalMask >> 16) & 0x02;
	iNormalSigns.z = (normalMask >>  7) & 0x02;
	iNormalSigns = 1 - iNormalSigns;
	float3 normal = float3(iNormal) / 255.0f;
	normal *= iNormalSigns;
	return normal;
}

// determine which of the 4 specified normals (encoded as normalMask) is cloest to 
// the specified direction. The function returns the cloest normal and as output parameter
// the corresponding dot product
float3 GetCloestNormal(uint4 normalMask, float3 direction, out float dotProduct)
{
	float4x3 normalMatrix;
	normalMatrix[0] = DecodeNormal(normalMask.x);
	normalMatrix[1] = DecodeNormal(normalMask.y);
	normalMatrix[2] = DecodeNormal(normalMask.z);
	normalMatrix[3] = DecodeNormal(normalMask.w);
	
	float4 dotProducts = mul(normalMatrix, direction);
	float maximum = max(max(dotProducts.x, dotProducts.y), max(dotProducts.z, dotProducts.w));
	uint maxComp;
	if (maximum == dotProducts.x)
		maxComp = 0;
	else if (maximum == dotProducts.y)
		maxComp = 1;
	else if (maximum == dotProducts.z)
		maxComp = 2;
	else 
		maxComp = 3;

	dotProduct = dotProducts[maxComp];
	return normalMatrix[maxComp];
}

uint GetNormalIndex(float3 normal, out float dotProduct)
{
	float4x3 faceMatrix;
	faceMatrix[0] = faceVectors[0];
	faceMatrix[1] = faceVectors[1];
	faceMatrix[2] = faceVectors[2];
	faceMatrix[3] = faceVectors[3];

	float4 dotProducts = mul(faceMatrix, normal);
	float maximum = max(max(dotProducts.x, dotProducts.y), max(dotProducts.z, dotProducts.w));
	uint index;
	if (maximum == dotProducts.x)
		index = 0;
	else if (maximum == dotProducts.y)
		index = 1;
	else if (maximum == dotProducts.z)
		index = 2;
	else 
		index = 3;

	dotProduct = dotProducts[index];
	return index;
}

#endif	// VOXELUTILITY_HLSLI

/**
 * 	GPU Pro4 - 7. Rasterized Voxel-Based Dynamic Global Illumination
 *  	http://hd-prg.com/RVGlobalIllumination.html
 */
