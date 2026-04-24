#include "krkr_plugin_bridge.h"

#include <Windows.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../RuntimeCore/hook/Hook.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../../third/detours/include/detours.h"
#include "../../../third/krkrplugin/CompilerHelper.h"
#include "../../../third/krkrplugin/KrkrPatchStream.h"
#include "../../../third/krkrplugin/Pe.h"

using namespace Rut::HookX;

namespace CialloHook
{
	namespace HookModules
	{
		namespace
		{
			std::wstring sg_gameDir;
			std::wstring sg_gameDirLower;
			std::vector<std::wstring> sg_patchRoots;
			std::vector<std::wstring> sg_patchCustomPaks;
			std::vector<std::wstring> sg_patchBaseNames;
			std::vector<std::wstring> sg_patchFolders;
			std::vector<std::wstring> sg_patchArchives;
			bool sg_enableKrkrPatch = false;
			bool sg_enableKrkrPatchVerboseLog = false;
			bool sg_enableKrkrBootstrapBypass = false;
			bool sg_krkrBootstrapBypassTried = false;

			static std::wstring NormalizeSlashes(const std::wstring& path)
			{
				std::wstring normalized = path;
				for (wchar_t& ch : normalized)
				{
					if (ch == L'/')
					{
						ch = L'\\';
					}
				}
				return normalized;
			}

			static bool IsAbsolutePath(const std::wstring& path)
			{
				return (path.size() >= 2 && path[1] == L':')
					|| (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
			}

			static std::wstring ToLowerCopy(const std::wstring& path)
			{
				std::wstring lowered = path;
				for (wchar_t& ch : lowered)
				{
					ch = (wchar_t)towlower(ch);
				}
				return lowered;
			}

			static bool StartsWithInsensitive(const std::wstring& value, const std::wstring& prefix)
			{
				if (value.size() < prefix.size())
				{
					return false;
				}
				return _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
			}

			static std::wstring JoinGamePath(const std::wstring& path)
			{
				if (path.empty())
				{
					return sg_gameDir;
				}
				if (IsAbsolutePath(path) || sg_gameDir.empty())
				{
					return NormalizeSlashes(path);
				}
				std::wstring normalized = NormalizeSlashes(path);
				if (!sg_gameDir.empty() && (sg_gameDir.back() == L'\\' || sg_gameDir.back() == L'/'))
				{
					return sg_gameDir + normalized;
				}
				return sg_gameDir + L"\\" + normalized;
			}

			static bool EndsWithInsensitive(const std::wstring& value, const wchar_t* suffix)
			{
				size_t suffixLen = wcslen(suffix);
				if (value.size() < suffixLen)
				{
					return false;
				}
				return _wcsicmp(value.c_str() + value.size() - suffixLen, suffix) == 0;
			}

			static bool IsExistingRegularFile(const std::wstring& path)
			{
				if (path.empty())
				{
					return false;
				}
				DWORD attrs = GetFileAttributesW(path.c_str());
				return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
			}

			static bool TryBuildRelativePath(const std::wstring& source, std::wstring& relativePath)
			{
				relativePath.clear();
				if (source.empty())
				{
					return false;
				}
				if (!IsAbsolutePath(source))
				{
					relativePath = source;
				}
				else
				{
					std::wstring sourceLower = ToLowerCopy(source);
					if (sg_gameDirLower.empty() || !StartsWithInsensitive(sourceLower, sg_gameDirLower))
					{
						return false;
					}
					relativePath = source.substr(sg_gameDir.size());
				}
				while (!relativePath.empty() && (relativePath[0] == L'.' || relativePath[0] == L'\\' || relativePath[0] == L'/'))
				{
					relativePath.erase(relativePath.begin());
				}
				while (!relativePath.empty() && (relativePath.back() == L'\\' || relativePath.back() == L'/'))
				{
					relativePath.pop_back();
				}
				return !relativePath.empty();
			}

			static std::wstring PatchName(const ttstr& name)
			{
				std::wstring rawName = name.c_str();
				std::wstring rawNameLower = ToLowerCopy(rawName);
				std::wstring patchName = rawName;
				if (rawNameLower.rfind(L"archive://./", 0) == 0 || rawNameLower.rfind(L"arc://./", 0) == 0)
				{
					size_t pos = patchName.find_last_of(L"/\\");
					if (pos != std::wstring::npos)
					{
						patchName.erase(0, pos + 1);
					}
				}
				else if (rawNameLower.rfind(L"file://./", 0) == 0)
				{
					size_t entryPos = rawNameLower.find(L".xp3>");
					if (entryPos != std::wstring::npos)
					{
						patchName = patchName.substr(entryPos + 5);
					}
					else
					{
						patchName.erase(0, wcslen(L"file://./"));
					}
				}
				else
				{
					std::wstring relativePath;
					if (TryBuildRelativePath(patchName, relativePath))
					{
						patchName = relativePath;
					}
				}

				std::replace(patchName.begin(), patchName.end(), L'/', L'\\');
				while (!patchName.empty() && (patchName[0] == L'.' || patchName[0] == L'\\'))
				{
					patchName.erase(patchName.begin());
				}
				return patchName;
			}

			static std::vector<std::wstring> BuildPatchCandidates(const std::wstring& patchName)
			{
				std::vector<std::wstring> candidates;
				if (patchName.empty())
				{
					return candidates;
				}

				candidates.push_back(patchName);

				size_t sepPos = patchName.find_last_of(L"\\/");
				if (sepPos != std::wstring::npos && sepPos + 1 < patchName.size())
				{
					std::wstring basename = patchName.substr(sepPos + 1);
					bool exists = false;
					for (const std::wstring& candidate : candidates)
					{
						if (_wcsicmp(candidate.c_str(), basename.c_str()) == 0)
						{
							exists = true;
							break;
						}
					}
					if (!exists)
					{
						candidates.push_back(std::move(basename));
					}
				}

				return candidates;
			}

			static std::wstring JoinRelativePath(const std::wstring& base, const std::wstring& leaf)
			{
				if (base.empty())
				{
					return leaf;
				}
				if (leaf.empty())
				{
					return base;
				}
				if (base.back() == L'\\' || base.back() == L'/')
				{
					return base + leaf;
				}
				return base + L"\\" + leaf;
			}

			static std::wstring BuildArchiveSourceLabel(const std::wstring& archivePath, const std::vector<std::wstring>& nestedArchives, const std::wstring& relativePath)
			{
				std::wstring label = archivePath;
				for (const std::wstring& nestedArchive : nestedArchives)
				{
					label += L">";
					label += nestedArchive;
				}
				if (!relativePath.empty())
				{
					label += L">";
					label += relativePath;
				}
				return label;
			}

			static std::wstring BuildArchiveStoragePath(const std::wstring& archivePath, const std::vector<std::wstring>& nestedArchives, const std::wstring& relativePath)
			{
				std::wstring storagePath = archivePath;
				for (const std::wstring& nestedArchive : nestedArchives)
				{
					storagePath += L">";
					storagePath += nestedArchive;
				}
				if (!relativePath.empty())
				{
					storagePath += L">";
					storagePath += relativePath;
				}
				return storagePath;
			}

			static std::wstring FormatStoragePathForLog(const std::wstring& storagePath)
			{
				return storagePath;
			}

			static bool ReadWholeFile(const std::wstring& filePath, std::vector<BYTE>& outData)
			{
				outData.clear();

				HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
				{
					return false;
				}

				LARGE_INTEGER fileSize = {};
				if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0)
				{
					CloseHandle(hFile);
					return false;
				}

				if (fileSize.QuadPart > static_cast<LONGLONG>(SIZE_MAX))
				{
					CloseHandle(hFile);
					return false;
				}

				outData.resize(static_cast<size_t>(fileSize.QuadPart));
				DWORD totalRead = 0;
				while (totalRead < outData.size())
				{
					DWORD chunkRead = 0;
					DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(outData.size() - totalRead, 1u << 20));
					if (!ReadFile(hFile, outData.data() + totalRead, chunkSize, &chunkRead, nullptr) || chunkRead == 0)
					{
						CloseHandle(hFile);
						outData.clear();
						return totalRead == outData.size();
					}
					totalRead += chunkRead;
				}

