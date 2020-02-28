#define Particle_RootSig \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 3)," \
	"CBV(b1)," \
	"CBV(b2)," \
	"DescriptorTable(SRV(t0, numDescriptors = 10))," \
	"DescriptorTable(UAV(u0, numDescriptors = 8))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_BORDER," \
		"addressV = TEXTURE_ADDRESS_BORDER," \
		"addressW = TEXTURE_ADDRESS_BORDER," \
		"borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
	"StaticSampler(s1, " \
		"addressU = TEXTURE_ADDRESS_BORDER," \
		"addressV = TEXTURE_ADDRESS_BORDER," \
		"addressW = TEXTURE_ADDRESS_BORDER," \
		"borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
		"filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s2, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
		"filter = FILTER_MIN_MAG_MIP_POINT)" // \FILTER_MIN_MAG_MIP_LINEAR
