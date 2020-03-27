#pragma once
#include "pch.h"
#include "Graphics.h"
#include "CommonCompute.h"
#include "CommandListManager.h"

namespace MyDirectX
{
	class IComputingApp
	{
	public:
		IComputingApp() : m_CommonCompute{ std::make_unique<Graphics>() } {  }
		virtual void Init()
		{
			m_CommonCompute->CreateDeviceResources();
			InitCustom();
		}

		virtual void Run() {  }

		virtual void Shutdown() 
		{
			m_CommonCompute->s_CommandManager.IdleGPU();
			m_CommonCompute->Shutdown(); 
		}

	protected:
		virtual void InitCustom() {  }

		std::unique_ptr<Graphics> m_CommonCompute;
	};
}
