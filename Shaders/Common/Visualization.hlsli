#ifndef VISUALIZATION_INCLUDED
#define VISUALIZATION_INCLUDED

// Ref: UE - Hash.ush

uint MurmurAdd(uint Hash, uint Element)
{
	Element *= 0xcc9e2d51;
	Element = (Element << 15) | (Element >> (32 - 15));
	Element *= 0x1b873593;

	Hash ^= Element;
	Hash = (Hash << 13) | (Hash >> (32 - 13));
	Hash = Hash * 5 + 0xe6546b64;
	return Hash;
}

uint MurmurMix(uint Hash)
{
	Hash ^= Hash >> 16;
	Hash *= 0x85ebca6b;
	Hash ^= Hash >> 13;
	Hash *= 0xc2b2ae35;
	Hash ^= Hash >> 16;
	return Hash;
}

// Ref:  UE - Visualization.ush

float3 IntToColor(uint Index)
{
	uint Hash = MurmurMix(Index);

	float3 Color = float3
	(
		(Hash >> 0) & 255,
		(Hash >> 8) & 255,
		(Hash >> 16) & 255
	);

	return Color * (1.0f / 255.0f);
}

float3 HUEtoRGB(in float H)
{
	float R = abs(H * 6 - 3) - 1;
	float G = 2 - abs(H * 6 - 2);
	float B = 2 - abs(H * 6 - 4);
	return saturate(float3(R, G, B));
}

float3 HSVtoRGB(in float3 HSV)
{
	float3 RGB = HUEtoRGB(HSV.x);
	return ((RGB - 1) * HSV.y + 1) * HSV.z;
}

float3 GetColorCode(float x)
{
	float c = (1 - saturate(x)) * 0.6; // Remap [0,1] to Blue-Red
	return x > 0 ? HSVtoRGB(float3(c, 1, 1)) : float3(0, 0, 0);
}

float3 GreenToRedHUE(float s)
{
	return HUEtoRGB(lerp(0.333333f, 0.0f, saturate(s)));
}

#endif // VISUALIZATION_INCLUDED
