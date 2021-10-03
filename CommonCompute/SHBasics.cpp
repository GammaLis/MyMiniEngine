#include "SHBasics.h"
#include <array>

namespace SH
{
	float Factorial(size_t n, size_t d)
	{
		d = std::max(size_t(1), d);
		n = std::max(size_t(1), n);
		float r = 1.0;
		if (n == d)
		{
			// intentionally left blank
		}
		else if (n > d)
		{
			for (; n > d; --n)
				r *= n;
		}
		else
		{
			for (; d > n; --d)
				r *= d;
			r = 1.0f / r;
		}
		return r;
	}

	// sqrt((2*l + 1) / 4*pi) * sqrt( (l-|m|)! / (l+|m|)! )
	float Kml(int m, size_t l)
	{
		m = std::abs(m);
		float K = (2 * l + 1) * Factorial(size_t(l - m), size_t(l + m));
		return std::sqrt(K) * (OneOverSqrtPi * 0.5f);
	}

	// 各级Kml系数
	std::vector<float> Ki(size_t numBands)
	{
		const size_t numCoefs = numBands * numBands;
		std::vector<float> K(numCoefs);
		for (size_t l = 0; l < numBands; ++l)
		{
			K[SHIndex(0, l)] = Kml(0, l);
			for (int m = 1; m <= l; ++m)
			{
				K[SHIndex(m, l)] = K[SHIndex(-m, l)] = SqrtTwo * Kml(m, l);
			}
		}
		return K;
	}

	// <cos(theta)> SH coefficients pre-multiplied by 1 / K(0, 1)
	/**
		decomposition of <cos(theta)>
		C(0, l) = 2Pi * Int(<cos(theta)> y(0, l)sin(theta), dtheta)

		C'(0, l) = 1/K0_l = 2Pi * (-1)^(l/2-1) / ((l+2)(l-1)) * (l! / 2^l *((l/2)!)^2)
		
	*/
	constexpr float ComputeTruncatedCosSH(size_t l)
	{
		if (l == 0)
			return Pi;
		else if (l == 1)
			return 2 * Pi / 3;
		else if (l & 1u)
			return 0;

		const size_t halfL = l / 2;
		float A0 = ((halfL & 1u) ? 1.0f : -1.0f) / ((l + 2) * (l - 1));
		float A1 = Factorial(l, halfL) / (Factorial(halfL) * (1 << l));
		return 2 * Pi * A0 * A1;
	}

