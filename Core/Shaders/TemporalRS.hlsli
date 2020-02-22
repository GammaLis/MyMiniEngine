#define Temporal_RootSig \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"CBV(b1)," \
	"DescriptorTable(SRV(t0, numDescriptors = 10))," \
	"DescriptorTable(UAV(u0, numDescriptors = 10))," \
	"StaticSampler(s0," \
		"addressU = TEXTURE_ADDRESS_BORDER," \
		"addressV = TEXTURE_ADDRESS_BORDER," \
		"addressW = TEXTURE_ADDRESS_BORDER," \
		"borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s1," \
		"addressU = TEXTURE_ADDRESS_BORDER," \
		"addressV = TEXTURE_ADDRESS_BORDER," \
		"addressW = TEXTURE_ADDRESS_BORDER," \
		"borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK," \
		"filter = FILTER_MIN_MAG_MIP_POINT)"