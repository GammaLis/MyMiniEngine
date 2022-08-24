
#define GROUP_SIZE 8

Texture2D _InputTexture			: register(t12);
RWTexture2D<float4> RWTexture	: register(u2);

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	RWTexture[dtid.xy] = _InputTexture[dtid.xy];
}
