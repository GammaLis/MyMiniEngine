#pragma once
#include "pch.h"
#include "PipelineState.h"
#include "RootSignature.h"

namespace MyDirectX
{
	class CommandContext;
	class CommandListManager;
	class CommandSignature;
	class ContextManager;

	class Graphics
	{
	public:
		static CommandListManager s_CommandManager;
		static ContextManager s_ContextManager;


	};

}
