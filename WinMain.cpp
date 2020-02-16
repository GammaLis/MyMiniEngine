#include "MyBaseApp.h"
#include "Utility.h"
#include "IGameApp.h"
#include "ModelViewer.h"

// I. windows程序 预处理定义-_WINDOWS,连接器子系统SubSystem:WINDOWS
//int WINAPI WinMain(_In_ HINSTANCE hInstance,
//	_In_opt_ HINSTANCE hPrevInstance,
//	_In_ LPSTR lpCmdLine,
//	_In_ int nCmdShow)
//{
//	MyDirectX::MyBaseApp myApp(hInstance, L"Hello, World!");
//
//	int ret = 0;
//	if (myApp.Init())
//	{
//		ret = myApp.Run();
//	}
//
//	return ret;
//}

// II.console程序 预处理定义-_CONSOLE,连接器子系统SubSystem:CONSOLE
// 还需链接 runtimeobject.lib
#pragma comment(lib, "runtimeobject.lib")

int main(int argc, const char* argv[])
{
	// RoInitializer
	Microsoft::WRL::Wrappers::RoInitializeWrapper InitializeWinRT(RO_INIT_MULTITHREADED);
	ASSERT_SUCCEEDED(InitializeWinRT);

	HINSTANCE hInst = GetModuleHandle(0);

#pragma region Test
	float f = 0.25f;	// B 0.01 -> 1E(-2) -> 1E(127 -2 = 125) <0 - 符号位， 125 - E， 0 - D>
	float* pf = &f;
#pragma endregion

#ifdef MINI_ENGINE
	// 1. IGameApp
	// MyDirectX::IGameApp gApp(hInst);
	MyDirectX::ModelViewer gApp(hInst, L"ModelViewer");

	int ret = 0;
	if (gApp.Init())
	{
		ret = gApp.Run();
	}

	gApp.Cleanup();

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