				CloseHandle(hFile);
				return true;
			}

			class TempTtstr
			{
			public:
				explicit TempTtstr(const std::wstring& value)
					: m_storage(value)
				{
					ZeroMemory(&m_variant, sizeof(m_variant));
					m_variant.Length = static_cast<tjs_int>(m_storage.size());
					if (m_variant.Length > TJS_VS_SHORT_LEN)
					{
						m_variant.LongString = const_cast<tjs_char*>(m_storage.c_str());
					}
					else
					{
						memcpy(m_variant.ShortString, m_storage.c_str(), (m_storage.size() + 1) * sizeof(wchar_t));
					}
					m_text.Ptr = &m_variant;
				}

				const ttstr& Get() const
				{
					return m_text;
				}

			private:
				std::wstring m_storage;
				tTJSVariantString m_variant = {};
				ttstr m_text = {};
			};

			static bool TryResolvePatchFile(const std::wstring& folderSpec, const std::wstring& patchCandidate, std::wstring& patchUrl)
			{
				std::wstring folderPath = JoinGamePath(folderSpec);
				std::wstring candidate = JoinRelativePath(folderPath, patchCandidate);
				if (IsExistingRegularFile(candidate))
				{
					patchUrl = candidate;
					return true;
				}
				return false;
			}

			static bool TryResolvePatchArchive(const std::wstring& archiveSpec, const std::vector<std::wstring>& nestedArchives, const std::wstring& relativePrefix, const std::wstring& patchCandidate, std::wstring& patchUrl, std::wstring& patchArc)
			{
				std::wstring archivePath = JoinGamePath(archiveSpec);
				std::wstring relativePath = relativePrefix.empty() ? patchCandidate : JoinRelativePath(relativePrefix, patchCandidate);
				std::shared_ptr<const std::vector<uint8_t>> archiveData;
				if (!Rut::HookX::ResolveCustomPakArchiveDataEx(archivePath.c_str(), nestedArchives, relativePath.c_str(), archiveData) || !archiveData)
				{
					return false;
				}
				patchArc = archivePath;
				patchUrl = BuildArchiveStoragePath(archivePath, nestedArchives, relativePath);
				return true;
			}

			static bool TryReadPatchFileToMemory(const std::wstring& folderSpec, const std::wstring& patchCandidate, tTJSBinaryStream*& outStream, std::wstring& outSource)
			{
				std::wstring folderPath = JoinGamePath(folderSpec);
				std::wstring candidate = JoinRelativePath(folderPath, patchCandidate);
				if (!IsExistingRegularFile(candidate))
				{
					return false;
				}

				std::vector<BYTE> buffer;
				if (!ReadWholeFile(candidate, buffer))
				{
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: archive item read failed %s", candidate.c_str());
					return false;
				}

				outSource = candidate;
				outStream = tTJSBinaryStream::ApplyWrapVTable(new KrkrPatchMemoryStream(std::move(buffer)));
				return true;
			}

			static bool TryReadPatchArchiveToMemory(const std::wstring& archiveSpec, const std::vector<std::wstring>& nestedArchives, const std::wstring& relativePrefix, const std::wstring& patchCandidate, tTJSBinaryStream*& outStream, std::wstring& outSource)
			{
				std::wstring archivePath = JoinGamePath(archiveSpec);
				std::wstring relativePath = relativePrefix.empty() ? patchCandidate : JoinRelativePath(relativePrefix, patchCandidate);
				std::shared_ptr<const std::vector<uint8_t>> archiveData;
				if (!Rut::HookX::ResolveCustomPakArchiveDataEx(archivePath.c_str(), nestedArchives, relativePath.c_str(), archiveData) || !archiveData)
				{
					return false;
				}

				outSource = BuildArchiveSourceLabel(archivePath, nestedArchives, relativePath);
				outStream = tTJSBinaryStream::ApplyWrapVTable(new KrkrPatchMemoryStream(std::vector<BYTE>(archiveData->begin(), archiveData->end())));
				return true;
			}

