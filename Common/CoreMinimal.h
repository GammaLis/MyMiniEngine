#pragma once

#pragma warning(disable:4201)	// nonstandard extension used : nameless structure/union
#pragma warning(disable:4238)	// nonstandard extension used : class rvalue used as lvalue
#pragma warning(disable:4239)	// a non-const reference may only be bound to an lvalue; assignment operator takes a reference to non-const
#pragma warning(disable:4324)	// structure was padded due to __declspec(align())

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX
#include "d3dx12.h"

// MiniEngine
#ifndef D3D12_GPU_VIRTUAL_ADDRESS_NULL
#define D3D12_GPU_VIRTUAL_ADDRESS_NULL		((D3D12_GPU_VIRTUAL_ADDRESS) 0)
#endif
#ifndef D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN	((D3D12_GPU_VIRTUAL_ADDRESS)-1)
#endif

// STL
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <vector>
#include <string>
#include <cwctype>
#include <map>
#include <memory>
#include <algorithm>
#include <functional>
#include <exception>
#include <stdexcept>

// Math
#include "VectorMath.h"
