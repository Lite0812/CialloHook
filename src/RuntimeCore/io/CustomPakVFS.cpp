#include "CustomPakVFS.h"
#include "Hook_API.h"

#include <Windows.h>
#include <array>
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <vector>
#include <string>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include "../../../third/miniz/miniz.h"
#include "../../../third/zstd/zstd.h"
#include "../../../third/lzma/LzmaDec.h"

namespace Rut
{
	namespace HookX
	{
		namespace
		{
			static constexpr uint8_t kMasterKey[] = {
				0x43,0x69,0x61,0x6C,0x6C,0x6F,0x50,0x61,0x6B,0x43,0x75,0x73,0x74,0x6F,0x6D,0x4B,0x65,0x79,0x3A,0x3A,0x76,0x34
			};
			static constexpr uint8_t kMagic[] = { 0x43,0x69,0x61,0x6C,0x6C,0x6F,0x50,0x41,0x4B };
			static constexpr uint8_t kXp3Magic[] = { 0x58,0x50,0x33,0x0D,0x0A,0x20,0x0A,0x1A,0x8B,0x67,0x01 };
			static constexpr uint16_t kVersion = 4;
			static constexpr uint8_t kModeRaw = 0;
			static constexpr uint8_t kModeZlib = 1;
			static constexpr uint8_t kModeZstd = 2;
			static constexpr uint8_t kModeLzma = 3;
			static constexpr uint8_t kXp3IndexEncodeRaw = 0;
			static constexpr uint8_t kXp3IndexEncodeZlib = 1;
			static constexpr uint8_t kXp3IndexEncodeMask = 0x07;
			static constexpr uint8_t kXp3IndexContinue = 0x80;
			static constexpr uint32_t kXp3ChunkFile = 0x656C6946;
			static constexpr uint32_t kXp3ChunkInfo = 0x6F666E69;
			static constexpr uint32_t kXp3ChunkSegment = 0x6D676573;
			static constexpr size_t kHashSize = 16;
			static constexpr size_t kNonceSize = 12;
			static constexpr size_t kBlockSize = 128;

			struct Blake2bCtx
			{
				uint64_t h[8];
				uint64_t t[2];
				uint64_t f[2];
				uint8_t buf[kBlockSize];
				size_t buflen;
				size_t outlen;
			};

			static const uint64_t blake2bIV[8] = {
				0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
				0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
				0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
				0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
			};

