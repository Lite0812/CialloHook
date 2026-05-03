#pragma once

#include "CompilerHelper.h"
#include "tp_stub.h"

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
			 VType::Destructor>
			(pStream);
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

template <CompilerType compilerType, typename T>
struct CompilerSpecificVector;

template <typename T>
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

template <typename T>
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

template <CompilerType compilerType>
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