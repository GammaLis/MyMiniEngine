#pragma once
#include "pch.h"


namespace MyDirectX
{
	class CommandContext;

	class ProfilingScope
	{
	public:
		ProfilingScope(const std::wstring& name, CommandContext& context)
			: m_Context(&context)
		{
			BeginBlock(name, m_Context);
		}
		~ProfilingScope()
		{
			EndBlock(m_Context);
		}


	private:
		void BeginBlock(const std::wstring& name, CommandContext* context = nullptr);
		void EndBlock(CommandContext* context = nullptr);

		CommandContext* m_Context = nullptr;
	};
}