			static bool TryBuildPatchUrlForBaseName(const std::wstring& baseName, const std::wstring& patchCandidate, std::wstring& patchUrl, std::wstring& patchArc)
			{
				for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
				{
					if (TryResolvePatchFile(JoinRelativePath(*rootIt, baseName), patchCandidate, patchUrl))
					{
						return true;
					}
				}

				for (auto customPakIt = sg_patchCustomPaks.rbegin(); customPakIt != sg_patchCustomPaks.rend(); ++customPakIt)
				{
					for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
					{
						if (TryResolvePatchArchive(*customPakIt, {}, JoinRelativePath(*rootIt, baseName), patchCandidate, patchUrl, patchArc))
						{
							return true;
						}
					}
					if (TryResolvePatchArchive(*customPakIt, {}, baseName, patchCandidate, patchUrl, patchArc))
					{
						return true;
					}
				}

				if (TryResolvePatchFile(baseName, patchCandidate, patchUrl))
				{
					return true;
				}

				for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
				{
					if (TryResolvePatchArchive(JoinRelativePath(*rootIt, baseName) + L".xp3", {}, L"", patchCandidate, patchUrl, patchArc))
					{
						return true;
					}
				}

				for (auto customPakIt = sg_patchCustomPaks.rbegin(); customPakIt != sg_patchCustomPaks.rend(); ++customPakIt)
				{
					for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
					{
						std::vector<std::wstring> nestedArchives = { JoinRelativePath(*rootIt, baseName) + L".xp3" };
						if (TryResolvePatchArchive(*customPakIt, nestedArchives, L"", patchCandidate, patchUrl, patchArc))
						{
							return true;
						}
					}

					std::vector<std::wstring> nestedArchives = { baseName + L".xp3" };
					if (TryResolvePatchArchive(*customPakIt, nestedArchives, L"", patchCandidate, patchUrl, patchArc))
					{
						return true;
					}
				}

				if (TryResolvePatchArchive(baseName + L".xp3", {}, L"", patchCandidate, patchUrl, patchArc))
				{
					return true;
				}

				return false;
			}

			static bool TryBuildPatchMemoryStreamForBaseName(const std::wstring& baseName, const std::wstring& patchCandidate, tTJSBinaryStream*& outStream, std::wstring& outSource)
			{
				for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
				{
					if (TryReadPatchFileToMemory(JoinRelativePath(*rootIt, baseName), patchCandidate, outStream, outSource))
					{
						return true;
					}
				}

				for (auto customPakIt = sg_patchCustomPaks.rbegin(); customPakIt != sg_patchCustomPaks.rend(); ++customPakIt)
				{
					for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
					{
						if (TryReadPatchArchiveToMemory(*customPakIt, {}, JoinRelativePath(*rootIt, baseName), patchCandidate, outStream, outSource))
						{
							return true;
						}
					}
					if (TryReadPatchArchiveToMemory(*customPakIt, {}, baseName, patchCandidate, outStream, outSource))
					{
						return true;
					}
				}

				if (TryReadPatchFileToMemory(baseName, patchCandidate, outStream, outSource))
				{
					return true;
				}

				for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
				{
					if (TryReadPatchArchiveToMemory(JoinRelativePath(*rootIt, baseName) + L".xp3", {}, L"", patchCandidate, outStream, outSource))
					{
						return true;
					}
				}

				for (auto customPakIt = sg_patchCustomPaks.rbegin(); customPakIt != sg_patchCustomPaks.rend(); ++customPakIt)
				{
					for (auto rootIt = sg_patchRoots.rbegin(); rootIt != sg_patchRoots.rend(); ++rootIt)
					{
						std::vector<std::wstring> nestedArchives = { JoinRelativePath(*rootIt, baseName) + L".xp3" };
						if (TryReadPatchArchiveToMemory(*customPakIt, nestedArchives, L"", patchCandidate, outStream, outSource))
						{
							return true;
						}
					}

					std::vector<std::wstring> nestedArchives = { baseName + L".xp3" };
					if (TryReadPatchArchiveToMemory(*customPakIt, nestedArchives, L"", patchCandidate, outStream, outSource))
					{
						return true;
					}
				}

				if (TryReadPatchArchiveToMemory(baseName + L".xp3", {}, L"", patchCandidate, outStream, outSource))
				{
					return true;
				}

				return false;
			}

			static bool TryBuildPatchUrlOrdered(const std::vector<std::wstring>& patchCandidates, std::wstring& patchUrl, std::wstring& patchArc)
			{
				patchUrl.clear();
				patchArc.clear();

				for (const std::wstring& patchCandidate : patchCandidates)
				{
					for (const std::wstring& baseName : sg_patchBaseNames)
					{
						if (TryBuildPatchUrlForBaseName(baseName, patchCandidate, patchUrl, patchArc))
						{
							return true;
						}
					}

					for (const std::wstring& folder : sg_patchFolders)
					{
						if (TryResolvePatchFile(folder, patchCandidate, patchUrl))
						{
							return true;
						}
					}

					for (const std::wstring& archive : sg_patchArchives)
					{
						if (TryResolvePatchArchive(archive, {}, L"", patchCandidate, patchUrl, patchArc))
						{
							return true;
						}
					}
				}

				return false;
			}

