#pragma once

#include <cstdint>

#include "CompilerHelper.h"

typedef __int32 tjs_int32;
typedef unsigned __int32 tjs_uint32;
typedef __int64 tjs_int64;
typedef unsigned __int64 tjs_uint64;
typedef int tjs_int;
typedef unsigned int tjs_uint;
typedef wchar_t tjs_char;

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD __cdecl
#endif

#define TJS_BS_READ 0
#define TJS_BS_SEEK_SET 0
#define TJS_BS_SEEK_CUR 1
#define TJS_BS_SEEK_END 2

#define TJS_VS_SHORT_LEN 21

#pragma pack(push, 4)
struct tTJSVariantString
{
	tjs_int RefCount;
	tjs_char* LongString;
	tjs_char ShortString[TJS_VS_SHORT_LEN + 1];
	tjs_int Length;
	tjs_uint32 HeapFlag;
	tjs_uint32 Hint;
};

class ttstr
{
public:
	tTJSVariantString* Ptr;

	bool IsEmpty() const
	{
		return Ptr == nullptr || Ptr->Length == 0;
	}

	const tjs_char* c_str() const
	{
		if (!Ptr)
		{
			return L"";
		}
		return Ptr->Length > TJS_VS_SHORT_LEN ? Ptr->LongString : Ptr->ShortString;
	}
};
#pragma pack(pop)

template <typename T>
class tTJSHashFunc
{
};

template <>
class tTJSHashFunc<ttstr>
{
};

template <typename KeyT, typename ValueT, typename HashFuncT = tTJSHashFunc<KeyT>, tjs_int HashSize = 64>
class tTJSHashTable
{
private:
	struct element
	{
		tjs_uint32 Hash;
		tjs_uint32 Flags;
		char Key[sizeof(KeyT)];
		char Value[sizeof(ValueT)];
		element* Prev;
		element* Next;
		element* NPrev;
		element* NNext;
	} Elms[HashSize];

	tjs_uint Count;
	element* NFirst;
	element* NLast;
};

template<CompilerType type, typename T>
struct CompilerSpecificVector;

template<typename T>
struct CompilerSpecificVector<CompilerType::Msvc, T>
{
public:
	T* begin()
	{
		return _start;
	}

	T* end()
	{
		return _finish;
	}

private:
	T* _start;
	T* _finish;
	T* _end_of_storage;
};

template<typename T>
struct CompilerSpecificVector<CompilerType::Borland, T>
{
public:
	T* begin()
	{
		return _start;
	}

	T* end()
	{
		return _finish;
	}

private:
	int _buffer_size;
	T* _start;
	T* _finish;
	int paddingC[3];
	T* _end_of_storage;
	int padding1C;
};

class tTJSBinaryStream
{
public:
	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) = 0;
	virtual tjs_uint TJS_INTF_METHOD Read(void* buffer, tjs_uint read_size) = 0;
	virtual tjs_uint TJS_INTF_METHOD Write(const void* buffer, tjs_uint write_size) = 0;
	virtual void TJS_INTF_METHOD SetEndOfStorage() = 0;
	virtual tjs_uint64 TJS_INTF_METHOD GetSize() = 0;

	virtual ~tTJSBinaryStream() = default;

	using VType = CompilerHelper::VType;

	static tTJSBinaryStream* ApplyWrapVTable(tTJSBinaryStream* pStream)
	{
		return CompilerHelper::ApplyWrapVTable
			<VType::Member,
			VType::Member,
			VType::Member,
			VType::Member,
			VType::Member,
			VType::Destructor>(pStream);
	}
};

struct XP3ArchiveSegment
{
	tjs_uint64 Start;
	tjs_uint64 Offset;
	tjs_uint64 OrgSize;
	tjs_uint64 ArcSize;
	bool IsCompressed;
};

using tTVPXP3ArchiveSegment = XP3ArchiveSegment;

#pragma pack(push, 4)
class tTVPArchive
{
public:
	virtual ~tTVPArchive() = default;
	virtual tjs_uint GetCount() = 0;
	virtual ttstr GetName(tjs_uint idx) = 0;
	virtual tTJSBinaryStream* CreateStreamByIndex(tjs_uint idx) = 0;

protected:
	tjs_uint RefCount;
	tTJSHashTable<ttstr, tjs_uint, tTJSHashFunc<ttstr>, 1024> Hash;
	bool Init;
	ttstr ArchiveName;
};

template<CompilerType compilerType>
class tTVPXP3Archive : public tTVPArchive
{
public:
	ttstr Name;

	struct tArchiveItem
	{
		ttstr Name;
		tjs_uint32 FileHash;
		tjs_uint64 OrgSize;
		tjs_uint64 ArcSize;
		CompilerSpecificVector<compilerType, tTVPXP3ArchiveSegment> Segments;
	};

	tjs_int Count;
	CompilerSpecificVector<compilerType, tArchiveItem> ItemVector;
};
#pragma pack(pop)

#pragma pack(push, 4)
class tTVPXP3ArchiveStreamBorland : public tTJSBinaryStream
{
public:
	void* Owner;
	tjs_int StorageIndex;
	void* Segments;
	tTJSBinaryStream* Stream;
	tjs_uint64 OrgSize;
	tjs_int CurSegmentNum;
	XP3ArchiveSegment* CurSegment;
	tjs_int LastOpenedSegmentNum;
	tjs_uint64 CurPos;
	tjs_uint64 SegmentRemain;
	tjs_uint64 SegmentPos;
	void* SegmentData;
	bool SegmentOpened;
};
#pragma pack(pop)

class tTVPXP3ArchiveStreamMsvc : public tTJSBinaryStream
{
public:
	void* Owner;
	tjs_int StorageIndex;
	void* Segments;
	tTJSBinaryStream* Stream;
	tjs_uint64 OrgSize;
	tjs_int CurSegmentNum;
	XP3ArchiveSegment* CurSegment;
	tjs_int LastOpenedSegmentNum;
	tjs_uint64 CurPos;
	tjs_uint64 SegmentRemain;
	tjs_uint64 SegmentPos;
	void* SegmentData;
	bool SegmentOpened;
};
