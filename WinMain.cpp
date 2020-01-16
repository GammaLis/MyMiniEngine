#include "MyBaseApp.h"
#include "Utility.h"
#include "IGameApp.h"

// I. windows���� Ԥ������-_WINDOWS,��������ϵͳSubSystem:WINDOWS
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

// II.console���� Ԥ������-_CONSOLE,��������ϵͳSubSystem:CONSOLE
// �������� runtimeobject.lib
#pragma comment(lib, "runtimeobject.lib")

int main(int argc, const char* argv[])
{
	// RoInitializer
	Microsoft::WRL::Wrappers::RoInitializeWrapper InitializeWinRT(RO_INIT_MULTITHREADED);
	ASSERT_SUCCEEDED(InitializeWinRT);

	HINSTANCE hInst = GetModuleHandle(0);

#ifdef MINI_ENGINE
	// 1. IGameApp
	MyDirectX::IGameApp gApp(hInst);

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