			static bool TryBuildPatchMemoryStreamOrdered(const std::vector<std::wstring>& patchCandidates, tTJSBinaryStream*& outStream, std::wstring& outSource)
			{
				outStream = nullptr;
				outSource.clear();

				for (const std::wstring& patchCandidate : patchCandidates)
				{
					for (const std::wstring& baseName : sg_patchBaseNames)
					{
						if (TryBuildPatchMemoryStreamForBaseName(baseName, patchCandidate, outStream, outSource))
						{
							return true;
						}
					}

					for (const std::wstring& folder : sg_patchFolders)
					{
						if (TryReadPatchFileToMemory(folder, patchCandidate, outStream, outSource))
						{
							return true;
						}
					}

					for (const std::wstring& archive : sg_patchArchives)
					{
						if (TryReadPatchArchiveToMemory(archive, {}, L"", patchCandidate, outStream, outSource))
						{
							return true;
						}
					}
				}

				return false;
			}

			static bool IsCustomPakArchiveSource(const std::wstring& archiveSpec)
			{
				if (archiveSpec.empty())
				{
					return false;
				}

				std::wstring normalized = ToLowerCopy(NormalizeSlashes(archiveSpec));
				for (const std::wstring& customPak : sg_patchCustomPaks)
				{
					std::wstring customPakPath = ToLowerCopy(JoinGamePath(customPak));
					if (normalized == customPakPath)
					{
						return true;
					}
					if (normalized.size() > customPakPath.size()
						&& _wcsnicmp(normalized.c_str(), customPakPath.c_str(), customPakPath.size()) == 0
						&& normalized[customPakPath.size()] == L'>')
					{
						return true;
					}
				}
				return false;
			}

			static bool TryBuildPatchUrl(const ttstr& name, tjs_uint32 flags, std::wstring& patchUrl, std::wstring& patchArc)
			{
				patchUrl.clear();
				patchArc.clear();

				if (!sg_enableKrkrPatch)
				{
					return false;
				}
				if (flags != TJS_BS_READ)
				{
					if (sg_enableKrkrPatchVerboseLog)
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: skip non-read stream flags=%u name=%s", (uint32_t)flags, name.c_str());
					}
					return false;
				}

				std::wstring rawName = name.c_str();
				if (sg_enableKrkrPatchVerboseLog)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: PatchUrl request=%s", rawName.c_str());
				}

				if (EndsWithInsensitive(rawName, L".sig"))
				{
					return false;
				}

				std::wstring patchName = PatchName(name);
				std::vector<std::wstring> patchCandidates = BuildPatchCandidates(patchName);
				if (patchCandidates.empty())
				{
					if (sg_enableKrkrPatchVerboseLog)
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: PatchUrl ignore empty patch name request=%s", rawName.c_str());
					}
					return false;
				}
				if (sg_enableKrkrPatchVerboseLog)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: PatchUrl normalized=%s", patchName.c_str());
				}

				if (TryBuildPatchUrlOrdered(patchCandidates, patchUrl, patchArc))
				{
					std::wstring displayPath = FormatStoragePathForLog(patchUrl);
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: PatchUrl redirect %s -> %s", rawName.c_str(), displayPath.c_str());
					return true;
				}

				if (sg_enableKrkrPatchVerboseLog)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: PatchUrl miss %s", rawName.c_str());
				}
				return false;
			}

#if defined(_M_IX86)
#define KRKR_MSVC_HOOK_CALL __fastcall
#else
#define KRKR_MSVC_HOOK_CALL
#endif

#if defined(_M_IX86)
			using pPatchSignVerifyMsvc = BOOL(__fastcall*)(HMODULE hModule);
			using pCreateStreamBorland = tTJSBinaryStream* (*)(const ttstr& name, tjs_uint32 flags);
			using pCreateStreamMsvc = tTJSBinaryStream* (KRKR_MSVC_HOOK_CALL*)(const ttstr& name, tjs_uint32 flags);

			pPatchSignVerifyMsvc sg_rawSignVerifyMsvc = nullptr;
			pCreateStreamBorland sg_rawCreateStreamBorland = nullptr;
			bool sg_compilerAnalyzed = false;
			bool sg_signVerifyHooked = false;
			bool sg_createStreamHooked = false;

			static BOOL __fastcall PatchSignVerifyMsvc(HMODULE /*hModule*/)
			{
				return TRUE;
			}
#else
			using pPatchSignVerifyResultMsvc = bool(*)();
			using pCreateStreamMsvc = tTJSBinaryStream* (KRKR_MSVC_HOOK_CALL*)(const ttstr& name, tjs_uint32 flags);
			using pCreateStreamByIndexMsvc = tTJSBinaryStream* (*)(tTVPXP3Archive<CompilerType::Msvc>* pArchive, tjs_uint idx);

			bool sg_compilerAnalyzed = false;
			bool sg_signVerifyHooked = false;
			bool sg_createStreamHooked = false;
			bool sg_createStreamByIndexHooked = false;
			void** sg_signVerifyVTable = nullptr;
			void** sg_xp3ArchiveVTable = nullptr;
			pPatchSignVerifyResultMsvc sg_rawSignVerifyResultMsvc = nullptr;
			pCreateStreamByIndexMsvc sg_rawCreateStreamByIndexMsvc = nullptr;

			static bool PatchSignVerifyResultMsvc()
			{
				return true;
			}
