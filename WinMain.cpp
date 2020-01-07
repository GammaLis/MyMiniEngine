#include "MyBaseApp.h"

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