			static const uint8_t blake2bSigma[12][16] = {
				{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
				{14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 },
				{11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4 },
				{ 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8 },
				{ 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13 },
				{ 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9 },
				{12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11 },
				{13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10 },
				{ 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5 },
				{10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0 },
				{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
				{14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 }
			};

			static uint64_t RotR64(uint64_t w, uint64_t c)
			{
				return (w >> c) | (w << (64 - c));
			}

			static uint64_t Load64(const void* src)
			{
				const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
				uint64_t w = 0;
				for (size_t i = 0; i < 8; ++i)
				{
					w |= ((uint64_t)p[i]) << (8 * i);
				}
				return w;
			}

			static void Store64(void* dst, uint64_t w)
			{
				uint8_t* p = reinterpret_cast<uint8_t*>(dst);
				for (size_t i = 0; i < 8; ++i)
				{
					p[i] = (uint8_t)(w >> (8 * i));
				}
			}

			static void Blake2bCompress(Blake2bCtx* ctx, const uint8_t block[kBlockSize])
			{
				uint64_t m[16];
				uint64_t v[16];
				for (size_t i = 0; i < 16; ++i)
				{
					m[i] = Load64(block + i * 8);
				}
				for (size_t i = 0; i < 8; ++i)
				{
					v[i] = ctx->h[i];
				}
				v[8] = blake2bIV[0];
				v[9] = blake2bIV[1];
				v[10] = blake2bIV[2];
				v[11] = blake2bIV[3];
				v[12] = blake2bIV[4] ^ ctx->t[0];
				v[13] = blake2bIV[5] ^ ctx->t[1];
				v[14] = blake2bIV[6] ^ ctx->f[0];
				v[15] = blake2bIV[7] ^ ctx->f[1];

#define B2B_G(a, b, c, d, x, y) \
				do { \
					v[a] = v[a] + v[b] + (x); \
					v[d] = RotR64(v[d] ^ v[a], 32); \
					v[c] = v[c] + v[d]; \
					v[b] = RotR64(v[b] ^ v[c], 24); \
					v[a] = v[a] + v[b] + (y); \
					v[d] = RotR64(v[d] ^ v[a], 16); \
					v[c] = v[c] + v[d]; \
					v[b] = RotR64(v[b] ^ v[c], 63); \
				} while (0)

				for (size_t r = 0; r < 12; ++r)
				{
					B2B_G(0, 4, 8, 12, m[blake2bSigma[r][0]], m[blake2bSigma[r][1]]);
					B2B_G(1, 5, 9, 13, m[blake2bSigma[r][2]], m[blake2bSigma[r][3]]);
					B2B_G(2, 6, 10, 14, m[blake2bSigma[r][4]], m[blake2bSigma[r][5]]);
					B2B_G(3, 7, 11, 15, m[blake2bSigma[r][6]], m[blake2bSigma[r][7]]);
					B2B_G(0, 5, 10, 15, m[blake2bSigma[r][8]], m[blake2bSigma[r][9]]);
					B2B_G(1, 6, 11, 12, m[blake2bSigma[r][10]], m[blake2bSigma[r][11]]);
					B2B_G(2, 7, 8, 13, m[blake2bSigma[r][12]], m[blake2bSigma[r][13]]);
					B2B_G(3, 4, 9, 14, m[blake2bSigma[r][14]], m[blake2bSigma[r][15]]);
				}

#undef B2B_G

				for (size_t i = 0; i < 8; ++i)
				{
					ctx->h[i] ^= v[i] ^ v[i + 8];
				}
			}

			static bool Blake2bInit(Blake2bCtx* ctx, size_t outlen, const uint8_t* key, size_t keylen, const uint8_t* person, size_t personlen)
			{
				if (!ctx || outlen == 0 || outlen > 64 || keylen > 64 || personlen > 16)
				{
					return false;
				}
				for (size_t i = 0; i < 8; ++i)
				{
					ctx->h[i] = blake2bIV[i];
				}
				ctx->t[0] = 0;
				ctx->t[1] = 0;
				ctx->f[0] = 0;
				ctx->f[1] = 0;
				ctx->buflen = 0;
				ctx->outlen = outlen;

				uint8_t param[64] = {};
				param[0] = (uint8_t)outlen;
				param[1] = (uint8_t)keylen;
				param[2] = 1;
				param[3] = 1;
				if (person && personlen > 0)
				{
					memcpy(param + 48, person, personlen);
				}
				for (size_t i = 0; i < 8; ++i)
				{
					ctx->h[i] ^= Load64(param + i * 8);
				}
				if (key && keylen > 0)
				{
					uint8_t block[kBlockSize] = {};
					memcpy(block, key, keylen);
					ctx->t[0] += kBlockSize;
					if (ctx->t[0] < kBlockSize)
					{
						ctx->t[1] += 1;
					}
					Blake2bCompress(ctx, block);
				}
				return true;
			}

			static void Blake2bUpdate(Blake2bCtx* ctx, const uint8_t* in, size_t inlen)
			{
				if (!ctx || !in || inlen == 0)
				{
					return;
				}
				size_t left = ctx->buflen;
				size_t fill = kBlockSize - left;
				if (inlen > fill)
				{
					ctx->buflen = 0;
					memcpy(ctx->buf + left, in, fill);
					ctx->t[0] += kBlockSize;
					if (ctx->t[0] < kBlockSize)
					{
						ctx->t[1] += 1;
					}
					Blake2bCompress(ctx, ctx->buf);
					in += fill;
					inlen -= fill;
					while (inlen > kBlockSize)
					{
						ctx->t[0] += kBlockSize;
						if (ctx->t[0] < kBlockSize)
						{
							ctx->t[1] += 1;
						}
						Blake2bCompress(ctx, in);
						in += kBlockSize;
						inlen -= kBlockSize;
					}
				}
				memcpy(ctx->buf + ctx->buflen, in, inlen);
				ctx->buflen += inlen;
			}

			static void Blake2bFinal(Blake2bCtx* ctx, uint8_t* out, size_t outlen)
			{
				if (!ctx || !out || outlen < ctx->outlen)
				{
					return;
				}
				ctx->t[0] += ctx->buflen;
				if (ctx->t[0] < ctx->buflen)
				{
					ctx->t[1] += 1;
				}
				ctx->f[0] = ~0ULL;
				memset(ctx->buf + ctx->buflen, 0, kBlockSize - ctx->buflen);
				Blake2bCompress(ctx, ctx->buf);
				uint8_t full[64];
				for (size_t i = 0; i < 8; ++i)
				{
					Store64(full + i * 8, ctx->h[i]);
				}
				memcpy(out, full, ctx->outlen);
			}

			static std::vector<uint8_t> Blake2bDigest(size_t outlen, const uint8_t* key, size_t keylen, const uint8_t* person, size_t personlen, const std::vector<std::pair<const uint8_t*, size_t>>& chunks)
			{
				std::vector<uint8_t> out(outlen);
				Blake2bCtx ctx = {};
				if (!Blake2bInit(&ctx, outlen, key, keylen, person, personlen))
				{
					return {};
				}
				for (const auto& c : chunks)
				{
					Blake2bUpdate(&ctx, c.first, c.second);
				}
				Blake2bFinal(&ctx, out.data(), out.size());
				return out;
			}

			static std::wstring NormalizeSlashesW(const std::wstring& value)
			{
				std::wstring out = value;
				for (wchar_t& ch : out)
				{
					if (ch == L'/')
					{
						ch = L'\\';
					}
				}
				return out;
			}

			static std::wstring ToLowerCopyW(const std::wstring& value)
			{
				std::wstring out = value;
				for (wchar_t& ch : out)
				{
					ch = (wchar_t)towlower(ch);
				}
				return out;
			}

			static bool StartsWithNoCase(const std::wstring& textLower, const std::wstring& prefixLower)
			{
				if (prefixLower.empty() || textLower.size() < prefixLower.size())
				{
					return false;
				}
				return _wcsnicmp(textLower.c_str(), prefixLower.c_str(), prefixLower.size()) == 0;
			}

			static std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
			{
				if (left.empty())
				{
					return right;
				}
				if (right.empty())
				{
					return left;
				}
				wchar_t last = left[left.size() - 1];
				if (last == L'\\' || last == L'/')
				{
					return left + right;
				}
				return left + L"\\" + right;
			}

			static bool IsAbsPath(const std::wstring& path)
			{
				if (path.size() >= 2 && path[1] == L':')
				{
					return true;
				}
				if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
				{
					return true;
				}
				return false;
			}

			static bool IsFileExists(const std::wstring& path)
			{
				if (path.empty())
				{
					return false;
				}
				DWORD attr = GetFileAttributesW(path.c_str());
				return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
			}

			static std::wstring GetGameDir()
			{
				wchar_t exePath[MAX_PATH] = {};
				GetModuleFileNameW(nullptr, exePath, MAX_PATH);
				std::wstring path = exePath;
				size_t pos = path.find_last_of(L'\\');
				if (pos != std::wstring::npos)
				{
					path = path.substr(0, pos + 1);
				}
				return NormalizeSlashesW(path);
			}

			static bool BuildRelativePath(const std::wstring& gameDir, const std::wstring& gameDirLower, const wchar_t* originalPath, std::wstring& relativePath)
			{
				relativePath.clear();
				if (!originalPath || originalPath[0] == L'\0')
				{
					return false;
				}
				std::wstring source = NormalizeSlashesW(originalPath);
				bool isAbs = IsAbsPath(source);
				if (!isAbs)
				{
					relativePath = source;
				}
				else
				{
					std::wstring sourceLower = ToLowerCopyW(source);
					if (!StartsWithNoCase(sourceLower, gameDirLower))
					{
						return false;
					}
					relativePath = source.substr(gameDir.size());
				}
				while (!relativePath.empty() && (relativePath[0] == L'.' || relativePath[0] == L'\\'))
				{
					relativePath.erase(relativePath.begin());
				}
				while (!relativePath.empty() && relativePath.back() == L'\\')
				{
					relativePath.pop_back();
				}
				return !relativePath.empty();
			}

			static std::string WToUtf8(const std::wstring& ws)
			{
				if (ws.empty())
				{
					return "";
				}
				int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
				if (len <= 0)
				{
					return "";
				}
				std::string s((size_t)len, '\0');
				WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
				return s;
			}

			static std::wstring ToPosixPath(const std::wstring& path)
			{
				std::wstring out = path;
				for (wchar_t& ch : out)
				{
					if (ch == L'\\')
					{
						ch = L'/';
					}
				}
				while (out.rfind(L"./", 0) == 0)
				{
					out.erase(0, 2);
				}
				while (!out.empty() && out.front() == L'/')
				{
					out.erase(out.begin());
				}
				while (!out.empty() && out.back() == L'/')
				{
					out.pop_back();
				}
				out = ToLowerCopyW(out);
				return out;
			}

			static std::array<uint8_t, 16> HashRelPath(const std::wstring& relPath)
			{
				std::wstring normalized = ToLowerCopyW(relPath);
				std::wstring posix = ToPosixPath(normalized);
				std::string u8 = WToUtf8(posix);
				static const uint8_t person[] = { 'C','i','a','l','l','o','H','a','s','h','V','4' };
				auto digest = Blake2bDigest(16, nullptr, 0, person, sizeof(person), { { reinterpret_cast<const uint8_t*>(u8.data()), u8.size() } });
				std::array<uint8_t, 16> out = {};
				if (digest.size() == out.size())
				{
					memcpy(out.data(), digest.data(), out.size());
				}
				return out;
			}

			static uint16_t ReadU16LE(const uint8_t* p)
			{
				return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
			}

			static uint32_t ReadU32LE(const uint8_t* p)
			{
				return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
			}

			static uint64_t ReadU64LE(const uint8_t* p)
			{
				uint64_t v = 0;
				for (size_t i = 0; i < 8; ++i)
				{
					v |= ((uint64_t)p[i]) << (8 * i);
				}
				return v;
			}

			static std::vector<uint8_t> U64ToLE(uint64_t v)
			{
				std::vector<uint8_t> out(8);
				for (size_t i = 0; i < 8; ++i)
				{
					out[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
				}
				return out;
			}

			static std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& scope, const std::vector<uint8_t>& material, uint64_t size)
			{
				static const uint8_t person[] = { 'C','i','a','l','l','o','K','e','y','V','4' };
				std::vector<uint8_t> sizeLe = U64ToLE(size);
				return Blake2bDigest(32, kMasterKey, sizeof(kMasterKey), person, sizeof(person),
					{
						{ scope.data(), scope.size() },
						{ material.data(), material.size() },
						{ sizeLe.data(), sizeLe.size() }
					});
			}

			static std::vector<uint8_t> XorCrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, const std::array<uint8_t, kNonceSize>& nonce)
			{
				std::vector<uint8_t> out(data.size());
				size_t pos = 0;
				uint64_t counter = 0;
				static const uint8_t person[] = { 'C','i','a','l','l','o','X','o','r','V','4' };
				while (pos < data.size())
				{
					uint8_t counterLe[8];
					for (size_t i = 0; i < 8; ++i)
					{
						counterLe[i] = (uint8_t)((counter >> (8 * i)) & 0xFF);
					}
					auto block = Blake2bDigest(64, key.data(), key.size(), person, sizeof(person),
						{
							{ nonce.data(), nonce.size() },
							{ counterLe, sizeof(counterLe) }
						});
					size_t take = (std::min)(block.size(), data.size() - pos);
					for (size_t i = 0; i < take; ++i)
					{
						out[pos + i] = data[pos + i] ^ block[i];
					}
					pos += take;
					counter += 1;
				}
				return out;
			}

			static bool DecompressZlib(const std::vector<uint8_t>& payload, size_t expectedSize, std::vector<uint8_t>& output)
			{
				if (expectedSize > 0xFFFFFFFFull || payload.size() > 0xFFFFFFFFull)
				{
					return false;
				}
				output.resize(expectedSize);
				mz_ulong dstLen = static_cast<mz_ulong>(expectedSize);
				int z = mz_uncompress(
					reinterpret_cast<mz_uint8*>(output.data()),
					&dstLen,
					reinterpret_cast<const mz_uint8*>(payload.data()),
					static_cast<mz_ulong>(payload.size()));
				if (z != MZ_OK || dstLen != expectedSize)
				{
					output.clear();
					return false;
				}
				return true;
			}

			static bool DecompressZstd(const std::vector<uint8_t>& payload, size_t expectedSize, std::vector<uint8_t>& output, std::string& errorMessage)
			{
				errorMessage.clear();
				output.resize(expectedSize);
				size_t ret = ZSTD_decompress(output.data(), expectedSize, payload.data(), payload.size());
				if (ZSTD_isError(ret))
				{
					const char* name = ZSTD_getErrorName(ret);
					if (name && name[0] != '\0')
					{
						errorMessage = name;
					}
					else
					{
						errorMessage = "zstd decompress failed";
					}
					output.clear();
					return false;
				}
				if (ret != expectedSize)
				{
					errorMessage = "zstd output size mismatch";
					output.clear();
					return false;
				}
				return true;
			}

			static void* LzmaAlloc(ISzAllocPtr, size_t size)
			{
				return std::malloc(size);
			}

			static void LzmaFree(ISzAllocPtr, void* address)
			{
				std::free(address);
			}

			static bool DecompressLzma(const std::vector<uint8_t>& payload, size_t expectedSize, std::vector<uint8_t>& output, std::string& errorMessage)
			{
				errorMessage.clear();
				if (payload.size() < LZMA_PROPS_SIZE)
				{
					errorMessage = "lzma payload too small";
					return false;
				}
				const Byte* props = reinterpret_cast<const Byte*>(payload.data());
				const Byte* src = reinterpret_cast<const Byte*>(payload.data() + LZMA_PROPS_SIZE);
				SizeT srcLen = static_cast<SizeT>(payload.size() - LZMA_PROPS_SIZE);
				output.resize(expectedSize);
				Byte* dst = reinterpret_cast<Byte*>(output.data());
				SizeT dstLen = static_cast<SizeT>(expectedSize);
				ISzAlloc alloc = { LzmaAlloc, LzmaFree };
				ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
				SRes decRes = LzmaDecode(
					dst,
					&dstLen,
					src,
					&srcLen,
					props,
					LZMA_PROPS_SIZE,
					LZMA_FINISH_END,
					&status,
					&alloc);
				if (decRes != SZ_OK)
				{
					errorMessage = "lzma decode failed";
					output.clear();
					return false;
				}
				if (dstLen != expectedSize)
				{
					errorMessage = "lzma output size mismatch";
					output.clear();
					return false;
				}
				if (srcLen != payload.size() - LZMA_PROPS_SIZE)
				{
					errorMessage = "lzma input not fully consumed";
					output.clear();
					return false;
				}
				if (status != LZMA_STATUS_FINISHED_WITH_MARK && status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
				{
					errorMessage = "lzma stream not finished";
					output.clear();
					return false;
				}
				return true;
			}

			static std::wstring NormalizeXp3EntryPath(std::wstring value)
			{
				value = NormalizeSlashesW(value);
				while (!value.empty() && (value[0] == L'.' || value[0] == L'\\'))
				{
					value.erase(value.begin());
				}
				while (!value.empty() && value.back() == L'\\')
				{
					value.pop_back();
				}
				return ToLowerCopyW(value);
			}

			static bool ReadExactly(std::ifstream& fs, void* data, size_t size)
			{
				if (size == 0)
				{
					return true;
				}
				fs.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
				return fs.good() && static_cast<size_t>(fs.gcount()) == size;
			}

			static bool SeekTo(std::ifstream& fs, uint64_t offset)
			{
				if (offset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)()))
				{
					return false;
				}
				fs.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
				return fs.good();
			}

