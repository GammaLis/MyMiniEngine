#pragma once
#include "Camera.h"

namespace MyDirectX
{
	class ShadowCamera : public Math::BaseCamera
	{
	public:
		ShadowCamera() {}

		void UpdateMatrix(
			Math::Vector3 lightDirection,	// direction parallel to light, in direction of travel
			Math::Vector3 shadowCenter,		// center location on far bounding plane of shadowed region
			Math::Vector3 shadowBounds,		// width, height, and depth in world space represented by the shadow buffer
			uint32_t bufferWidth,			// shadow buffer width
			uint32_t bufferHeight,			// shadow buffer height - usually same as width
			uint32_t bufferPrecision		// bit depth of shadow buffer - usually 16 or 24
		);

		// used to transform world space to texture space for shadow sampling
		const Math::Matrix4 GetShadowMatrix() const { return m_ShadowMatrix; }

	private:
		Math::Matrix4 m_ShadowMatrix;
	};

}