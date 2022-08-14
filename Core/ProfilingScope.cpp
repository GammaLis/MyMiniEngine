#include "ProfilingScope.h"
#include "CommandContext.h"

using namespace MyDirectX;

void ProfilingScope::BeginBlock(const std::wstring& name, CommandContext* context)
{
	context->PIXBeginEvent(name.data());
}

void ProfilingScope::EndBlock(CommandContext* context)
{
	context->PIXEndEvent();
}