			struct Hash16
			{
				std::array<uint8_t, 16> bytes;

				bool operator==(const Hash16& other) const
				{
					return bytes == other.bytes;
				}
			};

			struct Hash16Hasher
			{
				size_t operator()(const Hash16& h) const
				{
#if INTPTR_MAX == INT64_MAX
					size_t seed = static_cast<size_t>(14695981039346656037ull);
					constexpr size_t prime = static_cast<size_t>(1099511628211ull);
#else
					size_t seed = static_cast<size_t>(2166136261u);
					constexpr size_t prime = static_cast<size_t>(16777619u);
#endif
					for (uint8_t b : h.bytes)
					{
						seed ^= (size_t)b;
						seed *= prime;
					}
					return seed;
				}
			};

			struct PakEntry
			{
				Hash16 hash;
				uint8_t flags = 0;
				uint64_t origSize = 0;
				uint64_t storedSize = 0;
				uint64_t offset = 0;
				std::array<uint8_t, kNonceSize> nonce;
				std::wstring pathKey;
				struct Xp3Segment
				{
					uint32_t flags = 0;
					uint64_t offset = 0;
					uint64_t origSize = 0;
					uint64_t storedSize = 0;
				};
				std::vector<Xp3Segment> segments;
			};

			enum class PakArchiveFormat : uint8_t
			{
				CustomPak,
				Xp3
			};

			struct PakArchive
			{
				std::wstring path;
				std::wstring pathLower;
				PakArchiveFormat format = PakArchiveFormat::CustomPak;
				std::unordered_map<Hash16, PakEntry, Hash16Hasher> hashedEntries;
				std::unordered_map<std::wstring, PakEntry> pathEntries;
				bool indexLoaded = false;
				bool indexLoadAttempted = false;
			};

			static CRITICAL_SECTION sg_lock;
			static bool sg_lockInitialized = false;
			static bool sg_enabled = false;
			static bool sg_enableLog = false;
			static std::wstring sg_gameDir;
			static std::wstring sg_gameDirLower;
			static std::vector<PakArchive> sg_archives;
			static std::unordered_map<std::wstring, std::wstring> sg_resolvedCache;
			static std::unordered_set<std::wstring> sg_missingSet;
			static std::unordered_map<std::wstring, std::shared_ptr<const std::vector<uint8_t>>> sg_extractedDataCache;
			static std::unordered_map<std::wstring, PakArchive> sg_memoryArchiveIndexCache;
			static thread_local uint32_t sg_customPakInternalIoDepth = 0;

			struct InternalIoScope
			{
				InternalIoScope()
				{
					++sg_customPakInternalIoDepth;
				}
				~InternalIoScope()
				{
					if (sg_customPakInternalIoDepth > 0)
					{
						--sg_customPakInternalIoDepth;
					}
				}
			};

			static void LogCustomPakInfo(const wchar_t* format, ...)
			{
				if (!sg_enableLog)
				{
					return;
				}
				va_list args;
				va_start(args, format);
				wchar_t buffer[1024] = {};
				_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
				va_end(args);
				LogMessage(LogLevel::Info, L"[CustomPAK] %s", buffer);
			}

			static void LogCustomPakWarn(const wchar_t* format, ...)
			{
				if (!sg_enableLog)
				{
					return;
				}
				va_list args;
				va_start(args, format);
				wchar_t buffer[1024] = {};
				_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
				va_end(args);
				LogMessage(LogLevel::Warn, L"[CustomPAK] %s", buffer);
			}

			static void EnsureLock()
			{
				if (!sg_lockInitialized)
				{
					InitializeCriticalSection(&sg_lock);
					sg_lockInitialized = true;
				}
			}

			static std::wstring HashHex(const std::array<uint8_t, 16>& hash)
			{
				static const wchar_t* chars = L"0123456789ABCDEF";
				std::wstring s;
				s.reserve(32);
				for (uint8_t b : hash)
				{
					s.push_back(chars[(b >> 4) & 0xF]);
					s.push_back(chars[b & 0xF]);
				}
				return s;
			}

			static bool LoadCustomPakIndex(PakArchive& archive, std::ifstream& fs, const std::vector<uint8_t>& header)
			{
				const std::wstring& pakPath = archive.path;
				uint16_t version = ReadU16LE(header.data() + 9);
				if (version != kVersion)
				{
					LogCustomPakWarn(L"Version mismatch: %s version=%u expected=%u", pakPath.c_str(), (uint32_t)version, (uint32_t)kVersion);
					return false;
				}
				uint64_t indexOffset = ReadU64LE(header.data() + 15);
				uint64_t indexSize = ReadU64LE(header.data() + 23);
				std::array<uint8_t, kNonceSize> indexNonce = {};
				memcpy(indexNonce.data(), header.data() + 31, kNonceSize);
				if (indexSize > static_cast<uint64_t>(SIZE_MAX))
				{
					LogCustomPakWarn(L"Index too large: %s size=%llu", pakPath.c_str(), (unsigned long long)indexSize);
					return false;
				}
				if (!SeekTo(fs, indexOffset))
				{
					LogCustomPakWarn(L"Seek index failed: %s indexOffset=%llu", pakPath.c_str(), (unsigned long long)indexOffset);
					return false;
				}
				std::vector<uint8_t> indexEnc(static_cast<size_t>(indexSize));
				if (!ReadExactly(fs, indexEnc.data(), indexEnc.size()))
				{
					LogCustomPakWarn(L"Read index failed: %s indexSize=%llu", pakPath.c_str(), (unsigned long long)indexSize);
					return false;
				}

				std::vector<uint8_t> scope = { 'I','N','D','E','X' };
				std::vector<uint8_t> material = { '_','_','i','n','d','e','x','_','_' };
				std::vector<uint8_t> indexKey = DeriveKey(scope, material, indexSize);
				std::vector<uint8_t> indexPlain = XorCrypt(indexEnc, indexKey, indexNonce);
				if (indexPlain.size() < 4)
				{
					LogCustomPakWarn(L"Decrypt index failed: %s", pakPath.c_str());
					return false;
				}
				uint32_t count = ReadU32LE(indexPlain.data());
				size_t entrySize = 16 + 1 + 8 + 8 + 8 + 12;
				if (indexPlain.size() < 4 + (size_t)count * entrySize)
				{
					LogCustomPakWarn(L"Index size invalid: %s count=%u plainSize=%llu", pakPath.c_str(), count, (unsigned long long)indexPlain.size());
					return false;
				}
				size_t pos = 4;
				for (uint32_t i = 0; i < count; ++i)
				{
					PakEntry e = {};
					memcpy(e.hash.bytes.data(), indexPlain.data() + pos, 16);
					pos += 16;
					e.flags = indexPlain[pos];
					pos += 1;
					e.origSize = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					e.storedSize = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					e.offset = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					memcpy(e.nonce.data(), indexPlain.data() + pos, 12);
					pos += 12;
					if (archive.hashedEntries.find(e.hash) != archive.hashedEntries.end())
					{
						LogCustomPakWarn(L"Duplicate hash in archive index: %s hash=%s", pakPath.c_str(), HashHex(e.hash.bytes).c_str());
						return false;
					}
					archive.hashedEntries[e.hash] = e;
				}
				archive.format = PakArchiveFormat::CustomPak;
				LogCustomPakInfo(L"CustomPAK loaded: %s entries=%u", pakPath.c_str(), (uint32_t)archive.hashedEntries.size());
				return true;
			}

			static bool ParseXp3FileChunk(PakArchive& archive, const uint8_t* data, size_t size)
			{
				size_t pos = 0;
				PakEntry entry = {};
				bool hasInfo = false;
				while (pos + 12 <= size)
				{
					uint32_t chunkType = ReadU32LE(data + pos);
					pos += 4;
					uint64_t chunkSize64 = ReadU64LE(data + pos);
					pos += 8;
					if (chunkSize64 > static_cast<uint64_t>(size - pos))
					{
						return false;
					}
					size_t chunkSize = static_cast<size_t>(chunkSize64);
					const uint8_t* chunkData = data + pos;

					if (chunkType == kXp3ChunkInfo)
					{
						if (chunkSize < 22)
						{
							return false;
						}
						entry.flags = static_cast<uint8_t>(ReadU32LE(chunkData) & 0xFF);
						entry.origSize = ReadU64LE(chunkData + 4);
						entry.storedSize = ReadU64LE(chunkData + 12);
						uint16_t nameLength = ReadU16LE(chunkData + 20);
						size_t nameBytes = static_cast<size_t>(nameLength) * sizeof(wchar_t);
						if (chunkSize < 22 + nameBytes)
						{
							return false;
						}
						std::wstring fileName;
						fileName.reserve(nameLength);
						for (uint16_t i = 0; i < nameLength; ++i)
						{
							fileName.push_back(static_cast<wchar_t>(ReadU16LE(chunkData + 22 + i * 2)));
						}
						entry.pathKey = NormalizeXp3EntryPath(fileName);
						hasInfo = !entry.pathKey.empty();
					}
					else if (chunkType == kXp3ChunkSegment)
					{
						if ((chunkSize % 28) != 0)
						{
							return false;
						}
						size_t segmentCount = chunkSize / 28;
						entry.segments.reserve(entry.segments.size() + segmentCount);
						for (size_t i = 0; i < segmentCount; ++i)
						{
							const uint8_t* segmentData = chunkData + i * 28;
							PakEntry::Xp3Segment segment = {};
							segment.flags = ReadU32LE(segmentData);
							segment.offset = ReadU64LE(segmentData + 4);
							segment.origSize = ReadU64LE(segmentData + 12);
							segment.storedSize = ReadU64LE(segmentData + 20);
							entry.segments.push_back(segment);
						}
					}

					pos += chunkSize;
				}
				if (pos != size || !hasInfo || entry.segments.empty())
				{
					return false;
				}
				uint64_t totalOrig = 0;
				uint64_t totalStored = 0;
				for (const PakEntry::Xp3Segment& segment : entry.segments)
				{
					totalOrig += segment.origSize;
					totalStored += segment.storedSize;
				}
				if (entry.origSize == 0)
				{
					entry.origSize = totalOrig;
				}
				if (entry.storedSize == 0)
				{
					entry.storedSize = totalStored;
				}
				if (archive.pathEntries.find(entry.pathKey) != archive.pathEntries.end())
				{
					LogCustomPakWarn(L"Duplicate XP3 path in archive index: %s path=%s", archive.path.c_str(), entry.pathKey.c_str());
					return false;
				}
				archive.pathEntries[entry.pathKey] = std::move(entry);
				return true;
			}