#endif

			pCreateStreamMsvc sg_rawCreateStreamMsvc = nullptr;

			static bool TryBuildPatchMemoryStream(const std::wstring& patchName, tTJSBinaryStream*& outStream, std::wstring& outSource);

			template <typename TArcStream, auto** TOriginalCreateStream>
			static tTJSBinaryStream* PatchCreateStreamImpl(const ttstr& name, tjs_uint32 flags)
			{
				std::wstring rawName = name.c_str();
				std::wstring patchName = PatchName(name);
				if (EndsWithInsensitive(rawName, L".sig"))
				{
					if (sg_enableKrkrPatchVerboseLog)
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: intercept sig stream %s", rawName.c_str());
					}
					return tTJSBinaryStream::ApplyWrapVTable(new KrkrPatchSigStream());
				}

				std::wstring patchUrl;
				std::wstring patchArc;
				if (!TryBuildPatchUrl(name, flags, patchUrl, patchArc))
				{
					tTJSBinaryStream* patchStream = nullptr;
					std::wstring patchSource;
					if (TryBuildPatchMemoryStream(patchName, patchStream, patchSource))
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: memory redirect %s -> %s", rawName.c_str(), patchSource.c_str());
						return patchStream;
					}
					return CompilerHelper::CallStaticFunc<tTJSBinaryStream*, TOriginalCreateStream, const ttstr&, tjs_uint32>(name, flags);
				}

				if (!patchArc.empty() && IsCustomPakArchiveSource(patchArc))
				{
					tTJSBinaryStream* patchStream = nullptr;
					std::wstring patchSource;
					if (TryBuildPatchMemoryStream(patchName, patchStream, patchSource))
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: custom pak memory redirect %s -> %s", rawName.c_str(), patchSource.c_str());
						return patchStream;
					}
					std::wstring displayPath = FormatStoragePathForLog(patchUrl);
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: custom pak redirect lost before stream build %s -> %s", rawName.c_str(), displayPath.c_str());
					return CompilerHelper::CallStaticFunc<tTJSBinaryStream*, TOriginalCreateStream, const ttstr&, tjs_uint32>(name, flags);
				}

				TempTtstr patchUrlName(patchUrl);
				tTJSBinaryStream* patchUrlStream = CompilerHelper::CallStaticFunc<tTJSBinaryStream*, TOriginalCreateStream, const ttstr&, tjs_uint32>(patchUrlName.Get(), flags);
				if (!patchUrlStream)
				{
					std::wstring displayPath = FormatStoragePathForLog(patchUrl);
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: original CreateStream returned null for redirect=%s", displayPath.c_str());
					tTJSBinaryStream* patchStream = nullptr;
					std::wstring patchSource;
					if (TryBuildPatchMemoryStream(patchName, patchStream, patchSource))
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: memory fallback %s -> %s", rawName.c_str(), patchSource.c_str());
						return patchStream;
					}
					return CompilerHelper::CallStaticFunc<tTJSBinaryStream*, TOriginalCreateStream, const ttstr&, tjs_uint32>(name, flags);
				}

				if (!patchArc.empty())
				{
					TArcStream* arcStream = reinterpret_cast<TArcStream*>(patchUrlStream);
					if (!arcStream->CurSegment)
					{
						std::wstring displayPath = FormatStoragePathForLog(patchUrl);
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: arc stream missing CurSegment redirect=%s", displayPath.c_str());
						return patchUrlStream;
					}

					try
					{
						std::wstring displayPath = FormatStoragePathForLog(patchUrl);
						if (sg_enableKrkrPatchVerboseLog)
						{
							LogMessage(LogLevel::Info, L"KrkrPluginBridge: wrap archive redirect=%s start=%llu size=%llu", displayPath.c_str(), (unsigned long long)arcStream->CurSegment->Start, (unsigned long long)arcStream->CurSegment->OrgSize);
						}
						return tTJSBinaryStream::ApplyWrapVTable(new KrkrPatchArcStream(patchArc, arcStream->CurSegment));
					}
					catch (const std::exception&)
					{
						std::wstring displayPath = FormatStoragePathForLog(patchUrl);
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: KrkrPatchArcStream build failed redirect=%s, fallback original stream", displayPath.c_str());
						return patchUrlStream;
					}
				}

				if (sg_enableKrkrPatchVerboseLog)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: use redirected stream=%s", patchUrl.c_str());
				}
				return patchUrlStream;
			}

#if defined(_M_IX86)
			static tTJSBinaryStream* PatchCreateStreamBorland(const ttstr& name, tjs_uint32 flags)
			{
				return PatchCreateStreamImpl<tTVPXP3ArchiveStreamBorland, &sg_rawCreateStreamBorland>(name, flags);
			}
#endif

			static tTJSBinaryStream* KRKR_MSVC_HOOK_CALL PatchCreateStreamMsvc(const ttstr& name, tjs_uint32 flags)
			{
				return PatchCreateStreamImpl<tTVPXP3ArchiveStreamMsvc, &sg_rawCreateStreamMsvc>(name, flags);
			}

#if !defined(_M_IX86)
			static tTJSBinaryStream* PatchCreateStreamByIndexMsvc(tTVPXP3Archive<CompilerType::Msvc>* pArchive, tjs_uint idx)
			{
				if (!pArchive || !sg_enableKrkrPatch || !sg_rawCreateStreamByIndexMsvc)
				{
					return sg_rawCreateStreamByIndexMsvc ? sg_rawCreateStreamByIndexMsvc(pArchive, idx) : nullptr;
				}

				const std::wstring archiveName = pArchive->Name.c_str();
				if (!StartsWithInsensitive(archiveName, L"file://"))
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}
				if (pArchive->Count <= 0 || idx >= static_cast<tjs_uint>(pArchive->Count))
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}

				auto* itemBegin = pArchive->ItemVector.begin();
				auto* itemEnd = pArchive->ItemVector.end();
				if (!itemBegin || !itemEnd || itemEnd <= itemBegin)
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}

				size_t itemBytes = reinterpret_cast<const BYTE*>(itemEnd) - reinterpret_cast<const BYTE*>(itemBegin);
				if (itemBytes == 0 || (itemBytes % static_cast<size_t>(pArchive->Count)) != 0)
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}

				size_t itemSize = itemBytes / static_cast<size_t>(pArchive->Count);
				auto* pItem = reinterpret_cast<tTVPXP3Archive<CompilerType::Msvc>::tArchiveItem*>(reinterpret_cast<BYTE*>(itemBegin) + static_cast<size_t>(idx) * itemSize);
				if (!pItem || pItem->FileHash != 0)
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}

				std::wstring patchName = PatchName(pItem->Name);
				tTJSBinaryStream* patchStream = nullptr;
				std::wstring patchSource;
				if (!TryBuildPatchMemoryStream(patchName, patchStream, patchSource))
				{
					return sg_rawCreateStreamByIndexMsvc(pArchive, idx);
				}

				LogMessage(LogLevel::Info, L"KrkrPluginBridge: x64 CreateStreamByIndex redirect archive=%s item=%s source=%s", archiveName.c_str(), pItem->Name.c_str(), patchSource.c_str());
				return patchStream;
			}
