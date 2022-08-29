#include "Reservoir.hlsl"

[numthreads(64, 1, 1)]
void main( uint dtid : SV_DispatchThreadID )
{
	RWReservoirBuffer[dtid] = CreateReservoir();
}
