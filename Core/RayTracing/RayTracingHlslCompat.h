#ifndef RAYTRACING_HLSL_COMPAT_INCLUDED
#define RAYTRACING_HLSL_COMPAT_INCLUDED

#ifndef HLSL
#include "../HlslCompat.h"
#endif

struct RayTraceMeshInfo
{
    uint  IndexOffsetBytes;
    uint  UVAttributeOffsetBytes;
    uint  NormalAttributeOffsetBytes;
    uint  TangentAttributeOffsetBytes;
    uint  BitangentAttributeOffsetBytes;
    uint  PositionAttributeOffsetBytes;
    uint  AttributeStrideBytes;
    uint  MaterialInstanceId;
};

#endif // RAYTRACING_HLSL_COMPAT_INCLUDED
