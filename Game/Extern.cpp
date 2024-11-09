// cgltf

// Error C4996 : 'strncpy': This function or variable may be unsafe. Consider using strncpy_s instead.
// #define _CRT_NONSTDC_NO_WARNINGS
// Ref: https://stackoverflow.com/questions/22450423/how-to-use-crt-secure-no-warnings
#pragma warning(disable:4996)

#define CGLTF_IMPLEMENTATION
#include "Libraries/cgltf/cgltf.h"
