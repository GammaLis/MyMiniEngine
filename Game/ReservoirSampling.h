#pragma once 

namespace RayTracing
{
	enum class ESamplingMode 
	{
		NoResampling 				= 1, // No resampling (RIS)
		SpatialResampling 			= 2, // Spatial resampling only
		TemporalResampling			= 3, // Temporal resampling only
		SpatiotemporalResampling	= 4, // Spatiotemporal resampling
	};

	enum class EBiasCorrection
	{
		Off 		= 0, // Use (1/M) normalization, which is very biased but also very fast
		Basic 		= 1, // Use MIS normalization but assume that every sample is visible
		Pairwise 	= 2, // Use pairwise MIS normalization. Assumes every sample is visible
		RayTraced 	= 3, // Use MIS-like normalization with visibility rays. Unbiased
	};
}