			static bool LoadXp3Index(PakArchive& archive, std::ifstream& fs, const std::vector<uint8_t>& header)
			{
				const std::wstring& pakPath = archive.path;
				uint64_t indexOffset = ReadU64LE(header.data() + sizeof(kXp3Magic));
				for (;;)
				{
					if (!SeekTo(fs, indexOffset))
					{
						LogCustomPakWarn(L"Seek XP3 index failed: %s indexOffset=%llu", pakPath.c_str(), (unsigned long long)indexOffset);
						return false;
					}
					uint8_t indexFlags = 0;
					uint8_t sizeBytes[8] = {};
					if (!ReadExactly(fs, &indexFlags, 1) || !ReadExactly(fs, sizeBytes, sizeof(sizeBytes)))
					{
						LogCustomPakWarn(L"Read XP3 index header failed: %s", pakPath.c_str());
						return false;
					}
					uint64_t indexSize = ReadU64LE(sizeBytes);
					if ((indexFlags & kXp3IndexContinue) != 0)
					{
						if ((indexFlags & kXp3IndexEncodeMask) != kXp3IndexEncodeRaw || indexSize != 0)
						{
							LogCustomPakWarn(L"Unsupported XP3 continue index: %s flags=0x%02X size=%llu", pakPath.c_str(), (uint32_t)indexFlags, (unsigned long long)indexSize);
							return false;
						}
						uint8_t nextOffsetBytes[8] = {};
						if (!ReadExactly(fs, nextOffsetBytes, sizeof(nextOffsetBytes)))
						{
							LogCustomPakWarn(L"Read XP3 next index offset failed: %s", pakPath.c_str());
							return false;
						}
						indexOffset = ReadU64LE(nextOffsetBytes);
						continue;
					}

					std::vector<uint8_t> indexData;
					uint8_t encodeMethod = indexFlags & kXp3IndexEncodeMask;
					if (encodeMethod == kXp3IndexEncodeRaw)
					{
						if (indexSize > static_cast<uint64_t>(SIZE_MAX))
						{
							LogCustomPakWarn(L"XP3 raw index too large: %s size=%llu", pakPath.c_str(), (unsigned long long)indexSize);
							return false;
						}
						indexData.resize(static_cast<size_t>(indexSize));
						if (!ReadExactly(fs, indexData.data(), indexData.size()))
						{
							LogCustomPakWarn(L"Read XP3 raw index failed: %s size=%llu", pakPath.c_str(), (unsigned long long)indexSize);
							return false;
						}
					}
					else if (encodeMethod == kXp3IndexEncodeZlib)
					{
						uint8_t unpackedSizeBytes[8] = {};
						if (!ReadExactly(fs, unpackedSizeBytes, sizeof(unpackedSizeBytes)))
						{
							LogCustomPakWarn(L"Read XP3 unpacked index size failed: %s", pakPath.c_str());
							return false;
						}
						uint64_t unpackedSize = ReadU64LE(unpackedSizeBytes);
						if (indexSize > static_cast<uint64_t>(SIZE_MAX) || unpackedSize > static_cast<uint64_t>(SIZE_MAX))
						{
							LogCustomPakWarn(L"XP3 zlib index too large: %s packed=%llu unpacked=%llu", pakPath.c_str(), (unsigned long long)indexSize, (unsigned long long)unpackedSize);
							return false;
						}
						std::vector<uint8_t> compressedIndex(static_cast<size_t>(indexSize));
						if (!ReadExactly(fs, compressedIndex.data(), compressedIndex.size()))
						{
							LogCustomPakWarn(L"Read XP3 compressed index failed: %s size=%llu", pakPath.c_str(), (unsigned long long)indexSize);
							return false;
						}
						if (!DecompressZlib(compressedIndex, static_cast<size_t>(unpackedSize), indexData))
						{
							LogCustomPakWarn(L"Decompress XP3 index failed: %s packed=%llu unpacked=%llu", pakPath.c_str(), (unsigned long long)indexSize, (unsigned long long)unpackedSize);
							return false;
						}
					}
					else
					{
						LogCustomPakWarn(L"Unsupported XP3 index encode method: %s flags=0x%02X", pakPath.c_str(), (uint32_t)indexFlags);
						return false;
					}

					size_t pos = 0;
					while (pos + 12 <= indexData.size())
					{
						uint32_t chunkType = ReadU32LE(indexData.data() + pos);
						pos += 4;
						uint64_t chunkSize64 = ReadU64LE(indexData.data() + pos);
						pos += 8;
						if (chunkSize64 > static_cast<uint64_t>(indexData.size() - pos))
						{
							LogCustomPakWarn(L"XP3 chunk size invalid: %s chunk=%u size=%llu", pakPath.c_str(), chunkType, (unsigned long long)chunkSize64);
							return false;
						}
						size_t chunkSize = static_cast<size_t>(chunkSize64);
						if (chunkType == kXp3ChunkFile && !ParseXp3FileChunk(archive, indexData.data() + pos, chunkSize))
						{
							LogCustomPakWarn(L"Parse XP3 file chunk failed: %s", pakPath.c_str());
							return false;
						}
						pos += chunkSize;
					}
					if (pos != indexData.size())
					{
						LogCustomPakWarn(L"XP3 index trailing data invalid: %s pos=%llu size=%llu", pakPath.c_str(), (unsigned long long)pos, (unsigned long long)indexData.size());
						return false;
					}
					break;
				}
				archive.format = PakArchiveFormat::Xp3;
				LogCustomPakInfo(L"XP3 loaded: %s entries=%u", pakPath.c_str(), (uint32_t)archive.pathEntries.size());
				return !archive.pathEntries.empty();
			}

			static bool LoadArchiveIndex(PakArchive& archive)
			{
				InternalIoScope ioScope;
				archive.hashedEntries.clear();
				archive.pathEntries.clear();
				const std::wstring& pakPath = archive.path;

				std::ifstream fs(pakPath, std::ios::binary);
				if (!fs.good())
				{
					LogCustomPakWarn(L"Open archive failed: %s", pakPath.c_str());
					return false;
				}
				std::vector<uint8_t> header(43);
				if (!ReadExactly(fs, header.data(), header.size()))
				{
					LogCustomPakWarn(L"Read header failed: %s", pakPath.c_str());
					return false;
				}
				if (memcmp(header.data(), kMagic, sizeof(kMagic)) == 0)
				{
					return LoadCustomPakIndex(archive, fs, header);
				}
				if (memcmp(header.data(), kXp3Magic, sizeof(kXp3Magic)) == 0)
				{
					return LoadXp3Index(archive, fs, header);
				}
				LogCustomPakWarn(L"Unsupported archive magic: %s", pakPath.c_str());
				return false;
			}