	void ComputeSHBasis(float* SHb, size_t numBands, const float3& s)
	{
#if 0
		// reference implementation
		float phi = atan2(s.x, s.y);
		for (size_t l = 0; l < numBands; ++l)
		{
			SHb[SHIndex(0, l)] = Legendre(l, 0, s.z);
			for (size_t m = 1; m <= l; ++m)
			{
				float p = Legendre(l, m, s.z);
				SHb[SHIndex(-m, l)] = std::sin(m * phi) * p;
				SHb[SHIndex(m, l)] = std::cos(m * phi) * p;
			}
		}
#endif
		/**
			below, we compute the associated Legendre polynomials using recursion
			[1] s.z = cos(theta) => we only need to compute P(s.z)
			[2] we in fact compute P(s.z) / sin(theta)^|m|, by removing the "sqrt(1 - s.z*s.z)" [i.e.sin(theta)]
		factor from the recursion. This is later corrected in the ( cos(m*phi), sin(m*phi) ) recursion.
		*/
		// s = (x, y, z) = (cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta))
		// P(l, l, x) = (-1)^l * (2l - 1)!! * (1-x^2)^(l/2)
		// P(m, l, x) = ((2l-1)*x*P(m, l-1) - (l+m-1)*P(m, l-2)) / (l-m)
		// P(m, m+1, x) = x * (2m+1) * P(m, m, x)
		// handle m = 0 separately, since it produces only one coefficient
		float Pml_2 = 0;	// P(m, l-2)
		float Pml_1 = 1;	// P(m, l-1)	P(0, 0) = 1;
		SHb[0] = Pml_1;
		for (size_t l = 1; l < numBands; ++l)
		{
			float Pml = ((2 * l - 1.0f) * s.z * Pml_1 - (l - 1.0f) * Pml_2) / l;
			Pml_2 = Pml_1;
			Pml_1 = Pml;
			SHb[SHIndex(0, l)] = Pml;
		}
		float Pmm = 1;
		for (int m = 1; m < numBands; ++m)
		{
			Pmm = (1.0f - 2 * m) * Pmm;		// divide by sqrt(1 - s.z * s.z);	// sin(theta)
			Pml_2 = Pmm;
			Pml_1 = (2 * m + 1.0f) * Pmm * s.z;
			// l == m
			SHb[SHIndex(-m, m)] = Pmm;
			SHb[SHIndex( m, m)] = Pmm;

			if (m + 1 < numBands)
			{
				// l == m+1
				SHb[SHIndex(-m, m + 1)] = Pml_1;
				SHb[SHIndex( m, m + 1)] = Pml_1;
				for (size_t l = m + 2; l < numBands; ++l)
				{
					float Pml = ((2 * l - 1.0f) * Pml_1 * s.z - (l + m - 1.0f) * Pml_2) / (l - m);
					Pml_2 = Pml_1;
					Pml_1 = Pml;
					SHb[SHIndex(-m, l)] = Pml;
					SHb[SHIndex( m, l)] = Pml;
				}	
			}
			// -mf
			//Pml_2 = 0;
			//Pml_1 = Pmm;
			//for (size_t l = m + 1; l < numBands; ++l)
			//{
			//	float Pml = ((2 * l - 1.0f) * Pml_1 * s.z - (l + m - 1.0f) * Pml_2) / (l - m);
			//	Pml_2 = Pml_1;
			//	Pml_1 = Pml;
			//	SHb[SHIndex(-m, l)] = Pml;
			//	SHb[SHIndex( m, l)] = Pml;
			//}
		}

		/**
			at this point, SHb contains the associated Legendre polynomials divided by sin(theta)^|m|.

			( cos(m*phi), sin(m*phi) ) recursion:
			cos((m+1)*phi) = cos(m*phi)*cos(phi) - sin(m*phi)*sin(phi)
			sin((m+1)*phi) = sin(m*phi)*cos(phi) + cos(m*phi)*sin(phi)
			cos[m+1] == cos[m]*s.x - sin[m]*s.y
			sin[m+1] == sin[m]*s.x + cos[m]*s.y
			(d.x, d.y) == (cos(phi), sin(phi)) * sin(theta), so
				== (cos(m*phi), sin(m*phi)) * sin(theta)^|m|
		*/
		float Cm = s.x;
		float Sm = s.y;
		for (int m = 1; m <= numBands; ++m)	// why `<=`, not `<` ???
		{
			for (size_t l = m; l < numBands; ++l)
			{
				SHb[SHIndex(-m, l)] *= Sm;
				SHb[SHIndex( m, l)] *= Cm;

				float Cm1 = s.x * Cm - s.y * Sm;
				float Sm1 = s.x * Sm + s.y * Cm;
				Cm = Cm1;
				Sm = Sm1;
			}
		}
		// -mf
		//float Cm = 1;
		//float Sm = 0;
		//for (size_t m = 1; m < numBands; ++m)
		//{
		//	for (size_t l = m; l < numBands; ++l)
		//	{
		//		float Cm1 = s.x * Cm - s.y * Sm;
		//		float Sm1 = s.x * Sm + s.y * Cm;
		//		Cm = Cm1;
		//		Sm = Sm1;
		//		SHb[SHIndex(-m, l)] *= Sm;
		//		SHb[SHIndex( m, l)] *= Cm;
		//	}
		//}
	}

