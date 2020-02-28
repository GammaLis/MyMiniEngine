
cbuffer EmissionProperties	: register(b2)
{
	float3 _LastEmitPosW;
	float _EmitSpeed;
	float3 _EmitPosW;
	float _FloorHeight;
	float3 _EmitDirW;
	float _Restitution;		// 反弹系数
	float3 _EmitRightW;
	float _EmitterVelocitySensitivity;
	float3 _EmitUpW;
	uint _MaxParticles;
	float3 _Gravity;
	uint _TextureID;
	float3 _EmissiveColor;
	// float padding;
	uint4 _RandIndex[64];
};

struct ParticleSpawnData
{
	float ageRate;
	float rotationSpeed;
	float startSize;
	float endSize;
	float3 velocity;
	float  mass;
	float3 spreadOffset;
	float random;
	float4 startColor;
	float4 endColor;
};

struct ParticleMotion
{
	float3 position;
	float mass;
	float3 velocity;
	float age;
	float rotation;
	uint resetDataIndex;
};

struct ParticleVSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv	: TEXCOORD0;
	nointerpolation uint texId 		: TEXCOORD1;
	nointerpolation float4 color 	: TEXCOORD2;
	nointerpolation float linearZ	: TEXCOORD3;
};