			static bool LoadCustomPakIndexFromMemory(PakArchive& archive, const uint8_t* data, size_t size)
			{
				const std::wstring& pakPath = archive.path;
				if (!data || size < 43)
				{
					LogCustomPakWarn(L"CustomPAK memory header too small: %s size=%llu", pakPath.c_str(), (unsigned long long)size);
					return false;
				}
				uint16_t version = ReadU16LE(data + 9);
				if (version != kVersion)
				{
					LogCustomPakWarn(L"Memory version mismatch: %s version=%u expected=%u", pakPath.c_str(), (uint32_t)version, (uint32_t)kVersion);
					return false;
				}
				uint64_t indexOffset = ReadU64LE(data + 15);
				uint64_t indexSize = ReadU64LE(data + 23);
				std::array<uint8_t, kNonceSize> indexNonce = {};
				memcpy(indexNonce.data(), data + 31, kNonceSize);
				if (indexOffset > static_cast<uint64_t>(size) || indexSize > static_cast<uint64_t>(size) - indexOffset)
				{
					LogCustomPakWarn(L"Memory index range invalid: %s offset=%llu size=%llu total=%llu", pakPath.c_str(), (unsigned long long)indexOffset, (unsigned long long)indexSize, (unsigned long long)size);
					return false;
				}

				std::vector<uint8_t> indexEnc(data + static_cast<size_t>(indexOffset), data + static_cast<size_t>(indexOffset + indexSize));
				std::vector<uint8_t> scope = { 'I','N','D','E','X' };
				std::vector<uint8_t> material = { '_','_','i','n','d','e','x','_','_' };
				std::vector<uint8_t> indexKey = DeriveKey(scope, material, indexSize);
				std::vector<uint8_t> indexPlain = XorCrypt(indexEnc, indexKey, indexNonce);
				if (indexPlain.size() < 4)
				{
					LogCustomPakWarn(L"Memory decrypt index failed: %s", pakPath.c_str());
					return false;
				}

				uint32_t count = ReadU32LE(indexPlain.data());
				size_t entrySize = 16 + 1 + 8 + 8 + 8 + 12;
				if (indexPlain.size() < 4 + static_cast<size_t>(count) * entrySize)
				{
					LogCustomPakWarn(L"Memory index size invalid: %s count=%u plainSize=%llu", pakPath.c_str(), count, (unsigned long long)indexPlain.size());
					return false;
				}

				size_t pos = 4;
				for (uint32_t i = 0; i < count; ++i)
				{
					PakEntry e = {};
					memcpy(e.hash.bytes.data(), indexPlain.data() + pos, 16);
					pos += 16;
					e.flags = indexPlain[pos];
					pos += 1;
					e.origSize = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					e.storedSize = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					e.offset = ReadU64LE(indexPlain.data() + pos);
					pos += 8;
					memcpy(e.nonce.data(), indexPlain.data() + pos, 12);
					pos += 12;
					if (archive.hashedEntries.find(e.hash) != archive.hashedEntries.end())
					{
						LogCustomPakWarn(L"Duplicate hash in memory archive index: %s hash=%s", pakPath.c_str(), HashHex(e.hash.bytes).c_str());
						return false;
					}
					archive.hashedEntries[e.hash] = e;
				}

				archive.format = PakArchiveFormat::CustomPak;
				LogCustomPakInfo(L"CustomPAK memory loaded: %s entries=%u", pakPath.c_str(), (uint32_t)archive.hashedEntries.size());
				return true;
			}

			static bool LoadXp3IndexFromMemory(PakArchive& archive, const uint8_t* data, size_t size)
			{
				const std::wstring& pakPath = archive.path;
				if (!data || size < sizeof(kXp3Magic) + 8)
				{
					LogCustomPakWarn(L"XP3 memory header too small: %s size=%llu", pakPath.c_str(), (unsigned long long)size);
					return false;
				}

				uint64_t indexOffset = ReadU64LE(data + sizeof(kXp3Magic));
				for (;;)
				{
					if (indexOffset > static_cast<uint64_t>(size) || static_cast<size_t>(indexOffset) + 9 > size)
					{
						LogCustomPakWarn(L"Memory XP3 index header out of range: %s offset=%llu total=%llu", pakPath.c_str(), (unsigned long long)indexOffset, (unsigned long long)size);
						return false;
					}

					const uint8_t* p = data + static_cast<size_t>(indexOffset);
					uint8_t indexFlags = p[0];
					uint64_t indexSize = ReadU64LE(p + 1);
					size_t pos = static_cast<size_t>(indexOffset) + 9;

					if ((indexFlags & kXp3IndexContinue) != 0)
					{
						if ((indexFlags & kXp3IndexEncodeMask) != kXp3IndexEncodeRaw || indexSize != 0)
						{
							LogCustomPakWarn(L"Unsupported memory XP3 continue index: %s flags=0x%02X size=%llu", pakPath.c_str(), (uint32_t)indexFlags, (unsigned long long)indexSize);
							return false;
						}
						if (pos + 8 > size)
						{
							LogCustomPakWarn(L"Memory XP3 next index offset missing: %s", pakPath.c_str());
							return false;
						}
						indexOffset = ReadU64LE(data + pos);
						continue;
					}

					std::vector<uint8_t> indexData;
					uint8_t encodeMethod = indexFlags & kXp3IndexEncodeMask;
					if (encodeMethod == kXp3IndexEncodeRaw)
					{
						if (indexSize > static_cast<uint64_t>(size) - pos)
						{
							LogCustomPakWarn(L"Memory XP3 raw index range invalid: %s size=%llu total=%llu", pakPath.c_str(), (unsigned long long)indexSize, (unsigned long long)size);
							return false;
						}
						indexData.assign(data + pos, data + pos + static_cast<size_t>(indexSize));
					}
					else if (encodeMethod == kXp3IndexEncodeZlib)
					{
						if (pos + 8 > size)
						{
							LogCustomPakWarn(L"Memory XP3 unpacked index size missing: %s", pakPath.c_str());
							return false;
						}
						uint64_t unpackedSize = ReadU64LE(data + pos);
						pos += 8;
						if (indexSize > static_cast<uint64_t>(size) - pos || unpackedSize > static_cast<uint64_t>(SIZE_MAX))
						{
							LogCustomPakWarn(L"Memory XP3 zlib index range invalid: %s packed=%llu unpacked=%llu total=%llu", pakPath.c_str(), (unsigned long long)indexSize, (unsigned long long)unpackedSize, (unsigned long long)size);
							return false;
						}
						std::vector<uint8_t> compressedIndex(data + pos, data + pos + static_cast<size_t>(indexSize));
						if (!DecompressZlib(compressedIndex, static_cast<size_t>(unpackedSize), indexData))
						{
							LogCustomPakWarn(L"Memory XP3 index decompress failed: %s packed=%llu unpacked=%llu", pakPath.c_str(), (unsigned long long)indexSize, (unsigned long long)unpackedSize);
							return false;
						}
					}
					else
					{
						LogCustomPakWarn(L"Unsupported memory XP3 index encode method: %s flags=0x%02X", pakPath.c_str(), (uint32_t)indexFlags);
						return false;
					}

					size_t chunkPos = 0;
					while (chunkPos + 12 <= indexData.size())
					{
						uint32_t chunkType = ReadU32LE(indexData.data() + chunkPos);
						chunkPos += 4;
						uint64_t chunkSize64 = ReadU64LE(indexData.data() + chunkPos);
						chunkPos += 8;
						if (chunkSize64 > static_cast<uint64_t>(indexData.size() - chunkPos))
						{
							LogCustomPakWarn(L"Memory XP3 chunk size invalid: %s chunk=%u size=%llu", pakPath.c_str(), chunkType, (unsigned long long)chunkSize64);
							return false;
						}
						size_t chunkSize = static_cast<size_t>(chunkSize64);
						if (chunkType == kXp3ChunkFile && !ParseXp3FileChunk(archive, indexData.data() + chunkPos, chunkSize))
						{
							LogCustomPakWarn(L"Parse memory XP3 file chunk failed: %s", pakPath.c_str());
							return false;
						}
						chunkPos += chunkSize;
					}
					if (chunkPos != indexData.size())
					{
						LogCustomPakWarn(L"Memory XP3 index trailing data invalid: %s pos=%llu size=%llu", pakPath.c_str(), (unsigned long long)chunkPos, (unsigned long long)indexData.size());
						return false;
					}
					break;
				}

				archive.format = PakArchiveFormat::Xp3;
				LogCustomPakInfo(L"XP3 memory loaded: %s entries=%u", pakPath.c_str(), (uint32_t)archive.pathEntries.size());
				return !archive.pathEntries.empty();
			}

			static bool LoadArchiveIndexFromMemory(PakArchive& archive, const uint8_t* data, size_t size)
			{
				archive.hashedEntries.clear();
				archive.pathEntries.clear();
				if (!data || size == 0)
				{
					return false;
				}
				if (size >= sizeof(kMagic) && memcmp(data, kMagic, sizeof(kMagic)) == 0)
				{
					return LoadCustomPakIndexFromMemory(archive, data, size);
				}
				if (size >= sizeof(kXp3Magic) && memcmp(data, kXp3Magic, sizeof(kXp3Magic)) == 0)
				{
					return LoadXp3IndexFromMemory(archive, data, size);
				}
				LogCustomPakWarn(L"Unsupported memory archive magic: %s size=%llu", archive.path.c_str(), (unsigned long long)size);
				return false;
			}

			static bool EnsureArchiveIndexLoaded(PakArchive& archive)
			{
				if (archive.indexLoaded)
				{
					return true;
				}
				if (archive.indexLoadAttempted)
				{
					return false;
				}
				archive.indexLoadAttempted = true;
				if (!LoadArchiveIndex(archive))
				{
					LogCustomPakWarn(L"Load archive index failed: %s", archive.path.c_str());
					return false;
				}
				archive.indexLoaded = true;
				return true;
			}