#endif

			static std::string GetMainModuleNameA()
			{
				char modulePath[MAX_PATH] = {};
				DWORD len = GetModuleFileNameA(GetModuleHandleW(nullptr), modulePath, ARRAYSIZE(modulePath));
				if (len == 0 || len >= ARRAYSIZE(modulePath))
				{
					return {};
				}

				const char* baseName = modulePath;
				if (const char* slash = strrchr(modulePath, '\\'))
				{
					baseName = slash + 1;
				}
				return std::string(baseName);
			}

			static bool TryBuildPatchMemoryStream(const std::wstring& patchName, tTJSBinaryStream*& outStream, std::wstring& outSource)
			{
				std::vector<std::wstring> patchCandidates = BuildPatchCandidates(patchName);
				if (patchCandidates.empty())
				{
					return false;
				}
				return TryBuildPatchMemoryStreamOrdered(patchCandidates, outStream, outSource);
			}

			static bool WritePointer(void** target, void* value)
			{
				if (!target)
				{
					return false;
				}

				DWORD oldProtect = 0;
				if (!VirtualProtect(target, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
				{
					return false;
				}

				*target = value;

				DWORD restoreProtect = 0;
				VirtualProtect(target, sizeof(void*), oldProtect, &restoreProtect);
				FlushInstructionCache(GetCurrentProcess(), target, sizeof(void*));
				return true;
			}

			static bool WriteBytes(void* target, const void* bytes, size_t size)
			{
				if (!target || !bytes || size == 0)
				{
					return false;
				}

				DWORD oldProtect = 0;
				if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
				{
					return false;
				}

				memcpy(target, bytes, size);
				DWORD restoreProtect = 0;
				VirtualProtect(target, size, oldProtect, &restoreProtect);
				FlushInstructionCache(GetCurrentProcess(), target, size);
				return true;
			}

			static bool ModuleContainsBootStrapMarker(HMODULE module)
			{
				if (!module)
				{
					return false;
				}

				PIMAGE_OPTIONAL_HEADER opt = Pe::GetOptionalHeader(module);
				if (!opt || opt->SizeOfImage == 0)
				{
					return false;
				}

				const BYTE* imageBase = reinterpret_cast<const BYTE*>(module);
				const size_t imageSize = static_cast<size_t>(opt->SizeOfImage);
				static const wchar_t* kMarker = L"bootStrap";
				const BYTE* markerBytes = reinterpret_cast<const BYTE*>(kMarker);
				const size_t markerSize = (wcslen(kMarker) + 1) * sizeof(wchar_t);
				if (imageSize < markerSize)
				{
					return false;
				}

				for (size_t i = 0; i <= imageSize - markerSize; ++i)
				{
					if (memcmp(imageBase + i, markerBytes, markerSize) == 0)
					{
						return true;
					}
				}
				return false;
			}

			static uint32_t PatchModulePatternHead(HMODULE module, const BYTE* pattern, size_t patternSize, const BYTE* replacement, size_t replacementSize)
			{
				if (!module || !pattern || !replacement || patternSize == 0 || replacementSize == 0 || replacementSize > patternSize)
				{
					return 0;
				}

				PIMAGE_OPTIONAL_HEADER opt = Pe::GetOptionalHeader(module);
				if (!opt || opt->SizeOfImage < patternSize)
				{
					return 0;
				}

				BYTE* imageBase = reinterpret_cast<BYTE*>(module);
				const size_t imageSize = static_cast<size_t>(opt->SizeOfImage);
				uint32_t patchedCount = 0;

				for (size_t i = 0; i <= imageSize - patternSize; ++i)
				{
					BYTE* hit = imageBase + i;
					if (memcmp(hit, pattern, patternSize) != 0)
					{
						continue;
					}
					if (WriteBytes(hit, replacement, replacementSize))
					{
						++patchedCount;
					}
				}
				return patchedCount;
			}

			static void TryApplyKrkrBootstrapBypass()
			{
				if (!sg_enableKrkrBootstrapBypass || sg_krkrBootstrapBypassTried)
				{
					return;
				}
				sg_krkrBootstrapBypassTried = true;

#if !defined(_M_IX86)
				LogMessage(LogLevel::Warn, L"KrkrPluginBridge: KrkrBootstrapBypass is x86-only, current arch ignored");
				return;
#else
				HMODULE mainModule = GetModuleHandleW(nullptr);
				if (!mainModule)
				{
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: KrkrBootstrapBypass failed to get main module");
					return;
				}
				if (!ModuleContainsBootStrapMarker(mainModule))
				{
					if (sg_enableKrkrPatchVerboseLog)
					{
						LogMessage(LogLevel::Info, L"KrkrPluginBridge: KrkrBootstrapBypass marker bootStrap not found");
					}
					return;
				}

				static const BYTE kPatternShortRet[] = { 0x74, 0x04, 0xB0, 0x01, 0x5D, 0xC3, 0x32, 0xC0, 0x5D, 0xC3 };
				static const BYTE kReplaceShortRet[] = { 0x74, 0x00 };
				static const BYTE kPatternVerifyFlag[] = { 0x84, 0xDB, 0x74, 0x19, 0xB8, 0x17, 0xFC, 0xFF, 0xFF, 0x8B, 0x4D, 0xF4 };
				static const BYTE kReplaceVerifyFlag[] = { 0xB3, 0x01 };

				const uint32_t patchA = PatchModulePatternHead(mainModule, kPatternShortRet, sizeof(kPatternShortRet), kReplaceShortRet, sizeof(kReplaceShortRet));
				const uint32_t patchB = PatchModulePatternHead(mainModule, kPatternVerifyFlag, sizeof(kPatternVerifyFlag), kReplaceVerifyFlag, sizeof(kReplaceVerifyFlag));
				if (patchA != 0 || patchB != 0)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: KrkrBootstrapBypass patched shortRet=%u verifyFlag=%u", patchA, patchB);
				}
				else
				{
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: KrkrBootstrapBypass patterns not found");
				}
#endif
			}

			static pCreateStreamMsvc ResolveCreateStreamMsvc()
			{
#if defined(_M_IX86)
				static constexpr auto kPatternCreateStreamMsvc = "\x55\x8B\xEC\x6A\xFF\x68\x2A\x2A\x2A\x2A\x64\xA1\x2A\x2A\x2A\x2A\x50\x83\xEC\x5C\x53\x56\x57\xA1\x2A\x2A\x2A\x2A\x33\xC5\x50\x8D\x45\xF4\x64\xA3\x2A\x2A\x2A\x2A\x89\x65\xF0\x89\x4D\xEC\xC7\x45\xFC\x2A\x2A\x2A\x2A\xE8\x2A\x2A\x2A\x2A\x8B\x4D\xF4\x64\x89\x0D\x2A\x2A\x2A\x2A\x59\x5F\x5E\x5B\x8B\xE5\x5D\xC3";
				return reinterpret_cast<pCreateStreamMsvc>(Pe::FindData(kPatternCreateStreamMsvc, strlen(kPatternCreateStreamMsvc)));
#else
				static constexpr const char* kSymbolCandidates[] =
				{
					"?TVPCreateStream@@YAPEAVtTJSBinaryStream@@AEBVttstr@@I@Z",
					"?TVPCreateStream@@YAPEAVtTJSBinaryStream@@AEBVttstr@@K@Z",
					"TVPCreateStream"
				};

				std::string moduleName = GetMainModuleNameA();
				if (moduleName.empty())
				{
					LogMessage(LogLevel::Warn, L"KrkrPluginBridge: x64 failed to get main module name for CreateStream lookup");
					return nullptr;
				}

				for (const char* symbol : kSymbolCandidates)
				{
					if (auto* resolved = reinterpret_cast<pCreateStreamMsvc>(DetourFindFunction(moduleName.c_str(), symbol)))
					{
						LogMessageA(LogLevel::Info, "KrkrPluginBridge: x64 CreateStream resolved by symbol %s", symbol);
						return resolved;
					}
				}

				LogMessage(LogLevel::Warn, L"KrkrPluginBridge: x64 CreateStream symbol lookup failed in %S", moduleName.c_str());
				return nullptr;
#endif
			}

			static void EnsureHooksInstalled()
			{
				TryApplyKrkrBootstrapBypass();
#if defined(_M_IX86)
				if (sg_createStreamHooked)
#else
				if (sg_createStreamHooked && sg_createStreamByIndexHooked)
#endif
				{
					return;
				}
				if (!sg_compilerAnalyzed)
				{
					CompilerHelper::Analyze();
					sg_compilerAnalyzed = true;
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: compiler=%s", CompilerHelper::CompilerType == CompilerType::Borland ? L"Borland" : L"Msvc");
				}

#if defined(_M_IX86)
				if (!sg_signVerifyHooked)
				{
					static constexpr auto kPatternSignVerifyMsvc = "\x57\x8B\xF9\x8B\x8F\x80\x2A\x2A\x2A\x85\xC9\x75\x2A\x68\x2A\x2A\x2A\x2A\x8B\xCF\xE8\x2A\x2A\x2A\x2A\x5F\xC3";
					sg_rawSignVerifyMsvc = reinterpret_cast<pPatchSignVerifyMsvc>(Pe::FindData(GetModuleHandleW(nullptr), kPatternSignVerifyMsvc, strlen(kPatternSignVerifyMsvc)));
					if (sg_rawSignVerifyMsvc)
					{
						bool failed = DetourAttachFunc(&sg_rawSignVerifyMsvc, PatchSignVerifyMsvc);
						sg_signVerifyHooked = !failed;
						LogMessage(sg_signVerifyHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: sign verify hook=%s", sg_signVerifyHooked ? L"ok" : L"failed");
					}
					else
					{
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: sign verify pattern not found");
					}
				}
#else
				if (!sg_signVerifyHooked)
				{
					sg_signVerifyVTable = CompilerHelper::FindVTable("KrkrSign::VerifierImpl");
					if (sg_signVerifyVTable)
					{
						sg_rawSignVerifyResultMsvc = reinterpret_cast<pPatchSignVerifyResultMsvc>(sg_signVerifyVTable[4]);
						sg_signVerifyHooked = WritePointer(sg_signVerifyVTable + 4, reinterpret_cast<void*>(PatchSignVerifyResultMsvc));
						LogMessage(sg_signVerifyHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: x64 sign verify vtable hook=%s", sg_signVerifyHooked ? L"ok" : L"failed");
					}
					else
					{
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: x64 sign verify vtable not found");
					}
				}

				if (!sg_createStreamByIndexHooked)
				{
					sg_xp3ArchiveVTable = CompilerHelper::FindVTable("tTVPXP3Archive");
					if (sg_xp3ArchiveVTable)
					{
						sg_rawCreateStreamByIndexMsvc = reinterpret_cast<pCreateStreamByIndexMsvc>(sg_xp3ArchiveVTable[3]);
						sg_createStreamByIndexHooked = WritePointer(sg_xp3ArchiveVTable + 3, reinterpret_cast<void*>(PatchCreateStreamByIndexMsvc));
						LogMessage(sg_createStreamByIndexHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: x64 XP3Archive::CreateStreamByIndex hook=%s", sg_createStreamByIndexHooked ? L"ok" : L"failed");
					}
					else
					{
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: x64 XP3Archive vtable not found");
					}
				}
#endif

				switch (CompilerHelper::CompilerType)
				{
#if defined(_M_IX86)
				case CompilerType::Borland:
				{
					static constexpr auto kPatternCreateStreamBorland = "\x55\x8B\xEC\x81\xC4\x60\xFF\xFF\xFF\x53\x56\x57\x89\x95\x6C\xFF\xFF\xFF\x89\x85\x70\xFF\xFF\xFF\xB8\x2A\x2A\x2A\x2A\xC7\x85\x7C\xFF\xFF\xFF\x2A\x2A\x2A\x2A\x89\x65\x80\x89\x85\x78\xFF\xFF\xFF\x66\xC7\x45\x84\x2A\x2A\x33\xD2\x89\x55\x90\x64\x8B\x0D\x2A\x2A\x2A\x2A\x89\x8D\x74\xFF\xFF\xFF\x8D\x85\x74\xFF\xFF\xFF\x64\xA3\x2A\x2A\x2A\x2A\x66\xC7\x45\x84\x08\x2A\x8B\x95\x6C\xFF\xFF\xFF\x8B\x85\x70\xFF\xFF\xFF\xE8\x2A\x2A\x2A\x2A\x8B\x95\x74\xFF\xFF\xFF\x64\x89\x15\x2A\x2A\x2A\x2A\xE9\x2A\x06\x2A\x2A";
					sg_rawCreateStreamBorland = reinterpret_cast<pCreateStreamBorland>(Pe::FindData(kPatternCreateStreamBorland, strlen(kPatternCreateStreamBorland)));
					if (sg_rawCreateStreamBorland)
					{
						bool failed = DetourAttachFunc(&sg_rawCreateStreamBorland, CompilerHelper::WrapAsStaticFunc<tTJSBinaryStream*, PatchCreateStreamBorland, const ttstr&, tjs_uint32>());
						sg_createStreamHooked = !failed;
						LogMessage(sg_createStreamHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: borland CreateStream pattern/hook=%s", sg_createStreamHooked ? L"ok" : L"failed");
					}
					else
					{
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: borland CreateStream pattern not found");
					}
					break;
				}
#endif
				case CompilerType::Msvc:
				{
					sg_rawCreateStreamMsvc = ResolveCreateStreamMsvc();
					if (sg_rawCreateStreamMsvc)
					{
						bool failed = DetourAttachFunc(&sg_rawCreateStreamMsvc, PatchCreateStreamMsvc);
						sg_createStreamHooked = !failed;
#if defined(_M_IX86)
						LogMessage(sg_createStreamHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: msvc CreateStream pattern/hook=%s", sg_createStreamHooked ? L"ok" : L"failed");
#else
						LogMessage(sg_createStreamHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: x64 msvc CreateStream hook=%s", sg_createStreamHooked ? L"ok" : L"failed");
#endif
					}
					else
					{
						LogMessage(LogLevel::Warn, L"KrkrPluginBridge: msvc CreateStream target not found");
					}
					break;
				}
				default:
					break;
				}

				LogMessage(sg_createStreamHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: create stream hook=%s", sg_createStreamHooked ? L"ok" : L"failed");
#if !defined(_M_IX86)
				LogMessage(sg_createStreamByIndexHooked ? LogLevel::Info : LogLevel::Warn, L"KrkrPluginBridge: CreateStreamByIndex hook=%s", sg_createStreamByIndexHooked ? L"ok" : L"failed");
#endif
			}

#undef KRKR_MSVC_HOOK_CALL
		}

		void ConfigureKrkrPluginPatchTargets(
			bool enableKrkrPatch,
			bool verboseLog,
			bool bootstrapBypass,
			const std::wstring& gameDir,
			const std::vector<std::wstring>& patchRoots,
			const std::vector<std::wstring>& customPakFiles,
			const std::vector<std::wstring>& patchBaseNames,
			const std::vector<std::wstring>& patchFolders,
			const std::vector<std::wstring>& patchArchives)
		{
			sg_enableKrkrPatch = enableKrkrPatch;
			sg_enableKrkrPatchVerboseLog = verboseLog;
			sg_enableKrkrBootstrapBypass = bootstrapBypass;
			sg_krkrBootstrapBypassTried = false;
			sg_gameDir = NormalizeSlashes(gameDir);
			sg_gameDirLower = ToLowerCopy(sg_gameDir);
			sg_patchRoots = patchRoots;
			sg_patchCustomPaks = customPakFiles;
			sg_patchBaseNames = patchBaseNames;
			sg_patchFolders = patchFolders;
			sg_patchArchives = patchArchives;

			if (!enableKrkrPatch)
			{
				LogMessage(LogLevel::Info, L"KrkrPluginBridge: disabled");
				return;
			}

			if (sg_enableKrkrPatchVerboseLog)
			{
				LogMessage(LogLevel::Info, L"KrkrPluginBridge: roots=%u customPaks=%u bases=%u folders=%u archives=%u gameDir=%s",
					(uint32_t)sg_patchRoots.size(),
					(uint32_t)sg_patchCustomPaks.size(),
					(uint32_t)sg_patchBaseNames.size(),
					(uint32_t)sg_patchFolders.size(),
					(uint32_t)sg_patchArchives.size(),
					sg_gameDir.c_str());
				for (size_t i = 0; i < sg_patchRoots.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: root[%u]=%s", (uint32_t)i, sg_patchRoots[i].c_str());
				}
				for (size_t i = 0; i < sg_patchCustomPaks.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: customPak[%u]=%s", (uint32_t)i, sg_patchCustomPaks[i].c_str());
				}
				for (size_t i = 0; i < sg_patchBaseNames.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: base[%u]=%s", (uint32_t)i, sg_patchBaseNames[i].c_str());
				}
				for (size_t i = 0; i < sg_patchFolders.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: folder[%u]=%s", (uint32_t)i, sg_patchFolders[i].c_str());
				}
				for (size_t i = 0; i < sg_patchArchives.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"KrkrPluginBridge: archive[%u]=%s", (uint32_t)i, sg_patchArchives[i].c_str());
				}
			}
			EnsureHooksInstalled();
		}
	}
}
