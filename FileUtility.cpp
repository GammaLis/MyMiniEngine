#include "FileUtility.h"
#include <fstream>
#include <mutex>
// #include <zlib.h>

namespace Utility
{
	using namespace concurrency;
	using namespace std;

	ByteArray NullFile = make_shared<vector<byte>>(vector<byte>());

	ByteArray DecompressZippedFile(wstring& fileName)
	{
		// TODO 
		// 暂时返回NULL
		return NullFile;
	}

	ByteArray ReadFileHelper(const wstring& fileName)
	{
		// TODO 
		// 暂时返回NULL
		return NullFile;
	}

	ByteArray ReadFileHelperEx(shared_ptr<wstring> pFileName)
	{
		wstring zippedFileName = *pFileName + L".gz";
		ByteArray firstTry = DecompressZippedFile(zippedFileName);
		if (firstTry != NullFile)
			return firstTry;

		return ReadFileHelper(*pFileName);
	}

	ByteArray Inflate(ByteArray compressedSource, int& err, uint32_t chunkSize = 0x100000)
	{
		// create a dynamic buffer to hold compressed blocks
		vector<unique_ptr<byte>> blocks;

		// TODO 
		// 暂时返回NULL
		return NullFile;
	}

	ByteArray DecompressedZippedFile(wstring& fileName)
	{
		ByteArray compressedFile = ReadFileHelper(fileName);
		if (compressedFile == NullFile)
			return NullFile;

		int error;
		ByteArray decompressedFile = Inflate(compressedFile, error);
		if (decompressedFile->size() == 0)
		{
			Printf(L"Couldn't unzip file %s: Error = %d\n", fileName.c_str(), error);
			return NullFile;
		}

		return decompressedFile;
	}
	

	ByteArray ReadFileSync(const std::wstring& fileName)
	{
		return ReadFileHelperEx(make_shared<wstring>(fileName));
	}

	concurrency::task<ByteArray> ReadFileAsync(const std::wstring& fileName)
	{
		shared_ptr<wstring> sharedPtr = make_shared<wstring>(fileName);
		return create_task([=] {return ReadFileHelperEx(sharedPtr); });
	}

}

