//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#pragma once

#include "pch.h"
#include <codecvt>

namespace Utility
{
#ifdef _CONSOLE
    inline void Print( const char* msg ) { printf("%s", msg); }
    inline void Print( const wchar_t* msg ) { wprintf(L"%ws", msg); }
#else
    inline void Print(const char* msg) { OutputDebugStringA(msg); }
    inline void Print(const wchar_t* msg) { OutputDebugString(msg); }
#endif

    inline void Printf( const char* format, ... )
    {
        char buffer[256];
        va_list ap;
        va_start(ap, format);
        vsprintf_s(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
    }

    inline void Printf( const wchar_t* format, ... )
    {
        wchar_t buffer[256];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
    }

#ifndef RELEASE
    inline void PrintSubMessage( const char* format, ... )
    {
        Print("--> ");
        char buffer[256];
        va_list ap;
        va_start(ap, format);
        vsprintf_s(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
        Print("\n");
    }
    inline void PrintSubMessage( const wchar_t* format, ... )
    {
        Print("--> ");
        wchar_t buffer[256];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
        Print("\n");
    }
    inline void PrintSubMessage( void )
    {
    }

    std::wstring UTF8ToWideString(const std::string &str);
    std::string WideStringToUTF8(const std::wstring &wstr);

    std::string ToLower(const std::string &str);
    std::string GetBasePath(const std::string &str);
    std::string RemoveBasePath(const std::string &str);
    std::string GetFileExtension(const std::string &str);
    std::string RemoveExtension(const std::string &str);

    std::wstring ToLower(const std::wstring& str);
    std::wstring GetBasePath(const std::wstring& str);
    std::wstring RemoveBasePath(const std::wstring& str);
    std::wstring GetFileExtension(const std::wstring& str);
    std::wstring RemoveExtension(const std::wstring& str);

#endif

} // namespace Utility

#ifdef ERROR
#undef ERROR
#endif
#ifdef ASSERT
#undef ASSERT
#endif
#ifdef HALT
#undef HALT
#endif

#define HALT( ... ) ERROR( __VA_ARGS__ ) __debugbreak();

#ifdef RELEASE

    #define ASSERT( isTrue, ... ) (void)(isTrue)
    #define WARN_ONCE_IF( isTrue, ... ) (void)(isTrue)
    #define WARN_ONCE_IF_NOT( isTrue, ... ) (void)(isTrue)
    #define ERROR( msg, ... )
    #define DEBUGPRINT( msg, ... ) do {} while(0)
    #define ASSERT_SUCCEEDED( hr, ... ) (void)(hr)

#else    // !RELEASE

    #define STRINGIFY(x) #x
    #define STRINGIFY_BUILTIN(x) STRINGIFY(x)
    #define ASSERT( isFalse, ... ) \
        if (!(bool)(isFalse)) { \
            Utility::Print("\nAssertion failed in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
            Utility::PrintSubMessage("\'" #isFalse "\' is false"); \
            Utility::PrintSubMessage(__VA_ARGS__); \
            Utility::Print("\n"); \
            __debugbreak(); \
        }

    #define ASSERT_SUCCEEDED( hr, ... ) \
        if (FAILED(hr)) { \
            Utility::Print("\nHRESULT failed in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
            Utility::PrintSubMessage("hr = 0x%08X", hr); \
            Utility::PrintSubMessage(__VA_ARGS__); \
            Utility::Print("\n"); \
            __debugbreak(); \
        }


    #define WARN_ONCE_IF( isTrue, ... ) \
    { \
        static bool s_TriggeredWarning = false; \
        if ((bool)(isTrue) && !s_TriggeredWarning) { \
            s_TriggeredWarning = true; \
            Utility::Print("\nWarning issued in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
            Utility::PrintSubMessage("\'" #isTrue "\' is true"); \
            Utility::PrintSubMessage(__VA_ARGS__); \
            Utility::Print("\n"); \
        } \
    }

    #define WARN_ONCE_IF_NOT( isTrue, ... ) WARN_ONCE_IF(!(isTrue), __VA_ARGS__)

    #define ERROR( ... ) \
        Utility::Print("\nError reported in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
        Utility::PrintSubMessage(__VA_ARGS__); \
        Utility::Print("\n");

    #define DEBUGPRINT( msg, ... ) \
    Utility::Printf( msg "\n", ##__VA_ARGS__ );

#endif

#define BreakIfFailed( hr ) if (FAILED(hr)) __debugbreak()

void SIMDMemCopy( void* __restrict Dest, const void* __restrict Source, size_t NumQuadwords );
void SIMDMemFill( void* __restrict Dest, __m128 FillVector, size_t NumQuadwords );

std::wstring MakeWStr( const std::string& str );

namespace StringUtils
{
    // Falcor StringUtils.h

    // Ç°×º
    inline bool HasPrefix(const std::string str, const std::string& prefix, bool caseSensitive = false)
    {
        if (str.size() >= prefix.size())
        {
            if (caseSensitive == false)
            {
                std::string s = str;
                std::string pfx = prefix;
                std::transform(str.begin(), str.end(), s.begin(), ::tolower);
                std::transform(prefix.begin(), prefix.end(), pfx.begin(), ::tolower);
                return s.compare(0, pfx.length(), pfx) == 0;
            }
            else
                return str.compare(0, prefix.length(), prefix) == 0;
        }
        return false;
    }
    // ºó×º
    inline bool HasSuffix(const std::string& str, const std::string& suffix, bool caseSensitive = false)
    {
        if (str.size() >= suffix.size())
        {
            std::string s = str.substr(str.length() - suffix.length());
            if (caseSensitive == false)
            {
                std::string sfx = suffix;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                std::transform(suffix.begin(), suffix.end(), sfx.begin(), ::tolower);
                return (sfx == s);
            }
            else
                return (s == suffix);
        }
        return false;
    }

    inline std::vector<std::string> SplitString(const std::string& str, const std::string& delim)
    {
        std::string s;
        std::vector<std::string> vec;
        for (char c : str)
        {
            if (delim.find(c) != std::string::npos)
            {
                if (s.length())
                {
                    vec.push_back(s);
                    s.clear();
                }
            }
            else
            {
                s += c;
            }
        }
        if (s.length())
        {
            vec.push_back(s);
        }
        return vec;
    }

    inline std::string JoinStrings(const std::vector<std::string>& strings, const std::string& separator)
    {
        std::string result;
        for (auto it = strings.begin(); it != strings.end(); it++)
        {
            result += *it;

            if (it != strings.end() - 1)
            {
                result += separator;
            }
        }
        return result;
    }

    // Convert an ASCII string to a UTF-8 wstring
    inline std::wstring string_2_wstring(const std::string& s)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
        std::wstring ws = cvt.from_bytes(s);
        return ws;
    }

    // Convert a UTF-8 wstring to an ASCII string
    inline std::string wstring_2_string(const std::wstring& ws)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
        std::string s = cvt.to_bytes(ws);
        return s;
    }

    // -mf
    inline std::string GetBaseDir(const std::string& filePath)
    {
        // find_last_of - Searches the string for the last character that matches any of the characters specified in its arguments.
        size_t rpos = filePath.find_last_of("/\\");
        if (rpos != std::string::npos)
            return filePath.substr(0, rpos + 1);
        return "";
    }
    inline std::string GetBaseFileName(const std::string& filePath)
    {
        return filePath.substr(filePath.find_last_of("/\\") + 1);
    }
    inline std::string GetFileNameWithNoExtensions(const std::string &filePath)
    {
        std::string baseFileName = filePath.substr(filePath.find_last_of("/\\") + 1);
        return baseFileName.substr(0, baseFileName.rfind('.'));
    }
}

namespace MyDirectX
{
    //--------------------------------------------------------------------------------------
    // ½è¼øDDSTextureLoader.cpp
    // Return the Bytes for a particular format
    //--------------------------------------------------------------------------------------
    static uint32_t BytesPerFormat(_In_ DXGI_FORMAT fmt)
    {
        switch (fmt)
        {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 16;

        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 12;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            return 8;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_YUY2:
            return 4;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return 3;

        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_B4G4R4A4_UNORM:
            return 2;

        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
            return 1;

        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 1;

        default:
            return 0;
        }
    }
}
