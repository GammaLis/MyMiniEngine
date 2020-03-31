#ifndef SHBASICS_H
#define SHBASICS_H

#ifndef HLSL
#include "pch.h"
#define GLM_FORCE_CTOR_INIT
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;
using uint = glm::uint;
using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
#endif

namespace SH
{
	static constexpr float Pi = Math::Pi;
	static constexpr float OneOverPi = 1.0f / Pi;
	static constexpr float SqrtPi = 1.77245385f;
	static constexpr float OneOverSqrtPi = 1.0f / SqrtPi;
	static constexpr float SqrtTwo = 1.41421356f;

	/**
		SH
		0 - 1 / (2 * sqrt(pi))
		1 - -sqrt(3)  / (2 * sqrt(pi)) * y
			 sqrt(3)  / (2 * sqrt(pi)) * z
			-sqrt(3)  / (2 * sqrt(pi)) * x
		2 -  sqrt(15) / (2 * sqrt(pi)) * xy
			-sqrt(15) / (2 * sqrt(pi)) * yz
			 sqrt(5)  / (4 * sqrt(pi)) * (3z^2 - 1)
			-sqrt(15) / (2 * sqrt(pi)) * xz
			 sqrt(15) / (4 * sqrt(pi)) * (x^2 - y^2)
	*/

	class float5
	{
		float v[5];

	public:
		float5() = default;
		constexpr float5(float a, float b, float c, float d, float e) : v{ a, b, c, d, e } {}
		constexpr float operator[](size_t i) const { return v[i]; }
		float& operator[](size_t i) { return v[i]; }
	};
	static inline const float5 Multiply(const float5 mat[5], float5 x) noexcept
	{
		return float5{
			mat[0][0] * x[0] + mat[1][0] * x[1] + mat[2][0] * x[2] + mat[3][0] * x[3] + mat[4][0] * x[4],
			mat[0][1] * x[0] + mat[1][1] * x[1] + mat[2][1] * x[2] + mat[3][1] * x[3] + mat[4][1] * x[4],
			mat[0][2] * x[0] + mat[1][2] * x[1] + mat[2][2] * x[2] + mat[3][2] * x[3] + mat[4][2] * x[4],
			mat[0][3] * x[0] + mat[1][3] * x[1] + mat[2][3] * x[2] + mat[3][3] * x[3] + mat[4][3] * x[4],
			mat[0][4] * x[0] + mat[1][4] * x[1] + mat[2][4] * x[2] + mat[3][4] * x[3] + mat[4][4] * x[4],
		};
	}

	// n! / d!
	float Factorial(size_t n, size_t d = 1);

	inline size_t SHIndex(int m, size_t l)
	{
		return size_t(int(l * (l + 1)) + m);
	}

	// SH scaling factors:
	//	return sqrt((2*l + 1) / 4pi) * sqrt( (l-|m|)! / (l+|m|)! )
	float Kml(int m, size_t l);

	std::vector<float> Ki(size_t numBands);

	constexpr float ComputeTruncatedCosSH(size_t l);

	/*
	 * Calculates non-normalized SH bases, i.e.:
	 *  m > 0, cos(m*phi)   * P(m,l)
	 *  m < 0, sin(|m|*phi) * P(|m|,l)
	 *  m = 0, P(0,l)
	 */
	void ComputeSHBasis(float* SHb, size_t numBands, const float3& s);

	// this computes the 3-bands SH coefficients of the Cubemap convoluted by the truncated cos(theta) (i.e.:saturate(s.z)),
	// pre-scaled by the reconstruction factors
	void PreprocessSHForShader(std::unique_ptr<float3[]>& SH);

	// utilities to rotate very low order spherical harmonics (up to 3rd band)
	// band1
	float3 RotateSphericalHarmonicBand1(float3 band1, float3x3 const& mat);
	// band2
	float5 RotateSphericalHarmonicBand2(float5 const& band2, mat3 const& mat);

	// window
	float SincWindow(size_t l, float w);

	void WindowSH(std::unique_ptr<float3[]>& sh, size_t numBands, float cutoff);

	// debug
	float Legendre(int m, size_t l, float x);

}

#endif	// SHBASICS_H