	// 
	void PreprocessSHForShader(std::unique_ptr<float3[]>& SH)
	{
		constexpr size_t numBands = 3;
		constexpr size_t numCoefs = numBands * numBands;

		// Coefficient for the polynomial form of the SH functions -- these were taken from
		// "Stupid Spherical Harmonics (SH)" by Peter-Pike Sloan
		// They simply come for expanding the computation of each SH function.
		//
		// To render spherical harmonics we can use the polynomial form, like this:
		//          c += sh[0] * A[0];
		//          c += sh[1] * A[1] * s.y;
		//          c += sh[2] * A[2] * s.z;
		//          c += sh[3] * A[3] * s.x;
		//          c += sh[4] * A[4] * s.y * s.x;
		//          c += sh[5] * A[5] * s.y * s.z;
		//          c += sh[6] * A[6] * (3 * s.z * s.z - 1);
		//          c += sh[7] * A[7] * s.z * s.x;
		//          c += sh[8] * A[8] * (s.x * s.x - s.y * s.y);
		//
		// To save math in the shader, we pre-multiply our SH coefficient by the A[i] factors.
		// Additionally, we include the lambertian diffuse BRDF 1/pi.
		constexpr float F_SQRT_PI	= 1.7724538509f;
		constexpr float F_SQRT_3	= 1.7320508076f;
		constexpr float F_SQRT_5	= 2.2360679775f;
		constexpr float F_SQRT_15	= 3.8729833462f;
		constexpr float A[numCoefs] =
		{
			// band 0
			1.0f / (2.0f * F_SQRT_PI),			// 0, 0
			// band 1
			-F_SQRT_3	/ (2.0f * F_SQRT_PI),	// 1, -1
			 F_SQRT_3	/ (2.0f * F_SQRT_PI),	// 1, 0
			-F_SQRT_3	/ (2.0f * F_SQRT_PI),	// 1, 1
			// band 2
			 F_SQRT_15	/ (2.0f * F_SQRT_PI),	// 2, -2
			-F_SQRT_15	/ (2.0f * F_SQRT_PI),	// 2, -1
			 F_SQRT_5	/ (4.0f * F_SQRT_PI),	// 2, 0
			-F_SQRT_15	/ (2.0f * F_SQRT_PI),	// 2, 1
			 F_SQRT_15	/ (4.0f * F_SQRT_PI),	// 2, 2
		};
		for (size_t i = 0; i < numCoefs; ++i)
			SH[i] *= A[i] * OneOverPi;
	}

	// http://filmicworlds.com/blog/simple-and-fast-spherical-harmonic-rotation/
	// R_3x3 * P(N) = P(M * N), A = P(N0, N1, N2)
	// R_3x3 = P(M * Ni) * invA
	// R_3x3 * x = P(M * Ni) * invA * x		x - SH coefficents
	float3 RotateSphericalHarmonicBand1(float3 band1, float3x3 const& mat)
	{
		/**
			inverse() is not constexpr - so we pre-calculate it in mathematica
			constexpr float3 N0{1, 0, 0}
			constexpr float3 N1{0, 1, 0}
			constexpr float3 N2{0, 0, 1}
			constexpr float3x3 A1 =		// this is the projection of N0, N1, N2 to SH space
			{
				float3{-N0.y, N0.z, -N0.x},
				float3{-N1.y, N1.z, -N1.x},
				float3{-N2.y, N2.z, -N2.x},
			}
			A1 = 
			{
				 0, 0, -1,
				-1, 0,  0,
				 0, 1,  0
			};
			const float3x3 invA1 = inverse(A1)
		*/

		// 1. invA
		// column-major
		constexpr float3x3 invA1TimesK =
		{
			 0, -1, 0,	// col 0
			 0,  0, 1,	// col 1
			-1,  0, 0	// col 2
		};
		const float3 MN0 = mat[0];	// mat * N0
		const float3 MN1 = mat[1];	// mat * N1
		const float3 MN2 = mat[2];	// mat * N2;

		// 2. P(M * Ni)
		const mat3 R1OverK =
		{
			-MN0.y, MN0.z, -MN0.x,
			-MN1.y, MN1.z, -MN1.x,
			-MN2.y, MN2.z, -MN2.x,
		};
		
		// 3. P(M * Ni) * invA * x
		return R1OverK * invA1TimesK * band1;
	}

