
#define Present_RootSig \
    "RootFlags(0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 2))," \
    "RootConstants(b0, num32BitConstants = 6), " \
    "SRV(t2, visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(UAV(u0, numDescriptors = 1)), " \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s1," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_POINT)"