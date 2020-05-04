#include "CommonCullingRS.hlsli"
#define GroupSize 1024

ByteAddressBuffer _InputCounter	: register(t1);

RWByteAddressBuffer OutputArgs	: register(u0);

[RootSignature(Culling_RootSig)]
[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint count = _InputCounter.Load(0);
	uint groupX = (count + GroupSize - 1) / GroupSize;
	OutputArgs.Store3(0, uint3(groupX, 1, 1));
}
