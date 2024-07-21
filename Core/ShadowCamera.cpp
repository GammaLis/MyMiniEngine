#include "ShadowCamera.h"

using namespace MyDirectX;
using namespace Math;

void ShadowCamera::UpdateMatrix(const Math::Vector3 &lightDirection, 
	const Math::Vector3 &shadowCenter, 
	const Math::Vector3 &shadowBounds, 
	uint32_t bufferWidth, uint32_t bufferHeight, uint32_t bufferPrecision)
{
	SetLookDirection(lightDirection, Vector3(kZUnitVector));

	// converts world units to texel units so we can quantize the camera position to whole texel units
	Vector3 rcpDimensions = Recip(shadowBounds);
	Vector3 quantizeScale = Vector3((float)bufferWidth, (float)bufferHeight, (float)((1 << bufferPrecision) - 1)) *
		rcpDimensions;

	// 
	// recenter the camera at the quantized position
	//

	// transform to view space
	auto newShadowCenter = ~GetRotation() * shadowCenter;
	// scale to texel units, truncate fractional part, and scale back to world units
	newShadowCenter = Floor(newShadowCenter * quantizeScale) / quantizeScale;
	// transform back into world space
	newShadowCenter = GetRotation() * newShadowCenter;

	SetPosition(newShadowCenter);

	SetProjMatrix(Matrix4::MakeScale(Vector3(2.0f, 2.0f, 1.0f) * rcpDimensions));

	Update();

	// transform from clip space to texture space
	m_ShadowMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) *
		m_ViewProjMatrix;

}