	float5 RotateSphericalHarmonicBand2(float5 const& band2, mat3 const& mat)
	{
		constexpr float Sqrt_3 = 1.7320508076f;
		constexpr float n = 1.0f / SqrtTwo;

		// constexpr float3 N0{1, 0, 0};
		// constexpr float3 N0{0, 0, 1};
		// constexpr float3 N0{n, n, 0};
		// constexpr float3 N0{n, 0, n};
		// constexpr float3 N0{0, n, n};
		// constexpr float Sqrt_Pi = 1.7724538509f;
		// constexpr float Sqrt_15 = 3.8729833462f;
		// constexpr float K = Sqrt_15 / (2.0f * Sqrt_Pi);
		// --> K * Inv{mat5(Project(N0), Project(N1), Project(N2), Project(N3), Project(N4)})
		constexpr float5 invATimesK[5] =
		{
			{ 0,	  1, 2,  0,  0},
			{-1,	  0, 0,  0, -2},
			{ 0, Sqrt_3, 0,  0,  0},
			{ 1,	  1, 0, -2,  0},
			{ 2,	  1, 0,  0,  0}
		};

		// this projects a float3 to SH2/k space (i.e. we premultiply by 1/k)
		auto Project = [=](float3 s) -> float5
		{
			return float5
			{
				(s.y * s.x),
				-(s.y * s.z),
				1 / (2 * Sqrt_3) * (3 * s.z * s.z - 1),
				-(s.x * s.x),
				0.5f * (s.x * s.x - s.y * s.y)
			};
		};

		// invA * k * band2
		const float5 invATimesKTimesBand2 = Multiply(invATimesK, band2);

		// (Proj(mat * Ni)), i = 0...4
		const float5 ROverK[5] =
		{
			Project(mat[0]),				// mat * N0
			Project(mat[2]),				// mat * N1
			Project(n * (mat[0] + mat[1])),	// mat * N2
			Project(n * (mat[0] + mat[2])),	// mat * N3
			Project(n * (mat[1] + mat[2]))	// mat * N4
		};

		const float5 result = Multiply(ROverK, invATimesKTimesBand2);

		return result;
	}

	/**
		SH from environment with high dynamic range (or high frequencies -- high dynamic range creates
	high frequencies) exhibit "ringing" and negative values when reconstructed.
		to mitigate this, we need to low-pass the input image -- or equivalently window the SH by coefficient
	that tapper towards with the band.
		we use ideas and techniques from 
		<<Stupid Spherical Harmonics (SH)>>
		<<Deringing Spherical Harmonics>>
	by Peter-Pike Sloan
	https://www.ppsloan.org/publications/shdering.pdf
	*/
	float SincWindow(size_t l, float w)
	{
		if (l == 0)
			return 1.0f;
		else if (l >= w)
			return 0.0f;

		// we use a sinc window scaled to the desired window size in bands units
		// a sinc window only has zonal harmonics
		float x = (Pi * l) / w;
		x = std::sin(x) / x;

		// the convolution of a SH function f and z ZH function h is just the product of both 
		// scaled by 1 / K(0, l) -- the window coefficients include this scale factor

		// taking the window to power N is equivalent to applying the filter N times
		return std::pow(x, 4.0f);
	}

