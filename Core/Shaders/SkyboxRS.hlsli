#define Skybox_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"CBV(b0)," \
	"CBV(b1)," \
	"DescriptorTable(SRV(t0, numDescriptors = 1))," \
	"StaticSampler(s0," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR," \
		"visibility = SHADER_VISIBILITY_PIXEL)"