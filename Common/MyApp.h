#pragma once

#include "pch.h"
//#include "MyWindow.h"
//#include "DeviceResources.h"
//#include "GameTimer.h"

namespace MyDirectX
{
	class MyWindow;
	class DeviceResources;
	class GameTimer;

	class MyApp
	{
	public:
		static MyApp* GetApp();
		static UINT GetDescriptorIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type);

		MyApp(HINSTANCE hInstance, const wchar_t *title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);
		virtual ~MyApp();

		virtual bool Init();

		virtual void OnResize();

		virtual void Update();

		virtual void Render();

		int Run();

		// convenience overrides for handling mouse event
		virtual void OnMouseDown(WPARAM btnState, int x, int y) {  }
		virtual void OnMouseUp(WPARAM btnState, int x, int y) {  }
		virtual void OnMouseMove(WPARAM btnState, int x, int y) {  }

		virtual LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	protected:
		virtual void InitAssets();

		static MyApp* m_App;

		std::unique_ptr<MyWindow> m_Window;
		std::unique_ptr<DeviceResources> m_DeviceResources;
		std::unique_ptr<GameTimer> m_Timer;

#pragma region Cached Direct3D object
		ID3D12Device* pDevice;
		ID3D12GraphicsCommandList* pCmdList;
#pragma endregion

	private:
		void CalculateFrameStats();
		void CacheD3DObjects();
		void ReleaseD3DObjects();

		HINSTANCE m_HInstance;

	protected:
		UINT m_Width;
		UINT m_Height;
		const wchar_t* m_Title;
	};
}