			static bool ReadEntryRaw(const PakArchive& archive, const PakEntry& entry, std::vector<uint8_t>& raw)
			{
				InternalIoScope ioScope;
				raw.clear();
				if (entry.storedSize > (uint64_t)SIZE_MAX || entry.origSize > (uint64_t)SIZE_MAX)
				{
					LogCustomPakWarn(L"Entry size overflow: archive=%s stored=%llu orig=%llu", archive.path.c_str(), (unsigned long long)entry.storedSize, (unsigned long long)entry.origSize);
					return false;
				}
				std::ifstream fs(archive.path, std::ios::binary);
				if (!fs.good())
				{
					LogCustomPakWarn(L"Open archive failed when read entry: %s", archive.path.c_str());
					return false;
				}
				if (archive.format == PakArchiveFormat::Xp3)
				{
					raw.reserve(static_cast<size_t>(entry.origSize));
					for (const PakEntry::Xp3Segment& segment : entry.segments)
					{
						if (segment.storedSize > static_cast<uint64_t>(SIZE_MAX) || segment.origSize > static_cast<uint64_t>(SIZE_MAX))
						{
							LogCustomPakWarn(L"XP3 segment size overflow: archive=%s", archive.path.c_str());
							return false;
						}
						if (!SeekTo(fs, segment.offset))
						{
							LogCustomPakWarn(L"Seek XP3 segment failed: archive=%s offset=%llu", archive.path.c_str(), (unsigned long long)segment.offset);
							return false;
						}
						std::vector<uint8_t> segmentData(static_cast<size_t>(segment.storedSize));
						if (!ReadExactly(fs, segmentData.data(), segmentData.size()))
						{
							LogCustomPakWarn(L"Read XP3 segment failed: archive=%s size=%llu", archive.path.c_str(), (unsigned long long)segment.storedSize);
							return false;
						}
						std::vector<uint8_t> decodedSegment;
						if ((segment.flags & 0x07u) != 0)
						{
							if (!DecompressZlib(segmentData, static_cast<size_t>(segment.origSize), decodedSegment))
							{
								LogCustomPakWarn(L"Decompress XP3 segment failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)segmentData.size(), (unsigned long long)segment.origSize);
								return false;
							}
						}
						else
						{
							decodedSegment = std::move(segmentData);
						}
						raw.insert(raw.end(), decodedSegment.begin(), decodedSegment.end());
					}
					if (raw.size() != static_cast<size_t>(entry.origSize))
					{
						LogCustomPakWarn(L"XP3 entry size mismatch: archive=%s raw=%llu expected=%llu", archive.path.c_str(), (unsigned long long)raw.size(), (unsigned long long)entry.origSize);
						return false;
					}
					return true;
				}

				if (!SeekTo(fs, entry.offset))
				{
					LogCustomPakWarn(L"Seek entry failed: archive=%s offset=%llu", archive.path.c_str(), (unsigned long long)entry.offset);
					return false;
				}
				std::vector<uint8_t> enc((size_t)entry.storedSize);
				if (!ReadExactly(fs, enc.data(), enc.size()))
				{
					LogCustomPakWarn(L"Read entry payload failed: archive=%s size=%llu", archive.path.c_str(), (unsigned long long)entry.storedSize);
					return false;
				}
				std::vector<uint8_t> scope = { 'D','A','T','A' };
				std::vector<uint8_t> material(entry.nonce.begin(), entry.nonce.end());
				std::vector<uint8_t> key = DeriveKey(scope, material, entry.origSize);
				std::vector<uint8_t> packed = XorCrypt(enc, key, entry.nonce);
				if (packed.empty())
				{
					LogCustomPakWarn(L"Decrypt entry failed: archive=%s", archive.path.c_str());
					return false;
				}
				uint8_t mode = packed[0];
				std::vector<uint8_t> payload(packed.begin() + 1, packed.end());
				if (mode == kModeRaw)
				{
					raw = std::move(payload);
				}
				else if (mode == kModeZlib)
				{
					if (!DecompressZlib(payload, (size_t)entry.origSize, raw))
					{
						LogCustomPakWarn(L"Decompress zlib failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						return false;
					}
				}
				else if (mode == kModeZstd)
				{
					std::string zstdErr;
					if (!DecompressZstd(payload, (size_t)entry.origSize, raw, zstdErr))
					{
						LogCustomPakWarn(L"Decompress zstd failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						if (!zstdErr.empty())
						{
							std::wstring werr(zstdErr.begin(), zstdErr.end());
							LogCustomPakWarn(L"Zstd detail: %s", werr.c_str());
						}
						return false;
					}
				}
				else if (mode == kModeLzma)
				{
					std::string lzmaErr;
					if (!DecompressLzma(payload, (size_t)entry.origSize, raw, lzmaErr))
					{
						LogCustomPakWarn(L"Decompress lzma failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						if (!lzmaErr.empty())
						{
							std::wstring werr(lzmaErr.begin(), lzmaErr.end());
							LogCustomPakWarn(L"Lzma detail: %s", werr.c_str());
						}
						return false;
					}
				}
				else
				{
					LogCustomPakWarn(L"Unknown entry mode: archive=%s mode=%u", archive.path.c_str(), (uint32_t)mode);
					return false;
				}
				if (raw.size() != (size_t)entry.origSize)
				{
					LogCustomPakWarn(L"Entry size mismatch: archive=%s raw=%llu expected=%llu", archive.path.c_str(), (unsigned long long)raw.size(), (unsigned long long)entry.origSize);
					return false;
				}
				return true;
			}

			static bool ReadEntryRawFromMemory(const PakArchive& archive, const PakEntry& entry, const uint8_t* data, size_t size, std::vector<uint8_t>& raw)
			{
				raw.clear();
				if (!data)
				{
					return false;
				}
				if (entry.storedSize > static_cast<uint64_t>(SIZE_MAX) || entry.origSize > static_cast<uint64_t>(SIZE_MAX))
				{
					LogCustomPakWarn(L"Memory entry size overflow: archive=%s stored=%llu orig=%llu", archive.path.c_str(), (unsigned long long)entry.storedSize, (unsigned long long)entry.origSize);
					return false;
				}

				if (archive.format == PakArchiveFormat::Xp3)
				{
					raw.reserve(static_cast<size_t>(entry.origSize));
					for (const PakEntry::Xp3Segment& segment : entry.segments)
					{
						if (segment.storedSize > static_cast<uint64_t>(SIZE_MAX) || segment.origSize > static_cast<uint64_t>(SIZE_MAX))
						{
							LogCustomPakWarn(L"Memory XP3 segment size overflow: archive=%s", archive.path.c_str());
							return false;
						}
						if (segment.offset > static_cast<uint64_t>(size) || segment.storedSize > static_cast<uint64_t>(size) - segment.offset)
						{
							LogCustomPakWarn(L"Memory XP3 segment range invalid: archive=%s offset=%llu size=%llu total=%llu", archive.path.c_str(), (unsigned long long)segment.offset, (unsigned long long)segment.storedSize, (unsigned long long)size);
							return false;
						}
						std::vector<uint8_t> segmentData(
							data + static_cast<size_t>(segment.offset),
							data + static_cast<size_t>(segment.offset + segment.storedSize));
						std::vector<uint8_t> decodedSegment;
						if ((segment.flags & 0x07u) != 0)
						{
							if (!DecompressZlib(segmentData, static_cast<size_t>(segment.origSize), decodedSegment))
							{
								LogCustomPakWarn(L"Memory XP3 segment decompress failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)segmentData.size(), (unsigned long long)segment.origSize);
								return false;
							}
						}
						else
						{
							decodedSegment = std::move(segmentData);
						}
						raw.insert(raw.end(), decodedSegment.begin(), decodedSegment.end());
					}
					if (raw.size() != static_cast<size_t>(entry.origSize))
					{
						LogCustomPakWarn(L"Memory XP3 entry size mismatch: archive=%s raw=%llu expected=%llu", archive.path.c_str(), (unsigned long long)raw.size(), (unsigned long long)entry.origSize);
						return false;
					}
					return true;
				}

				if (entry.offset > static_cast<uint64_t>(size) || entry.storedSize > static_cast<uint64_t>(size) - entry.offset)
				{
					LogCustomPakWarn(L"Memory entry range invalid: archive=%s offset=%llu size=%llu total=%llu", archive.path.c_str(), (unsigned long long)entry.offset, (unsigned long long)entry.storedSize, (unsigned long long)size);
					return false;
				}

				std::vector<uint8_t> enc(
					data + static_cast<size_t>(entry.offset),
					data + static_cast<size_t>(entry.offset + entry.storedSize));
				std::vector<uint8_t> scope = { 'D','A','T','A' };
				std::vector<uint8_t> material(entry.nonce.begin(), entry.nonce.end());
				std::vector<uint8_t> key = DeriveKey(scope, material, entry.origSize);
				std::vector<uint8_t> packed = XorCrypt(enc, key, entry.nonce);
				if (packed.empty())
				{
					LogCustomPakWarn(L"Memory decrypt entry failed: archive=%s", archive.path.c_str());
					return false;
				}
				uint8_t mode = packed[0];
				std::vector<uint8_t> payload(packed.begin() + 1, packed.end());
				if (mode == kModeRaw)
				{
					raw = std::move(payload);
				}
				else if (mode == kModeZlib)
				{
					if (!DecompressZlib(payload, static_cast<size_t>(entry.origSize), raw))
					{
						LogCustomPakWarn(L"Memory zlib decompress failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						return false;
					}
				}
				else if (mode == kModeZstd)
				{
					std::string zstdErr;
					if (!DecompressZstd(payload, static_cast<size_t>(entry.origSize), raw, zstdErr))
					{
						LogCustomPakWarn(L"Memory zstd decompress failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						return false;
					}
				}
				else if (mode == kModeLzma)
				{
					std::string lzmaErr;
					if (!DecompressLzma(payload, static_cast<size_t>(entry.origSize), raw, lzmaErr))
					{
						LogCustomPakWarn(L"Memory lzma decompress failed: archive=%s compressed=%llu expected=%llu", archive.path.c_str(), (unsigned long long)payload.size(), (unsigned long long)entry.origSize);
						return false;
					}
				}
				else
				{
					LogCustomPakWarn(L"Unknown memory entry mode: archive=%s mode=%u", archive.path.c_str(), (uint32_t)mode);
					return false;
				}
				if (raw.size() != static_cast<size_t>(entry.origSize))
				{
					LogCustomPakWarn(L"Memory entry size mismatch: archive=%s raw=%llu expected=%llu", archive.path.c_str(), (unsigned long long)raw.size(), (unsigned long long)entry.origSize);
					return false;
				}
				return true;
			}

			static const PakEntry* FindArchiveEntry(const PakArchive& archive, const std::wstring& relativePath, const Hash16& hashKey)
			{
				if (archive.format == PakArchiveFormat::Xp3)
				{
					std::wstring pathKey = NormalizeXp3EntryPath(relativePath);
					auto itPath = archive.pathEntries.find(pathKey);
					if (itPath == archive.pathEntries.end())
					{
						return nullptr;
					}
					return &itPath->second;
				}
				auto itHash = archive.hashedEntries.find(hashKey);
				if (itHash == archive.hashedEntries.end())
				{
					return nullptr;
				}
				return &itHash->second;
			}

			static bool SplitArchiveChainPath(const std::wstring& archivePath, std::wstring& outerArchivePath, std::vector<std::wstring>& nestedArchivePaths)
			{
				outerArchivePath.clear();
				nestedArchivePaths.clear();
				if (archivePath.empty())
				{
					return false;
				}

				std::wstring normalized = NormalizeSlashesW(archivePath);
				size_t begin = 0;
				while (begin <= normalized.size())
				{
					size_t pos = normalized.find(L'>', begin);
					std::wstring segment = normalized.substr(begin, pos == std::wstring::npos ? std::wstring::npos : (pos - begin));
					while (!segment.empty() && iswspace(static_cast<wint_t>(segment.front())) != 0)
					{
						segment.erase(segment.begin());
					}
					while (!segment.empty() && iswspace(static_cast<wint_t>(segment.back())) != 0)
					{
						segment.pop_back();
					}
					if (segment.empty())
					{
						return false;
					}
					if (outerArchivePath.empty())
					{
						outerArchivePath = segment;
					}
					else
					{
						nestedArchivePaths.push_back(segment);
					}
					if (pos == std::wstring::npos)
					{
						break;
					}
					begin = pos + 1;
				}
				return !outerArchivePath.empty();
			}

			static std::wstring BuildArchiveChainDisplayPath(const std::wstring& outerArchivePath, const std::vector<std::wstring>& nestedArchivePaths)
			{
				std::wstring displayPath = outerArchivePath;
				for (const std::wstring& nestedArchive : nestedArchivePaths)
				{
					displayPath += L" :: ";
					displayPath += nestedArchive;
				}
				return displayPath;
			}

			static std::wstring BuildArchiveChainCacheKey(const std::wstring& outerArchivePath, const std::vector<std::wstring>& nestedArchivePaths)
			{
				std::wstring cacheKey = ToLowerCopyW(outerArchivePath);
				for (const std::wstring& nestedArchive : nestedArchivePaths)
				{
					cacheKey += L"|arc|";
					cacheKey += ToLowerCopyW(nestedArchive);
				}
				return cacheKey;
			}

			static bool ResolveArchiveDataFromMemory(const std::wstring& archiveTag, const uint8_t* data, size_t size, const std::wstring& relativePath, std::vector<uint8_t>& raw)
			{
				std::wstring archiveTagLower = ToLowerCopyW(archiveTag);
				PakArchive memoryArchive = {};
				auto itCached = sg_memoryArchiveIndexCache.find(archiveTagLower);
				if (itCached != sg_memoryArchiveIndexCache.end())
				{
					memoryArchive = itCached->second;
				}
				else
				{
					memoryArchive.path = archiveTag;
					memoryArchive.pathLower = archiveTagLower;
					if (!LoadArchiveIndexFromMemory(memoryArchive, data, size))
					{
						return false;
					}
					memoryArchive.indexLoaded = true;
					memoryArchive.indexLoadAttempted = true;
					sg_memoryArchiveIndexCache.emplace(archiveTagLower, memoryArchive);
				}

				Hash16 key = { HashRelPath(relativePath) };
				const PakEntry* entry = FindArchiveEntry(memoryArchive, relativePath, key);
				if (!entry)
				{
					LogCustomPakInfo(L"Resolve memory archive miss archive=%s relative=%s", archiveTag.c_str(), relativePath.c_str());
					return false;
				}
				return ReadEntryRawFromMemory(memoryArchive, *entry, data, size, raw);
			}

			static std::wstring BuildArchiveResolvedKey(const PakArchive& archive, const std::wstring& relativePath, const std::array<uint8_t, 16>& hash)
			{
				if (archive.format == PakArchiveFormat::Xp3)
				{
					return archive.pathLower + L"|xp3|" + NormalizeXp3EntryPath(relativePath);
				}
				return archive.pathLower + L"|cpk|" + HashHex(hash);
			}

		}

		void ConfigureCustomPakVFS(bool enable, const wchar_t* const* pakPaths, size_t pakCount, bool enableLog)
		{
			EnsureLock();
			EnterCriticalSection(&sg_lock);
			sg_enableLog = enableLog;
			sg_resolvedCache.clear();
			sg_missingSet.clear();
			sg_extractedDataCache.clear();
			sg_memoryArchiveIndexCache.clear();
			sg_archives.clear();
			sg_gameDir = GetGameDir();
			sg_gameDirLower = ToLowerCopyW(sg_gameDir);
			LogCustomPakInfo(L"Configure begin enable=%u pakCount=%u gameDir=%s", enable ? 1u : 0u, (uint32_t)pakCount, sg_gameDir.c_str());

			if (!enable)
			{
				sg_enabled = false;
				LogCustomPakInfo(L"Disabled");
				LeaveCriticalSection(&sg_lock);
				return;
			}

			for (size_t i = 0; i < pakCount; ++i)
			{
				const wchar_t* raw = pakPaths ? pakPaths[i] : nullptr;
				if (!raw || raw[0] == L'\0')
				{
					LogCustomPakWarn(L"Skip empty pak path at index=%u", (uint32_t)i);
					continue;
				}
				std::wstring path = NormalizeSlashesW(raw);
				if (!IsAbsPath(path))
				{
					path = JoinPath(sg_gameDir, path);
				}
				if (!IsFileExists(path))
				{
					LogCustomPakWarn(L"Pak file not found: %s", path.c_str());
					continue;
				}
				PakArchive archive = {};
				archive.path = path;
				archive.pathLower = ToLowerCopyW(path);
				sg_archives.push_back(std::move(archive));
				LogCustomPakInfo(L"Archive registered: %s", path.c_str());
			}

			sg_enabled = !sg_archives.empty();
			LogCustomPakInfo(L"Configure done enabled=%u pakCount=%u", sg_enabled ? 1u : 0u, (uint32_t)sg_archives.size());
			LeaveCriticalSection(&sg_lock);
		}

		static bool ResolveCustomPakVFSDataLocked(const wchar_t* originalPath, std::wstring* outRelativePath, std::shared_ptr<const std::vector<uint8_t>>& outData)
		{
			if (!sg_enabled)
			{
				return false;
			}
			std::wstring relativePath;
			if (!BuildRelativePath(sg_gameDir, sg_gameDirLower, originalPath, relativePath))
			{
				return false;
			}
			auto itResolved = sg_resolvedCache.find(relativePath);
			if (itResolved != sg_resolvedCache.end())
			{
				auto itData = sg_extractedDataCache.find(itResolved->second);
				if (itData != sg_extractedDataCache.end() && itData->second)
				{
					if (outRelativePath)
					{
						*outRelativePath = relativePath;
					}
					outData = itData->second;
					LogCustomPakInfo(L"Resolve memory cache hit source=%s relative=%s size=%u", originalPath ? originalPath : L"", relativePath.c_str(), (uint32_t)outData->size());
					return true;
				}
				sg_resolvedCache.erase(itResolved);
			}
			if (sg_missingSet.find(relativePath) != sg_missingSet.end())
			{
				return false;
			}
			std::array<uint8_t, 16> hash = HashRelPath(relativePath);
			Hash16 key = { hash };
			std::wstring hashText = HashHex(hash);
			for (size_t idx = sg_archives.size(); idx > 0; --idx)
			{
				PakArchive& archive = sg_archives[idx - 1];
				if (!EnsureArchiveIndexLoaded(archive))
				{
					continue;
				}
				const PakEntry* entry = FindArchiveEntry(archive, relativePath, key);
				if (!entry)
				{
					continue;
				}
				std::wstring extractedKey = BuildArchiveResolvedKey(archive, relativePath, hash);
				auto itExtracted = sg_extractedDataCache.find(extractedKey);
				if (itExtracted != sg_extractedDataCache.end() && itExtracted->second)
				{
					if (outRelativePath)
					{
						*outRelativePath = relativePath;
					}
					outData = itExtracted->second;
					sg_resolvedCache[relativePath] = extractedKey;
					LogCustomPakInfo(L"Resolve extracted memory cache hit relative=%s hash=%s pak=%s size=%u", relativePath.c_str(), hashText.c_str(), archive.path.c_str(), (uint32_t)outData->size());
					return true;
				}

				std::vector<uint8_t> raw;
				if (!ReadEntryRaw(archive, *entry, raw))
				{
					LogCustomPakWarn(L"Read entry failed relative=%s hash=%s pak=%s", relativePath.c_str(), hashText.c_str(), archive.path.c_str());
					continue;
				}
				std::shared_ptr<const std::vector<uint8_t>> memoryData = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
				if (!memoryData || memoryData->empty())
				{
					LogCustomPakWarn(L"Build memory data failed relative=%s hash=%s", relativePath.c_str(), hashText.c_str());
					continue;
				}
				sg_extractedDataCache[extractedKey] = memoryData;
				sg_resolvedCache[relativePath] = extractedKey;
				if (outRelativePath)
				{
					*outRelativePath = relativePath;
				}
				outData = memoryData;
				LogCustomPakInfo(L"Resolve extracted success relative=%s hash=%s pak=%s mode=memory size=%u", relativePath.c_str(), hashText.c_str(), archive.path.c_str(), (uint32_t)memoryData->size());
				return true;
			}
			sg_missingSet.insert(relativePath);
			LogCustomPakInfo(L"Resolve miss relative=%s hash=%s source=%s", relativePath.c_str(), hashText.c_str(), originalPath ? originalPath : L"");
			return false;
		}

		bool ResolveCustomPakVFSPath(const wchar_t* originalPath, std::wstring& resolvedPath)
		{
			resolvedPath.clear();
			std::shared_ptr<const std::vector<uint8_t>> resolvedData;
			EnsureLock();
			EnterCriticalSection(&sg_lock);
			bool ok = ResolveCustomPakVFSDataLocked(originalPath, nullptr, resolvedData);
			LeaveCriticalSection(&sg_lock);
			if (ok)
			{
				resolvedPath = L"CustomPAK:memory";
			}
			return ok;
		}

		bool ResolveCustomPakVFSData(const wchar_t* originalPath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			resolvedData.reset();
			EnsureLock();
			EnterCriticalSection(&sg_lock);
			bool ok = ResolveCustomPakVFSDataLocked(originalPath, nullptr, resolvedData);
			LeaveCriticalSection(&sg_lock);
			return ok;
		}

		bool ResolveCustomPakArchiveDataEx(const wchar_t* archivePath, const std::vector<std::wstring>& nestedArchives, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			resolvedData.reset();
			if (!archivePath || archivePath[0] == L'\0' || !relativePath || relativePath[0] == L'\0')
			{
				LogCustomPakWarn(L"Resolve archive direct invalid args archive=%s relative=%s", archivePath ? archivePath : L"", relativePath ? relativePath : L"");
				return false;
			}

			EnsureLock();
			EnterCriticalSection(&sg_lock);
			if (!sg_enabled)
			{
				LogCustomPakInfo(L"Resolve archive direct skipped disabled archive=%s relative=%s", archivePath, relativePath);
				LeaveCriticalSection(&sg_lock);
				return false;
			}

			std::wstring outerArchive = NormalizeSlashesW(archivePath);
			if (!IsAbsPath(outerArchive))
			{
				outerArchive = JoinPath(sg_gameDir, outerArchive);
			}
			std::wstring normalizedArchive = BuildArchiveChainDisplayPath(outerArchive, nestedArchives);
			std::wstring archiveLower = ToLowerCopyW(outerArchive);
			std::wstring cacheKey = BuildArchiveChainCacheKey(outerArchive, nestedArchives);
			cacheKey += L"|file|";
			cacheKey += ToLowerCopyW(relativePath);

			auto itCached = sg_extractedDataCache.find(cacheKey);
			if (itCached != sg_extractedDataCache.end() && itCached->second)
			{
				resolvedData = itCached->second;
				LogCustomPakInfo(L"Resolve archive direct cache hit archive=%s relative=%s size=%u", normalizedArchive.c_str(), relativePath, (uint32_t)resolvedData->size());
				LeaveCriticalSection(&sg_lock);
				return true;
			}

			for (PakArchive& archive : sg_archives)
			{
				if (archive.pathLower != archiveLower)
				{
					continue;
				}
				if (!EnsureArchiveIndexLoaded(archive))
				{
					LogCustomPakWarn(L"Resolve archive direct index load failed archive=%s relative=%s", normalizedArchive.c_str(), relativePath);
					break;
				}

				std::vector<uint8_t> raw;
				std::wstring relative = relativePath;
				if (nestedArchives.empty())
				{
					Hash16 entryKey = { HashRelPath(relative) };
					const PakEntry* entry = FindArchiveEntry(archive, relative, entryKey);
					if (!entry)
					{
						LogCustomPakInfo(L"Resolve archive direct miss entry archive=%s relative=%s", normalizedArchive.c_str(), relative.c_str());
						break;
					}
					if (!ReadEntryRaw(archive, *entry, raw))
					{
						LogCustomPakWarn(L"Resolve archive direct read failed archive=%s relative=%s", normalizedArchive.c_str(), relative.c_str());
						break;
					}
				}
				else
				{
					std::wstring currentArchiveTag = outerArchive;
					for (size_t i = 0; i < nestedArchives.size(); ++i)
					{
						std::wstring nextArchiveTag = BuildArchiveChainDisplayPath(outerArchive, std::vector<std::wstring>(nestedArchives.begin(), nestedArchives.begin() + i + 1));
						std::wstring nestedCacheKey = ToLowerCopyW(nextArchiveTag);

						auto itNestedCached = sg_extractedDataCache.find(nestedCacheKey);
						if (itNestedCached != sg_extractedDataCache.end() && itNestedCached->second)
						{
							raw.assign(itNestedCached->second->begin(), itNestedCached->second->end());
							currentArchiveTag = nextArchiveTag;
							continue;
						}

						std::vector<uint8_t> nextRaw;
						if (i == 0)
						{
							Hash16 nestedKey = { HashRelPath(nestedArchives.front()) };
							const PakEntry* entry = FindArchiveEntry(archive, nestedArchives.front(), nestedKey);
							if (!entry)
							{
								LogCustomPakInfo(L"Resolve archive nested miss first archive=%s nested=%s", outerArchive.c_str(), nestedArchives.front().c_str());
								raw.clear();
								break;
							}
							if (!ReadEntryRaw(archive, *entry, nextRaw))
							{
								LogCustomPakWarn(L"Resolve archive nested read first failed archive=%s nested=%s", outerArchive.c_str(), nestedArchives.front().c_str());
								raw.clear();
								break;
							}
						}
						else if (!ResolveArchiveDataFromMemory(currentArchiveTag, raw.data(), raw.size(), nestedArchives[i], nextRaw))
						{
							LogCustomPakWarn(L"Resolve archive nested extract failed archive=%s nested=%s", currentArchiveTag.c_str(), nestedArchives[i].c_str());
							raw.clear();
							break;
						}

						raw = std::move(nextRaw);
						sg_extractedDataCache[nestedCacheKey] = std::make_shared<const std::vector<uint8_t>>(raw);
						currentArchiveTag = nextArchiveTag;
					}
					if (raw.empty())
					{
						break;
					}

					std::vector<uint8_t> finalRaw;
					if (!ResolveArchiveDataFromMemory(currentArchiveTag, raw.data(), raw.size(), relative, finalRaw))
					{
						LogCustomPakInfo(L"Resolve archive nested miss final archive=%s relative=%s", currentArchiveTag.c_str(), relative.c_str());
						break;
					}
					raw = std::move(finalRaw);
				}

				resolvedData = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
				sg_extractedDataCache[cacheKey] = resolvedData;
				LogCustomPakInfo(L"Resolve archive direct success archive=%s relative=%s size=%u", normalizedArchive.c_str(), relative.c_str(), (uint32_t)resolvedData->size());
				LeaveCriticalSection(&sg_lock);
				return resolvedData != nullptr;
			}

			LogCustomPakInfo(L"Resolve archive direct miss archive=%s relative=%s", normalizedArchive.c_str(), relativePath);
			LeaveCriticalSection(&sg_lock);
			return false;
		}

		bool ResolveCustomPakArchiveData(const wchar_t* archivePath, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			std::wstring outerArchive;
			std::vector<std::wstring> nestedArchives;
			if (!archivePath || archivePath[0] == L'\0' || !SplitArchiveChainPath(archivePath, outerArchive, nestedArchives))
			{
				resolvedData.reset();
				LogCustomPakWarn(L"Resolve archive direct invalid chain archive=%s relative=%s", archivePath ? archivePath : L"", relativePath ? relativePath : L"");
				return false;
			}
			return ResolveCustomPakArchiveDataEx(outerArchive.c_str(), nestedArchives, relativePath, resolvedData);
		}

		static bool ResolveCustomPakVFSFileSizeLocked(const wchar_t* originalPath, uint64_t& outSize)
		{
			outSize = 0;
			if (!sg_enabled)
			{
				return false;
			}
			std::wstring relativePath;
			if (!BuildRelativePath(sg_gameDir, sg_gameDirLower, originalPath, relativePath))
			{
				return false;
			}

			auto itResolved = sg_resolvedCache.find(relativePath);
			if (itResolved != sg_resolvedCache.end())
			{
				auto itData = sg_extractedDataCache.find(itResolved->second);
				if (itData != sg_extractedDataCache.end() && itData->second)
				{
					outSize = itData->second->size();
					return true;
				}
			}

			if (sg_missingSet.find(relativePath) != sg_missingSet.end())
			{
				return false;
			}

			std::array<uint8_t, 16> hash = HashRelPath(relativePath);
			Hash16 key = { hash };
			for (size_t idx = sg_archives.size(); idx > 0; --idx)
			{
				PakArchive& archive = sg_archives[idx - 1];
				if (!EnsureArchiveIndexLoaded(archive))
				{
					continue;
				}
				const PakEntry* entry = FindArchiveEntry(archive, relativePath, key);
				if (entry)
				{
					outSize = entry->origSize;
					return true;
				}
			}
			return false;
		}

		bool ResolveCustomPakVFSFileSize(const wchar_t* originalPath, uint64_t& outSize)
		{
			outSize = 0;
			EnsureLock();
			EnterCriticalSection(&sg_lock);
			bool ok = ResolveCustomPakVFSFileSizeLocked(originalPath, outSize);
			LeaveCriticalSection(&sg_lock);
			return ok;
		}

		bool IsCustomPakArchivePath(const wchar_t* path)
		{
			if (!path || path[0] == L'\0')
			{
				return false;
			}
			if (IsCustomPakInternalIoActive())
			{
				return false;
			}
			EnsureLock();
			EnterCriticalSection(&sg_lock);
			if (sg_archives.empty())
			{
				LeaveCriticalSection(&sg_lock);
				return false;
			}
			std::wstring normalized = NormalizeSlashesW(path);
			if (!IsAbsPath(normalized))
			{
				normalized = JoinPath(sg_gameDir, normalized);
			}
			normalized = ToLowerCopyW(normalized);
			for (const PakArchive& archive : sg_archives)
			{
				if (archive.pathLower == normalized)
				{
					LeaveCriticalSection(&sg_lock);
					return true;
				}
			}
			LeaveCriticalSection(&sg_lock);
			return false;
		}

		bool IsCustomPakInternalIoActive()
		{
			return sg_customPakInternalIoDepth > 0;
		}
	}
}
