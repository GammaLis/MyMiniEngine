#pragma once
#include "pch.h"
#include <cmath>

using namespace DirectX;

namespace MyDirectX
{
	namespace Light
	{
		struct MyLight
		{
			MyLight(XMFLOAT4 color = XMFLOAT4(0, 0, 0, 1))
				: mColor{ color.x, color.y, color.z, color.w }
			{  }

			MyLight(XMFLOAT3 color = XMFLOAT3(0, 0, 0))
				: MyLight(XMFLOAT4(color.x, color.y, color.z, 1.0f))
			{  }
			virtual ~MyLight() = 0 {}

			XMFLOAT4 mDirectionOrPosition = XMFLOAT4(0, 0, 0, 0);
			XMFLOAT4 mColor = XMFLOAT4(0, 0, 0, 0);

			XMFLOAT4 mAttenuation = XMFLOAT4(-1.0f, 0.0f, 0.0f, 1.0f);		// x - range
			XMFLOAT4 mSpotDirection = XMFLOAT4(0, 0, 0, 0);					// w - spotAngle

			// float mRange = -1.0f;

		};

		struct DirectionalLight : public MyLight
		{
			DirectionalLight(XMFLOAT3 direction, XMFLOAT4 color)
				: MyLight(color)
			{
				mDirectionOrPosition = XMFLOAT4(-direction.x, -direction.y, -direction.z, 0.0f);
			}

			DirectionalLight(XMFLOAT3 direction, XMFLOAT3 color)
				: DirectionalLight(direction, XMFLOAT4(color.x, color.y, color.z, 1.0f))
			{	}

			virtual ~DirectionalLight() {}
		};

		struct PointLight : public MyLight
		{
			PointLight(XMFLOAT3 position, XMFLOAT4 color, float range)
				: MyLight(color)
			{
				mDirectionOrPosition = XMFLOAT4(position.x, position.y, position.z, 1.0f);

				if (range < 0)
				{
					range = 1.0f;
				}
				mAttenuation.x = range;
				mAttenuation.y = 1.0f / (std::max(range * range, 0.00001f));
				// (1 - (d^2 * atten.y))^2

			}

			PointLight(XMFLOAT3 position, XMFLOAT3 color, float range)
				: PointLight(position, XMFLOAT4(color.x, color.y, color.z, 1.0f), range)
			{	}

			virtual ~PointLight() {}
		};

		struct SpotLight : public MyLight
		{
			SpotLight(XMFLOAT3 position, XMFLOAT4 color, float range, XMFLOAT3 spotDir, float spotAngle = 60.0f)
				: MyLight(color)
			{
				mDirectionOrPosition = XMFLOAT4(position.x, position.y, position.z, 1.0f);

				mAttenuation.x = range;
				mAttenuation.y = 1.0f / (std::max(range * range, 0.0001f));

				mSpotDirection = XMFLOAT4(-spotDir.x, -spotDir.y, -spotDir.z, spotAngle);

				// tan(ri) = 46 / 64 * tan(ro)
				float outerRad = XMConvertToRadians(spotAngle / 2.0f);
				float outerCos = cos(outerRad);
				float outerTan = tan(outerRad);
				float innerCos = cos(atan((46.0f / 64.0f) * outerTan));
				float angleRange = std::max(innerCos - outerCos, 0.01f);
				mAttenuation.z = 1.0f / angleRange;
				mAttenuation.w = -outerCos * mAttenuation.z;
				// dotSpot * atten.z + atten.w (Ä¬ÈÏatten.w = 1)
			}

			SpotLight(XMFLOAT3 position, XMFLOAT3 color, float range, XMFLOAT3 spotDir, float spotAngle = 60.0f)
				: SpotLight(position, XMFLOAT4(color.x, color.y, color.z, 1.0f), range, spotDir, spotAngle)
			{  }

			virtual ~SpotLight() {}
		};
	}	
}


