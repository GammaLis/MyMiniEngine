#pragma once
#include "pch.h"
#include <ppltasks.h>

namespace Utility
{
	typedef std::shared_ptr<std::vector<byte>> ByteArray;
	extern ByteArray NullFile;

	// reads the entire contents of a binary file. If the file with the same name except with an additional
	// ".gz" suffix exists, it will be loaded and decompressed instead.
	// this operation blocks until the entire file is read
	ByteArray ReadFileSync(const std::wstring& fileName);

	// same as previous except that it does not block but instead returns a task
	concurrency::task<ByteArray> ReadFileAsync(const std::wstring& fileName);
}

