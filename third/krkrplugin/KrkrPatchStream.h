#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "KrkrDefinitions.h"

class KrkrPatchStream : public tTJSBinaryStream
{
public:
	explicit KrkrPatchStream(std::vector<BYTE> buffer);
	~KrkrPatchStream() override = default;

	tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) final;
	tjs_uint TJS_INTF_METHOD Read(void* buffer, tjs_uint read_size) final;
	tjs_uint TJS_INTF_METHOD Write(const void* buffer, tjs_uint write_size) final;
	void TJS_INTF_METHOD SetEndOfStorage() final;
	tjs_uint64 TJS_INTF_METHOD GetSize() final;

protected:
	std::vector<BYTE> data;

private:
	tjs_uint64 pos = 0;
};

class KrkrPatchSigStream final : public KrkrPatchStream
{
public:
	KrkrPatchSigStream();
};

class KrkrPatchMemoryStream final : public KrkrPatchStream
{
public:
	explicit KrkrPatchMemoryStream(std::vector<BYTE> buffer);
};

class KrkrPatchArcStream final : public KrkrPatchStream
{
public:
	KrkrPatchArcStream(const std::wstring& patchArc, const XP3ArchiveSegment* segment);
};
