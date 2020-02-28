#include "ParticleShaderStructs.h"

using namespace MyDirectX::ParticleEffects;

EmissionProperties* MyDirectX::ParticleEffects::CreateEmissionProperties()
{
	EmissionProperties* emitProps = new EmissionProperties;
	ZeroMemory(emitProps, sizeof(*emitProps));

	emitProps->EmitPosW = emitProps->LastEmitPosW = XMFLOAT3(0.0f, 0.0f, 0.0f);
	emitProps->EmitDirW = XMFLOAT3(0.0f, 0.0f, 1.0f);
	emitProps->EmitRightW = XMFLOAT3(1.0f, 0.0f, 0.0f);
	emitProps->EmitUpW = XMFLOAT3(0.0f, 1.0f, 0.0f);

	emitProps->Restitution = 0.6f;
	emitProps->FloorHeight = -0.7f;
	emitProps->EmitSpeed = 1.0f;
	emitProps->Gravity = XMFLOAT3(0.0f, -5.0f, 0.0f);
	emitProps->MaxParticles = 500;

	return emitProps;
}
