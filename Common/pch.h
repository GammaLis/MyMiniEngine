//
// pch.h
// Header for standard system include files.
//

#pragma once

#pragma warning(disable:4201)	// nonstandard extension used : nameless structure/union
#pragma warning(disable:4238)	// nonstandard extension used : class rvalue used as lvalue
#pragma warning(disable:4239)	// a non-const reference may only be bound to an lvalue; assignment operator takes a reference to non-const
#pragma warning(disable:4324)	// structure was padded due to __declspec(align())

#include <winsdkver.h>
#define _WIN32_WINNT 0x0A00
#include <sdkddkver.h>

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <d3d12.h>

#if defined(NTDDI_WIN10_RS2)
#include <dxgi1_6.h>
#else
#include <dxgi1_5.h>
#endif

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXPackedVector.h>

// DX12 - MiniEngine
#define D3D12_GPU_VIRTUAL_ADDRESS_NULL		((D3D12_GPU_VIRTUAL_ADDRESS)0)
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN	((D3D12_GPU_VIRTUAL_ADDRESS)-1)

#include "d3dx12.h"

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

#include <ppltasks.h>

// To use graphics and CPU markup events with the latest version of PIX, change this to include <pix3.h>
// then add the NuGet package WinPixEventRuntime to the project.
#include <pix3.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

//  DX12 - MiniEngine
#include "Utility.h"
#include "VectorMath.h"

namespace MyDirectX
{
	// Helper class for COM exceptions
	class com_exception : public std::exception
	{
	public:
		com_exception(HRESULT hr) : result(hr) {}

		virtual const char* what() const override
		{
			static char s_str[64] = {};
			sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
			return s_str;
		}

	private:
		HRESULT result;
	};

	// Helper utility converts D3D API failures into exceptions.
	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
		{
			throw com_exception(hr);
		}
	}

	inline void ThrowIfFailed(HRESULT hr, const wchar_t* msg)
	{
		if (FAILED(hr))
		{
			OutputDebugString(msg);
			throw com_exception(hr);
		}
	}

	inline void ThrowIfFalse(bool value)
	{
		ThrowIfFailed(value ? S_OK : E_FAIL);
	}

	inline void ThrowIfFalse(bool value, const wchar_t* msg)
	{
		ThrowIfFailed(value ? S_OK : E_FAIL, msg);
	}

	const unsigned SCR_WIDTH = 640, SCR_HEIGHT = 480;
}
