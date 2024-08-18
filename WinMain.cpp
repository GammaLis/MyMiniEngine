﻿#define MINI_ENGINE		// MINI_ENGINE COMMON_COMPUTE

#define IMPLEMENTED_IGAMEAPP	0
#define IMPLEMENTED_MODELVIEWER 1
#define IMPLEMENTED_GLTFVIEWER	2
#define IMPLEMENTED_SCENEVIEWER 3
#define IMPLEMENTED_OCEANVIEWER 4
#define IMPLEMENTED_VORONOITEXTURE	5

#define IMPLEMENTED_BVH 6

#define IMPLEMENTED IMPLEMENTED_SCENEVIEWER

#include "MyBaseApp.h"
#include "Utility.h"
#include "IGameApp.h"

#if IMPLEMENTED == IMPLEMENTED_MODELVIEWER
#include "ModelViewer.h"
#elif IMPLEMENTED == IMPLEMENTED_GLTFVIEWER
#include "glTFViewer.h"
#elif IMPLEMENTED == IMPLEMENTED_SCENEVIEWER
#include "SceneViewer.h"
#elif IMPLEMENTED == IMPLEMENTED_OCEANVIEWER
#include "OceanViewer.h"
#elif IMPLEMENTED == IMPLEMENTED_VORONOITEXTURE
#include "VoronoiTextureGenerator.h"
#elif IMPLEMENTED == IMPLEMENTED_BVH
#include "BVHApp.h"
#endif

#include "CubemapIBLApp.h"

namespace DX12 = MyDirectX;

/**
*	TODO:
*	ERROR C2102: “&” requires l-value	==>	https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
*	The problem is that pattern is actually not conformant. The fix is to use:
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);

*	for now by disabling /permissive- by changing "Conformance Mode" to "No" in the C/C++ -> Language project settings.
*/

// I. windows程序 预处理定义-_WINDOWS,连接器子系统SubSystem:WINDOWS
#if 0
int WINAPI WinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow)
{
	MyDirectX::MyBaseApp myApp(hInstance, L"Hello, World!");

	int ret = 0;
	if (myApp.Init())
	{
		ret = myApp.Run();
	}

	return ret;
}
#endif

// II.console程序 预处理定义-_CONSOLE,连接器子系统SubSystem:CONSOLE
// 还需链接 runtimeobject.lib
#pragma comment(lib, "runtimeobject.lib")

int main(int argc, const char* argv[])
{
	// RoInitializer
	Microsoft::WRL::Wrappers::RoInitializeWrapper InitializeWinRT(RO_INIT_MULTITHREADED);
	ASSERT_SUCCEEDED(InitializeWinRT);

	HINSTANCE hInst = GetModuleHandle(0);

#if defined(MINI_ENGINE)
	constexpr int SceneWidth = 1280, SceneHeight = 720;

#if IMPLEMENTED == IMPLEMENTED_MODELVIEWER
	std::unique_ptr<DX12::ModelViewer> gApp(new DX12::ModelViewer(hInst, "Models/sponza.obj", L"ModelViewer", SceneWidth, SceneHeight));

#elif IMPLEMENTED == IMPLEMENTED_GLTFVIEWER
	std::unique_ptr<DX12::glTFViewer> gApp(new DX12::glTFViewer(hInst, "Models/buster_drone.gltf", L"glTFViewer", SceneWidth, SceneHeight));

#elif IMPLEMENTED == IMPLEMENTED_SCENEVIEWER
	std::unique_ptr<DX12::SceneViewer> gApp(new DX12::SceneViewer(hInst, L"SceneViewer", SceneWidth, SceneHeight));

#elif IMPLEMENTED == IMPLEMENTED_OCEANVIEWER
	std::unique_ptr<DX12::OceanViewer> gApp(new DX12::OceanViewer(hInst, L"OceanViewer", SceneWidth, SceneHeight));

#elif IMPLEMENTED == IMPLEMENTED_VORONOITEXTURE
	std::unique_ptr<DX12::VoronoiTextureGenerator> gApp(new DX12::VoronoiTextureGenerator(hInst, L"VoronoiTextureGenerator", SceneWidth, SceneHeight));

#elif IMPLEMENTED == IMPLEMENTED_BVH
	std::unique_ptr<DX12::BVHApp> gApp(new DX12::BVHApp(hInst, L"BVH App", MyDirectX::SCR_WIDTH, MyDirectX::SCR_HEIGHT));

#else
	// 0. IGameApp
	MyDirectX::IGameApp gApp(hInst);
#endif	// IMPLEMENTED

	int ret = 0;

	if (gApp->Init())
	{
		ret = gApp->Run();
	}

	gApp->Cleanup();
	
#elif defined(COMMON_COMPUTE)
	int ret = 0;
	MyDirectX::CubemapIBLApp myApp;
	myApp.Init(L"Textures/grasscube1024.dds", 1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM);	// DXGI_FORMAT_R8G8B8A8_UNORM DXGI_FORMAT_R16G16B16A16_UNORM
		// Default_Anim grasscube1024 desertcube1024
	myApp.Run();
	ret = myApp.SaveToFile("CommonCompute/Test.png");
	myApp.Cleanup();

#else
	// 2. MyBaseApp
	MyDirectX::MyBaseApp myApp(hInst, L"Hello, World!");

	int ret = 0;
	if (myApp.Init())
	{
		ret = myApp.Run();
	}
#endif	

	return ret;
}
