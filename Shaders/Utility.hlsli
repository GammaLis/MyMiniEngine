#ifndef UTILITY_HLSLI
#define UTILITY_HLSLI

#include "MathConstants.h"

// Pseudo Random Numbers
// 1. Linear Congruential Generator (LCG)
uint rand_lcg(inout uint rng_state)
{
	// LCG values from Numerical Recipes
	rng_state = 1664525 * rng_state + 1013904223;
	return rng_state;
}

uint rand_xorshift(inout uint rng_state)
{
	// Xorshift algorithm from George Marsaglia's paper
	rng_state ^= (rng_state << 13);
	rng_state ^= (rng_state >> 17);
	rng_state ^= (rng_state << 5);
	return rng_state;
}
// Uses: 
// uint rng_state = dtid.x; (SV_DispatchThreadID)
// uint r = rand_xorshift(rng_state);
// float f = float(rand_xorshift(rng_state)) * (1.0f / 4294967296.0f);	// generate a random float in[0, 1)

// LCGs are really fast - updating the state takes just one imad instruction
// Xorshift is a bit slower, requiring 6 instructions, but that's not bad considering the quality
// of random numbers it gives you.

// Wide and Deep
// 		PRNGs are designed to be well-distributed when you "go deep" - draw many values from the same instances.
// Since this involves sequentially updating the state after each value, it doesn't map well to the GPU.
// On the GPU, we need to "go wide" - set up a lot of independent PRNGs instances with different seeds 
// so we can draw from each of them in parallel. But PRNGs are not designed to give good statistics across seeds.
// 		Thereis another kind of pseudorandom function that's explicitly designed to be well-distributed when going wide:
// 		hash functions. If we hash the seed when initializing the PRNG, it should mix things up enough to decorrelate 
// 		the sequences of nearby threads.

uint wang_hash(inout uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}
// Uses: using thread index as a seed

float random01(inout uint seed)
{
	return float( wang_hash(seed) ) * (1.0f / 4294967296.0f);
}

float2 Gauss_BoxMuller(inout uint seed, float mu = 0.0f, float sigma = 1.0f)
{
	static float Epsilon = 1e-4;
	float u, v;
	// do
	// {
	// 	u = random01(seed);
	// 	v = random01(seed);
	// } while(u <= Epsilon);
	
	// ==> [0, 1) 改成 (0, 1]
	// https://www.zhihu.com/question/29971598 如何生成正态分布的随机数？
	// miloyip/normaldist-benchmark · GitHub
	u = 1.0f - random01(seed);
	v = random01(seed);

	float r = sqrt(-2.0f * log(u));
	float theta = M_2PI * v;
	float z0 = r * cos(theta);
	float z1 = r * sin(theta);
	// return z0 * sigma + mu;
	return float2(z0, z1) * sigma + mu;
}

#endif	// UTILITY_HLSLI


/**
 * 	Quick And Easy GPU Random Numbers In D3D11
 * 	- http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
 */

/**
 * 	Box-Muller transform
 * 	The Box-Muller transform is a random number sampling method for generating pairs of independent,
 * standard, normally distributed (0 expectation, unit variance) random numbers, given a source of 
 * uniformly distributed random numbers.
 * 	Suppose U1 and U2 are independent samples chosen from the uniform distribution on the uint interval [0,1)
 * 	Z0 = R*cos(Theta) = sqrt(-2 * In(U1) ) * cos(2pi * U2)
 * 	Z1 = R*sin(Theta) = sqrt(-2 * In(U1) ) * sin(2pi * U2)
 */

/**
 * 	Ziggurat algorithm
 * 	The ziggurat algorithm is an algorithm for pseudo-random number sampling. Belonging to the class
 * of rejection sampling algorithm, it relies on an underlying source of uniformly-distributed random 
 * numbers, typically from a pseudo-random number generator, as well as precomputed tables. The algorithm
 * is used to generate values from a monotone decreasing probability distribution.
 * 
 * Ignoring for a moment the problem of layer 0, and given uniform random variables U0 and U1 [0, 1),
 * 	1. Choose a random layer 0 <= i < n
 * 	2. Let x = U0 * xi
 * 	3. If x < x_(i+1) return x
 * 	4. Let y = yi + U1 *(y_(i+1) - yi)
 * 	5. Compute f(x), if y < f(x), return x
 *  6. Otherwise, choose new random numbers and go back to step 1
 */