	void WindowSH(std::unique_ptr<float3[]>& sh, size_t numBands, float cutoff)
	{
		using SH3 = std::array<float, 9>;

		auto RotateSh3Bands = [](SH3 const& sh, const mat3& mat) -> SH3
		{
			// SH3 ret;
			const float b0 = sh[0];
			const float3 band1{ sh[1], sh[2], sh[3] };
			const float3 b1 = RotateSphericalHarmonicBand1(band1, mat);
			const float5 band2{ sh[4],sh[5],sh[6],sh[7],sh[8] };
			const float5 b2 = RotateSphericalHarmonicBand2(band2, mat);

			return {b0, b1[0], b1[2], b1[3], b2[0], b2[1], b2[2], b2[3], b2[4] };
		};

		auto Shmin = [RotateSh3Bands](SH3 f) -> float
		{
			// "Deringing Spherical Harmonics" by Peter-Pike Sloan
			constexpr float Sqrt_3 = 1.7320508076f;
			constexpr float Sqrt_5 = 2.2360679775f;
			constexpr float Sqrt_15 = 3.8729833462f;
			constexpr float A[9] =	// K = sqrt( [(2l + 1) * (l-|m|)! / (4Pi * (l+|m|)!])
			{
				1.0f / (2.0f * SqrtPi),		// 0: 0 0
				-Sqrt_3  / (2.0f * SqrtPi),	// 1: 1 -1
				 Sqrt_3  / (2.0f * SqrtPi),	// 2: 1 0
				-Sqrt_3  / (2.0f * SqrtPi),	// 3: 1 1
				 Sqrt_15 / (2.0f * SqrtPi),	// 4: 2 -2
				-Sqrt_15 / (2.0f * SqrtPi),	// 5: 2 -1
				 Sqrt_5  / (4.0f * SqrtPi),	// 6: 2 0
				-Sqrt_15 / (2.0f * SqrtPi),	// 7: 2 1
				 Sqrt_15 / (4.0f * SqrtPi),	// 8: 2 2
			};

			// first this to do is to rotate the SH to align Z with the optimal linear direction
			const float3 dir = normalize(float3{ -f[3], -f[1], f[2] });
			const float3 zAxis = -dir;
			const float3 xAxis = normalize(cross(float3(0, 1, 0), zAxis));
			const float3 yAxis = cross(zAxis, xAxis);
			const mat3 M = transpose(mat3(xAxis, yAxis, -zAxis));

			f = RotateSh3Bands(f, M);
			// here were guaranteed to have normalize(float3{-f[3], -f[1], f[2]}) == {0, 0, 1}

			// find the min for |m| = 2
			// Peter-Pike Sloan shows that the minimum can be expressed as a function
			// of z such as: m2Min = -m2Max * (1 - z^2) = m2Max * z^2 - m2Max
			//		with m2Max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);
			// we can therefore include this in the ZH min computation (which is function of z^2 as well)
			float m2Max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);

			// find the min of the zonal harmonics
			// this comes from minimizing the function:
			//	ZH(z) = (A[0] * f[0]) + (A[2] * f[2]) * z + (A[6] * f[6]) * (3 * s.z * s.z - 1)

			// we do that by finding where it's derivative d/dz is 0:
			//		dZH(z) / dz = a * z^2 + b * z + c
			//		which is 0 for z = -b / (2 * a)
			// we also need to check that -1<z<1, otherwise the min is either in z = -1 or 1
			const float a = 3 * A[6] * f[6] + m2Max;
			const float b = A[2] * f[2];
			const float c = A[0] * f[0] - A[6] * f[6] - m2Max;

			const float zmin = -b / (2.0f * a);
			const float m0Min_z = a * zmin * zmin + b * zmin + c;
			const float m0Min_b = std::min(a + b + c, a - b + c);

			const float m0Min = (a > 0 && zmin >= -1 && zmin <= 1) ? m0Min_z : m0Min_b;

			// find the min for l = 2, |m| = 1
			// l = 1, |m| = 1 is guaranteed to be 0 because of the rotation step 
			// the function considered is:
			//		Y(x, y, z) = A[5] * f[5] * x.y * x.z + A[7] * f[7] * s.z * s.x
			float d = A[4] * std::sqrt(f[5] * f[5] + f[7] * f[7]);

			// the |m|=1 function is minimal in -0.5 -- use that to skip the Newton's loop when possible
			float minimum = m0Min - 0.5f * d;
			if (minimum < 0)
			{
				// we could be negative, to find the minimum we will use Newton's method

				auto func = [=](float x)
				{
					// first term accounts for ZH + |m| = 2, second terms for |m|=1
					return (a * x * x + b * x + c) + (d * x * std::sqrt(1 - x * x));
				};

				// this is f'/f''
				auto increment = [=](float x)
				{
					return (x * x - 1) * (d - 2 * d * x * x + (b + 2 * a * x) * std::sqrt(1 - x * x)) /
						(3 * d * x - 2 * d * x * x * x - 2 * a * std::pow(1 - x * x, 1.5f));
				};

				float dz;
				float z = -1.0f / SqrtTwo;	// we start guessing the min of |m|=1 function
				do
				{
					minimum = func(z);	// evaluate our function
					dz = increment(z);	// refine out guess by this amount
					z = z - dz;
				} while (std::abs(z) <= 1 && std::abs(dz) > 1e-5f);

				if (std::abs(z) > 1)
				{
					// z was out of range 
					minimum = std::min(func(1), func(-1));
				}
			}
			return minimum;
		};

