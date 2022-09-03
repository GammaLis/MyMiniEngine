#ifndef RESERVOIR_INCLUDED
#define RESERVOIR_INCLUDED

// Ref: NV - RTXDI

#include "../Random.hlsl"

// Reserviors are stored in a structured buffer in a block-linear layout.
// This constant defines the size of that block, measured in pixels.
#define RESERVOIR_BLOCK_SIZE 16

// Bias correction for temporal and spatial resampling
// Use (1/M) normalization, which is very biased but also very fast
#define RESTIR_BIAS_CORRECTION_OFF 0
// Use MIS-like normalization but assume that every sample is visible
#define RESTIR_BIAS_CORRECTION_BASIC 1
// Use pairwise MIS normalization (assuming every sample is visible). Better perf & specular quality
#define RESTIR_BIAS_CORRECTION_PAIRWISE 2
// Use MIS-like normalization with visibility rays. Unbiased.
#define RESTIR_BAIS_CORRECTION_RAY_TRACED 3

#define RESTIR_WEIGHT_NORMALIZATION 1

#ifndef RESERVOIR_EPS 
#define RESERVOIR_EPS (1e-4f)
#endif

static const uint s_MaxM = 0x3FFF;

struct Reservoir
{
	int LightData;
	float TargetPdf;
	float Weights;
	float M;
};

StructuredBuffer<Reservoir> _ReservoirBuffer	: register(t5);

RWStructuredBuffer<Reservoir> RWReservoirBuffer	: register(u4);

Reservoir GetReservoir(uint2 pixel, uint2 resolution)
{
	uint index = pixel.y * resolution.x + pixel.x;
	return _ReservoirBuffer[index];
}

void StoreReservoir(Reservoir r, int2 pixel, uint2 resolution)
{
	uint index = pixel.y * resolution.x + pixel.x;
	RWReservoirBuffer[index] = r;
}

Reservoir CreateReservoir()
{
	Reservoir r = (Reservoir)0;
	r.LightData = -1;
	r.TargetPdf = 0;
	r.Weights = 0;
	r.M = 0;
	return r;
}

bool UpdateReservoir(inout Reservoir r, int lightIndex, float targetPdf, float weight, float random)
{
	r.Weights += weight;
	r.M += 1;
	if (random * r.Weights < weight)
	{
		r.LightData = lightIndex;
		r.TargetPdf = targetPdf;
		return true;
	}

	return false;
}

/**
 * 	A reservoir's state contains both the currently selected sample `y` and the sum of weights `w_sum` of all candidates
 * seen thus far. To combine 2 reservoirs, we treat each reservoir's `y` as a fresh sample with weight sum `w_sum` and 
 * feed it as input to a new reservoir. The result is mathematically equivalent to having performed reservoir sampling 
 * on the 2 reservoirs' combined input streams.	However, crutially this operation only requires *constant* time and 
 * avoids storing elements of either input stream, needing only access to each reservoir's current state. Input streams
 * of arbitrary number of reservoirs can be combined this way.
 * 	To account for the fact that samples from the neighboring pixel q' are resampled following a different target distribution
 * p_q', we reweight the samples with the factor p_q(y)/p_q'(y) to account for areas that were over- or undersampled at
 * the neighbor compared to the current pixel.
 */
bool CombineReservoirs(inout Reservoir dstReservoir, Reservoir srcReservoir, float random, float targetPdf = 1.0f)
{
	dstReservoir.M += srcReservoir.M;
#if RESTIR_WEIGHT_NORMALIZATION
	float risWeight = targetPdf * (srcReservoir.Weights * srcReservoir.M);
	dstReservoir.Weights += risWeight;
#else
	float risWeight = srcReservoir.TargetPdf < RESERVOIR_EPS ? 0 : srcReservoir.Weights * targetPdf / srcReservoir.TargetPdf;
	dstReservoir.Weights += risWeight;
#endif
	if (random * dstReservoir.Weights < risWeight)
	{
		dstReservoir.LightData = srcReservoir.LightData;
		dstReservoir.TargetPdf = targetPdf;
		return true;
	}

	return false;
}

// Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
void FinalizeResampling(inout Reservoir r)
{
	float denominator = r.TargetPdf * r.M;
	r.Weights = denominator < RESERVOIR_EPS ? 0.0 : r.Weights / denominator;
}


#endif // RESERVOIR_INCLUDED

/**
 * << Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting >>
 * > 5 DESIGN AND IMPLEMENTATION CHOICES
 * 
 * *  Candidate Generation - we sample M=32 initial candidates by importance sampling emissive triangles based on their power.
 * 
 * *  Target PDF - at each resampling step, we weight samples based on a target PDF. We use the unshadowed path contribution (fr*Le*G)
 * as the target PDF at each pixel.
 * 
 * *  Neighbor selection - for spatial reuse, we found that deterministically selected neighbors (e.g. in a small box around 
 * the current the current pixel) lead to distracting artifacts, and we instead sample k=5 (k=3 for unbiasd algorithm) random 
 * points in a 30-pixel radius around the current pixel, sampled from a low-discrepancy sequence.
 * For temporal reuse, we compute motion vectors to project the current pixel's position into the previous frame, and 
 * use the pixel there for temporal reuse.
 * For our biased algorithm, reusing candidates from neighboring pixels with substantially different geometry/material leads to
 * increased bias, and we use a simple heuristic to reject such pixels: we compare the difference in camera distance, and
 * the the angle between normals of the current pixel to the neighboring pixel, and reject the neighbor if either exceed 
 * some threshould (10% of current pixels depth and 25°, respectively).
 * 
 * *  Evaluated Sample Count - for higher sample counts, the algorithm can simply be repeated and the results averaged.
 * 
 * *  Reservior storage and temporal weighting - at each pixel, we only store the information of the pixel's reservoir:
 * `The selected sample y`, `The number of candidates M that contributed to the pixel` and `the probabilistic weight W`.
 * For N > 1, we store multiple samples y and weights W at each pixel to accomodate multiple reserviors.
 * 
 * 	With temporal reuse, the number of candidates M contributing to the pixel can in theory grow unbouned, as each frame
 * always combines its reservoir with the previous frame's. This causes temporal samples to be weighted disproportionately
 * high during resampling. To fix this, we simply clamp the previous frame's M to at most 20x of the current frame's reservoir's M,
 * which both stops unbouned growth of M and bounds the influence of temporal information.
 */
