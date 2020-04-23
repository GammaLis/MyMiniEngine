#pragma once
#include "Shaders/HlslDefines.h"
#include "Shaders/MathConstants.h"

// types of light sources. used in LightData structure
enum LightType
{
	Point,			// point light source, can be a spot light if its opening angle is < 2pi
	Directional,	// directional light source
	Rect,			// quad shaped area light source
	Sphere,			// spherical area light source
	Disc,			// disc shaped area light source
};

struct LightData
{
	float3 positionOrDirection = float3(0, -1, 0);
	uint type = 0;		// 0 - directional lights, 1 - punctual lights
	float3 color = float3(1, 1, 1);	// the color of emitted light, as a linear RGB color
	float intensity = 1.0f;	// the light's brighness. The unit depends on the type of light
	float3 spotDirection = float3(0, -1, 0);
	float falloffRadius = 100.0f;	// maximum distance of influence
	float2 spotAttenScaleOffset = float2(0, 1);	// Dot(...) * scaleOffset.x + scaleOffset.y
	// or float2 spotAngles;	// x - innerAngle, y - outerAngle
	float2 padding;	// CPP里字节对齐，这里需要补齐
};

struct alignas(16) CommonLightSettings
{
	// sun light
	float3 sunDirection = float3(1.0f, 1.0f, 1.0f);
	float sunOrientation = 0.5f;	// 方位角 (-1.0, 1.0) * Pi
	float3 sunColor = float3(1.0f, 1.0f, 1.0f);
	float sunInclination = 0.5f;	// 倾斜角 ( 0.0, 1.0) * Pi/2

	// ambient 
	float3 ambientColor = float3(1.0f, 1.0f, 1.0f);
};