		auto windowing = [numBands](SH3 f, float cutoff) -> SH3
		{
			for (size_t l = 0; l < numBands; ++l)
			{
				float w = SincWindow(l, cutoff);
				f[SHIndex(0, l)] *= w;
				for (int m = 1; m <= l; ++m)
				{
					f[SHIndex(-m, l)] *= w;
					f[SHIndex(m, l)] *= w;
				}
			}
			return f;
		};

		if (cutoff == 0)	// auto windowing (default)
		{
			if (numBands > 3)
			{
				// error
				return;
			}

			cutoff = numBands * 4.0f + 1.0f;	// start at a large band
			// we need to process each channel separately
			SH3 SHArray = {};
			for (int channel = 0; channel < 3; ++channel)
			{
				for (size_t i = 0; i < numBands * numBands; ++i)
				{
					SHArray[i] = sh[i][channel];
				}

				// find a cut-off band that works
				float l = (float)numBands;
				float r = cutoff;
				for (size_t i = 0; i < 16 && l + 0.1f < r; ++i)
				{
					float m = 0.5f * (l + r);
					if (Shmin(windowing(SHArray, m)) < 0)
						r = m;
					else
						l = m;
				}
				cutoff = std::min(cutoff, l);
			}
		}

		for (size_t l = 0; l < numBands; ++l)
		{
			float w = SincWindow(l, cutoff);
			sh[SHIndex(0, l)] *= w;
			for (int m = 1; m <= l; ++m)
			{
				sh[SHIndex(-m, l)] *= w;
				sh[SHIndex( m, l)] *= w;
			}
		}
	}

	// only used for debugging
	// evaluate an Associated Legendre Polynomial P(m, l, x) at x
	float Legendre(int m, size_t l, float x)
	{
		float pmm = 1.0;
		if (m > 0)
		{
			float somx2 = std::sqrt((1.0f - x) * (1.0f + x));	// sqrt((1-x^2))
			float fact = 1.0f;
			for (int i = 1; i <= m; ++i)
			{
				pmm *= (-fact) * somx2;
				fact += 2.0f;
			}
		}
		if (l == m)		// P(m, m, x)
			return pmm;
		float pmmp1 = x * (2.0f * m + 1.0f) * pmm;
		if (l == m + 1)	// P(m, m+1, x)
			return pmmp1;
		float pll = 0.0f;
		for (size_t ll = m + 2; ll <= l; ++ll)
		{
			pll = ((2.0f * ll - 1.0f) * x * pmmp1 - (ll + m - 1.0f) * pmm) / (ll - m);
			pmm = pmmp1;
			pmmp1 = pll;
		}

		return pll;
	}
	
}
