#include "FileUtility.h"
#include <fstream>
#include <mutex>
#include <zlib.h>		// NuGet

namespace Utility
{
	using namespace concurrency;
	using namespace std;

	ByteArray NullFile = make_shared<vector<byte>>(vector<byte>());

	ByteArray DecompressZippedFile(wstring& fileName);

	ByteArray ReadFileHelper(const wstring& fileName)
	{
		struct _stat64 fileStat;
		int fileExists = _wstat64(fileName.c_str(), &fileStat);
		if (fileExists == -1)
			return NullFile;

		ifstream file(fileName, ios::in | ios::binary);
		if (!file)
			return NullFile;

		// std::basic_istream<charT, Traits>::seekg
		// 设置当前关联streambuf 对象的输入位置指示器。
		ByteArray byteArray = make_shared<vector<byte>>(file.seekg(0, ios::end).tellg());
		file.seekg(0, ios::beg).read((char*)byteArray->data(), byteArray->size());
		file.close();

		ASSERT(byteArray->size() == (size_t)fileStat.st_size);

		return byteArray;
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

		z_stream strm = {};
		strm.data_type = Z_BINARY;
		strm.total_in = strm.avail_in = (uInt)compressedSource->size();
		strm.next_in = compressedSource->data();

		err = inflateInit2(&strm, (15 + 32));	// 15 window bits, and the +32 tells zlib to detect if using gzip or zlib 

		while (err == Z_OK || err == Z_BUF_ERROR)
		{
			strm.avail_out = chunkSize;
			strm.next_out = (byte*)malloc(chunkSize);
			blocks.emplace_back(strm.next_out);
			err = inflate(&strm, Z_NO_FLUSH);
		}

		if (err != Z_STREAM_END)
		{
			inflateEnd(&strm);
			return NullFile;
		}

		ASSERT(strm.total_out > 0, "Nothing to decompress");

		ByteArray byteArray = make_shared<vector<byte>>(strm.total_out);

		// allocate actual memory for this
		// copy the bits into that RAM
		// free everything else up!!
		void* curDest = byteArray->data();
		size_t remaining = byteArray->size();

		for (size_t i = 0; i < blocks.size(); ++i)
		{
			ASSERT(remaining > 0);

			size_t copySize = min(remaining, (size_t)chunkSize);

			memcpy(curDest, blocks[i].get(), copySize);
			curDest = (byte*)curDest + copySize;
			remaining -= copySize;
		}

		inflateEnd(&strm);

		return byteArray;
	}

	ByteArray DecompressZippedFile(wstring& fileName)
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
