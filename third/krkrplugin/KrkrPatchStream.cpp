#include "KrkrPatchStream.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "../../third/miniz/miniz.h"

KrkrPatchStream::KrkrPatchStream(std::vector<BYTE> buffer)
	: data(std::move(buffer))
{
}

tjs_uint64 KrkrPatchStream::Seek(tjs_int64 offset, tjs_int whence)
{
	tjs_int64 next = static_cast<tjs_int64>(pos);
	switch (whence)
	{
	case TJS_BS_SEEK_SET:
		next = offset;
		break;
	case TJS_BS_SEEK_CUR:
		next += offset;
		break;
	case TJS_BS_SEEK_END:
		next = static_cast<tjs_int64>(data.size()) + offset;
		break;
	default:
		break;
	}
	if (next < 0)
	{
		next = 0;
	}
	if (static_cast<size_t>(next) > data.size())
	{
		next = static_cast<tjs_int64>(data.size());
	}
	pos = static_cast<tjs_uint64>(next);
	return pos;
}

tjs_uint KrkrPatchStream::Read(void* buffer, tjs_uint read_size)
{
	if (!buffer || pos >= data.size())
	{
		return 0;
	}
	const size_t available = data.size() - static_cast<size_t>(pos);
	const size_t toRead = std::min<size_t>(read_size, available);
	if (toRead == 0)
	{
		return 0;
	}
	std::memcpy(buffer, data.data() + pos, toRead);
	pos += toRead;
	return static_cast<tjs_uint>(toRead);
}

tjs_uint KrkrPatchStream::Write(const void* /*buffer*/, tjs_uint /*write_size*/)
{
	throw std::runtime_error("KrkrPatchStream is read only");
}

void KrkrPatchStream::SetEndOfStorage()
{
	throw std::runtime_error("KrkrPatchStream is read only");
}

tjs_uint64 KrkrPatchStream::GetSize()
{
	return static_cast<tjs_uint64>(data.size());
}

KrkrPatchSigStream::KrkrPatchSigStream()
	: KrkrPatchStream(std::vector<BYTE>{ 's', 'k', 'i', 'p', '!' })
{
}

KrkrPatchMemoryStream::KrkrPatchMemoryStream(std::vector<BYTE> buffer)
	: KrkrPatchStream(std::move(buffer))
{
}

KrkrPatchArcStream::KrkrPatchArcStream(const std::wstring& patchArc, const XP3ArchiveSegment* segment)
	: KrkrPatchStream(std::vector<BYTE>())
{
	if (!segment)
	{
		throw std::runtime_error("KrkrPatchArcStream missing segment");
	}

	HANDLE hFile = CreateFileW(patchArc.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("KrkrPatchArcStream open archive failed");
	}

	data.resize(static_cast<size_t>(segment->OrgSize));

	LARGE_INTEGER filePos = {};
	filePos.QuadPart = static_cast<LONGLONG>(segment->Start);
	if (!SetFilePointerEx(hFile, filePos, nullptr, FILE_BEGIN))
	{
		CloseHandle(hFile);
		throw std::runtime_error("KrkrPatchArcStream seek archive failed");
	}

	if (segment->IsCompressed)
	{
		std::vector<BYTE> compressedData(static_cast<size_t>(segment->ArcSize));
		DWORD bytesRead = 0;
		if (!::ReadFile(hFile, compressedData.data(), static_cast<DWORD>(compressedData.size()), &bytesRead, nullptr) || bytesRead != compressedData.size())
		{
			CloseHandle(hFile);
			throw std::runtime_error("KrkrPatchArcStream read compressed data failed");
		}

		mz_ulong originalSize = static_cast<mz_ulong>(data.size());
		int z = mz_uncompress(data.data(), &originalSize, compressedData.data(), static_cast<mz_ulong>(compressedData.size()));
		if (z != MZ_OK || originalSize != data.size())
		{
			CloseHandle(hFile);
			throw std::runtime_error("KrkrPatchArcStream decompress failed");
		}
	}
	else
	{
		DWORD bytesRead = 0;
		if (!::ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &bytesRead, nullptr) || bytesRead != data.size())
		{
			CloseHandle(hFile);
			throw std::runtime_error("KrkrPatchArcStream read raw data failed");
		}
	}

	CloseHandle(hFile);
}
