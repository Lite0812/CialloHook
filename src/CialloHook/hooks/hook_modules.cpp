#include "hook_modules.h"
#include "alice_system3x_hooks.h"
#include "binary_patch_1337.h"
#include "krkr_plugin_bridge.h"
#include "rio_shiina_hooks.h"

#include <Windows.h>
#include <gdiplus.h>
#include <string>
#include <cwctype>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#pragma comment(lib, "gdiplus.lib")

#include "../../RuntimeCore/io/File.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../RuntimeCore/hook/Hook.h"
#include "../../RuntimeCore/hook/Hook_API.h"

using namespace Rut::FileX;
using namespace Rut::HookX;

#if defined(NDEBUG)
#define CIALLOHOOK_VERBOSE_INFO_LOG(...) ((void)0)
#else
#define CIALLOHOOK_VERBOSE_INFO_LOG(...) LogMessage(LogLevel::Info, __VA_ARGS__)
#endif

namespace CialloHook
{
	namespace HookModules
	{
		struct ResolvedFontRedirectRule
		{
			std::wstring sourceFont;
			std::wstring targetFont;
		};

		struct ResolvedFontFileSource
		{
			std::wstring resolvedDiskPath;
			bool fromCustomPak = false;
			bool usedTempCache = false;
		};

		struct ResolvedFontTarget
		{
			std::wstring effectiveFaceName;
			std::vector<std::wstring> candidateFaces;
			std::wstring sourcePath;
			std::wstring discoveryMethod;
			int selectedIndex = 0;
			bool usedManualOverride = false;
			bool usedFallbackBasename = false;
		};

		struct RegistryBootstrapKeyRecord
			{
				HKEY root = nullptr;
				std::wstring keyPath;
				bool keyExisted = false;
			};

			struct RegistryBootstrapValueRecord
			{
				HKEY root = nullptr;
				std::wstring keyPath;
				std::wstring valueName;
				bool valueExisted = false;
				DWORD originalType = REG_NONE;
				std::vector<BYTE> originalData;
			};

			static std::vector<std::wstring> sg_loadedTempFontFiles;
			static std::vector<std::wstring> sg_activeFontPatchFolders;
			static std::vector<std::wstring> sg_activeModuleAssetPatchFolders;
			static bool sg_activeModuleAssetCustomPak = false;
			static std::vector<RegistryBootstrapKeyRecord> sg_registryBootstrapKeys;
			static std::vector<RegistryBootstrapValueRecord> sg_registryBootstrapValues;
			static bool sg_registryBootstrapCleanupOnExit = true;

		static bool IsBlankText(const std::wstring& text)
		{
			for (wchar_t c : text)
			{
				if (!iswspace(c))
				{
					return false;
				}
			}
			return true;
		}

		static char* SaveStrOnHeap(const std::string& text)
		{
			char* p = new char[text.size() + 1];
			memcpy(p, text.c_str(), text.size() + 1);
			return p;
		}

		static wchar_t* SaveWStrOnHeap(const std::wstring& text)
		{
			wchar_t* p = new wchar_t[text.size() + 1];
			memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
			return p;
		}

		static bool CommitDetourBatchIfStarted(bool batchStarted, const wchar_t* operationName)
		{
			return !batchStarted || EndDetourBatch(operationName);
		}

		static bool EqualsInsensitive(const std::wstring& left, const std::wstring& right)
			{
				return _wcsicmp(left.c_str(), right.c_str()) == 0;
			}

			static bool TryResolveRegistryRoot(const std::wstring& rootName, HKEY& rootKey)
			{
				if (EqualsInsensitive(rootName, L"HKCU") || EqualsInsensitive(rootName, L"HKEY_CURRENT_USER"))
				{
					rootKey = HKEY_CURRENT_USER;
					return true;
				}
				if (EqualsInsensitive(rootName, L"HKLM") || EqualsInsensitive(rootName, L"HKEY_LOCAL_MACHINE"))
				{
					rootKey = HKEY_LOCAL_MACHINE;
					return true;
				}
				return false;
			}

			static bool TryParseRegistryDword(const std::wstring& rawValue, DWORD& outValue)
			{
				std::wstring trimmed = Rut::StrX::Trim(rawValue);
				if (trimmed.empty() || trimmed[0] == L'-')
				{
					return false;
				}
				try
				{
					size_t index = 0;
					unsigned long long parsed = std::stoull(trimmed, &index, 0);
					if (index != trimmed.size() || parsed > 0xFFFFFFFFull)
					{
						return false;
					}
					outValue = static_cast<DWORD>(parsed);
					return true;
				}
				catch (...)
				{
					return false;
				}
			}

			static bool HasRegistryBootstrapKeyRecord(HKEY rootKey, const std::wstring& keyPath)
			{
				for (const auto& record : sg_registryBootstrapKeys)
				{
					if (record.root == rootKey && EqualsInsensitive(record.keyPath, keyPath))
					{
						return true;
					}
				}
				return false;
			}

			static bool HasRegistryBootstrapValueRecord(HKEY rootKey, const std::wstring& keyPath, const std::wstring& valueName)
			{
				for (const auto& record : sg_registryBootstrapValues)
				{
					if (record.root == rootKey
						&& EqualsInsensitive(record.keyPath, keyPath)
						&& EqualsInsensitive(record.valueName, valueName))
					{
						return true;
					}
				}
				return false;
			}

			static std::string WideToCodePage(const std::wstring& text, uint32_t codePage)
		{
			if (text.empty())
			{
				return "";
			}

			UINT cp = codePage == 0 ? CP_ACP : (UINT)codePage;
			int len = WideCharToMultiByte(cp, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
			if (len <= 0)
			{
				return "";
			}

			std::string result(len - 1, '\0');
			WideCharToMultiByte(cp, 0, text.c_str(), -1, &result[0], len, NULL, NULL);
			return result;
		}

		static std::wstring GetLowerExtension(const std::wstring& path)
		{
			size_t sepPos = path.find_last_of(L"\\/");
			size_t dotPos = path.find_last_of(L'.');
			if (dotPos == std::wstring::npos || (sepPos != std::wstring::npos && dotPos <= sepPos))
			{
				return L"";
			}
			std::wstring ext = path.substr(dotPos);
			for (wchar_t& c : ext)
			{
				c = towlower(c);
			}
			return ext;
		}

		static bool IsSupportedFontFileExtension(const std::wstring& ext)
		{
			return ext == L".ttf"
				|| ext == L".otf"
				|| ext == L".ttc"
				|| ext == L".otc"
				|| ext == L".fon"
				|| ext == L".fnt";
		}

		static bool HasPathSeparator(const std::wstring& path)
		{
			return path.find_first_of(L"\\/") != std::wstring::npos;
		}

		static bool IsRegularFilePath(const std::wstring& path)
		{
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		static bool IsAbsolutePath(const std::wstring& path)
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

		static std::wstring GetGameDirectory()
		{
			wchar_t exePath[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePath, MAX_PATH);
			std::wstring gameDir = exePath;
			size_t pos = gameDir.find_last_of(L"\\/");
			if (pos != std::wstring::npos)
			{
				gameDir = gameDir.substr(0, pos);
			}
			else
			{
				gameDir.clear();
			}
			return gameDir;
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
			wchar_t tail = left.back();
			if (tail == L'\\' || tail == L'/')
			{
				return left + right;
			}
			return left + L"\\" + right;
		}

		static std::wstring GetDirectoryPath(const std::wstring& path)
		{
			size_t pos = path.find_last_of(L"\\/");
			if (pos == std::wstring::npos)
			{
				return L"";
			}
			return path.substr(0, pos);
		}

		static void TrackLoadedTempFontFile(const std::wstring& filePath)
		{
			if (filePath.empty())
			{
				return;
			}
			for (const std::wstring& existing : sg_loadedTempFontFiles)
			{
				if (_wcsicmp(existing.c_str(), filePath.c_str()) == 0)
				{
					return;
				}
			}
			sg_loadedTempFontFiles.push_back(filePath);
		}

		void CleanupLoadedFontTempFiles()
		{
			std::wstring processDir;
			for (const std::wstring& fontPath : sg_loadedTempFontFiles)
			{
				if (processDir.empty())
				{
					processDir = GetDirectoryPath(fontPath);
				}
				RemoveFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
				SetFileAttributesW(fontPath.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(fontPath.c_str());
			}
			sg_loadedTempFontFiles.clear();
			if (!processDir.empty())
			{
				RemoveDirectoryTreeIfEmpty(processDir.c_str(), 4);
			}
		}

		static std::wstring ToGameAbsolutePath(const std::wstring& gameDir, const std::wstring& inputPath)
		{
			if (inputPath.empty())
			{
				return L"";
			}
			if (IsAbsolutePath(inputPath))
			{
				return inputPath;
			}
			return JoinPath(gameDir, inputPath);
		}

		static std::wstring NormalizeLocalPathSlashes(std::wstring path)
		{
			for (wchar_t& c : path)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			return path;
		}

		static bool TryBuildGameRelativePath(const std::wstring& gameDir, const std::wstring& inputPath, std::wstring& relativePath)
		{
			relativePath.clear();
			if (inputPath.empty())
			{
				return false;
			}

			std::wstring normalizedInput = NormalizeLocalPathSlashes(inputPath);
			if (!IsAbsolutePath(normalizedInput))
			{
				relativePath = normalizedInput;
			}
			else
			{
				std::wstring normalizedGameDir = NormalizeLocalPathSlashes(gameDir);
				if (normalizedGameDir.empty() || _wcsnicmp(normalizedInput.c_str(), normalizedGameDir.c_str(), normalizedGameDir.size()) != 0)
				{
					return false;
				}
				if (normalizedInput.size() > normalizedGameDir.size() && normalizedInput[normalizedGameDir.size()] != L'\\')
				{
					return false;
				}
				relativePath = normalizedInput.substr(normalizedGameDir.size());
			}

			while (!relativePath.empty() && (relativePath[0] == L'.' || relativePath[0] == L'\\'))
			{
				relativePath.erase(relativePath.begin());
			}
			return !relativePath.empty();
		}

		static std::wstring TrimWide(std::wstring value)
		{
			size_t begin = 0;
			while (begin < value.size() && iswspace(static_cast<wint_t>(value[begin])) != 0)
			{
				++begin;
			}
			size_t end = value.size();
			while (end > begin && iswspace(static_cast<wint_t>(value[end - 1])) != 0)
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}

		static bool EndsWithNoCase(const std::wstring& value, const wchar_t* suffix)
		{
			if (!suffix)
			{
				return false;
			}
			size_t suffixLen = wcslen(suffix);
			if (value.size() < suffixLen)
			{
				return false;
			}
			return _wcsicmp(value.c_str() + value.size() - suffixLen, suffix) == 0;
		}

		static std::wstring NormalizeKrkrPatchBaseName(std::wstring value)
		{
			value = TrimWide(std::move(value));
			for (wchar_t& c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			while (!value.empty() && (value.back() == L'\\' || value.back() == L'/'))
			{
				value.pop_back();
			}
			if (EndsWithNoCase(value, L".xp3"))
			{
				value.resize(value.size() - 4);
			}
			return value;
		}

		static std::wstring NormalizeArchiveSpec(std::wstring value)
		{
			value = TrimWide(std::move(value));
			for (wchar_t& c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			size_t chainPos = value.find(L'>');
			size_t trimEnd = (chainPos == std::wstring::npos) ? value.size() : chainPos;
			while (trimEnd > 0 && value[trimEnd - 1] == L'\\')
			{
				value.erase(trimEnd - 1, 1);
				--trimEnd;
				if (chainPos != std::wstring::npos)
				{
					--chainPos;
				}
			}
			return value;
		}

		static bool IsExplicitKrkrArchiveSpec(const std::wstring& value)
		{
			return value.find(L'>') != std::wstring::npos
				|| EndsWithNoCase(value, L".cpk");
		}

		static std::wstring GetArchiveSpecOuterPath(const std::wstring& archiveSpec)
		{
			size_t chainPos = archiveSpec.find(L'>');
			if (chainPos == std::wstring::npos)
			{
				return archiveSpec;
			}
			return archiveSpec.substr(0, chainPos);
		}

		static std::wstring ResolveArchiveSpecFromRoot(const std::wstring& root, const std::wstring& archiveSpec)
		{
			std::wstring normalizedSpec = NormalizeArchiveSpec(archiveSpec);
			if (normalizedSpec.empty())
			{
				return normalizedSpec;
			}

			std::wstring outerPath = GetArchiveSpecOuterPath(normalizedSpec);
			if (root.empty() || IsAbsolutePath(outerPath))
			{
				return normalizedSpec;
			}

			return root + L"\\" + normalizedSpec;
		}

		static void AppendUniquePath(std::vector<std::wstring>& values, const std::wstring& value);

		static void CollectKrkrPatchBaseNames(const EnginePatchSettings& enginePatchSettings, std::vector<std::wstring>& baseNames)
		{
			baseNames.clear();
			if (!enginePatchSettings.enableKrkrPatch)
			{
				return;
			}

			std::vector<std::wstring> configuredNames = enginePatchSettings.krkrPatchNames;
			if (configuredNames.empty())
			{
				return;
			}
			std::reverse(configuredNames.begin(), configuredNames.end());
			for (const std::wstring& configuredName : configuredNames)
			{
				std::wstring trimmedName = TrimWide(configuredName);
				if (trimmedName.empty() || IsExplicitKrkrArchiveSpec(trimmedName))
				{
					continue;
				}

				std::wstring baseName = NormalizeKrkrPatchBaseName(trimmedName);
				if (!baseName.empty())
				{
					AppendUniquePath(baseNames, baseName);
				}
			}
		}

		static void AppendUniquePath(std::vector<std::wstring>& values, const std::wstring& value)
		{
			if (value.empty())
			{
				return;
			}
			for (const std::wstring& existing : values)
			{
				if (_wcsicmp(existing.c_str(), value.c_str()) == 0)
				{
					return;
				}
			}
			values.push_back(value);
		}

		static bool IsExistingDirectory(const std::wstring& path);
		static bool IsExistingRegularFile(const std::wstring& path);

		static bool IsNestedUnderAnyPatchFolder(const std::wstring& targetPath, const std::vector<std::wstring>& patchFolders)
		{
			for (const std::wstring& patchFolder : patchFolders)
			{
				size_t patchLen = patchFolder.size();
				if (patchLen == 0 || targetPath.size() <= patchLen)
				{
					continue;
				}
				if (_wcsnicmp(targetPath.c_str(), patchFolder.c_str(), patchLen) == 0 && targetPath[patchLen] == L'\\')
				{
					return true;
				}
			}
			return false;
		}

		static void CollectKrkrPatchTargets(
			const EnginePatchSettings& enginePatchSettings,
			const std::wstring& gameDir,
			const std::vector<std::wstring>& searchRoots,
			std::vector<std::wstring>& patchFolders,
			std::vector<std::wstring>& customPakFiles)
		{
			if (!enginePatchSettings.enableKrkrPatch)
			{
				return;
			}

			std::vector<std::wstring> baseNames = enginePatchSettings.krkrPatchNames;
			if (baseNames.empty())
			{
				if (enginePatchSettings.krkrPatchVerboseLog)
				{
					LogMessage(LogLevel::Info, L"KrkrPatch: enabled but no KrkrPatchName configured");
				}
				return;
			}
			std::reverse(baseNames.begin(), baseNames.end());

			uint32_t resolvedFolderCount = 0;
			uint32_t resolvedArchiveCount = 0;

			for (const std::wstring& root : searchRoots)
			{
				for (const std::wstring& configuredBaseName : baseNames)
				{
					std::wstring trimmedName = TrimWide(configuredBaseName);
					if (IsExplicitKrkrArchiveSpec(trimmedName))
					{
						continue;
					}
					std::wstring baseName = NormalizeKrkrPatchBaseName(configuredBaseName);
					if (baseName.empty())
					{
						continue;
					}

					std::wstring targetBase = root.empty() ? baseName : (root + L"\\" + baseName);
					if (IsExistingDirectory(ToGameAbsolutePath(gameDir, targetBase)))
					{
						AppendUniquePath(patchFolders, targetBase);
						++resolvedFolderCount;
					}
				}
			}

			for (const std::wstring& root : searchRoots)
			{
				for (const std::wstring& configuredBaseName : baseNames)
				{
					std::wstring trimmedName = TrimWide(configuredBaseName);
					if (IsExplicitKrkrArchiveSpec(trimmedName))
					{
						std::wstring archiveSpec = ResolveArchiveSpecFromRoot(root, trimmedName);
						std::wstring archiveFile = ToGameAbsolutePath(gameDir, GetArchiveSpecOuterPath(archiveSpec));
						if (archiveSpec.empty())
						{
							continue;
						}
						if (IsExistingRegularFile(archiveFile))
						{
							AppendUniquePath(customPakFiles, archiveSpec);
							++resolvedArchiveCount;
						}
						else
						{
							LogMessage(LogLevel::Warn, L"KrkrPatch: archive target missing, skip: %s", archiveFile.c_str());
						}
						continue;
					}

					std::wstring baseName = NormalizeKrkrPatchBaseName(configuredBaseName);
					if (baseName.empty())
					{
						continue;
					}

					std::wstring targetBase = root.empty() ? baseName : (root + L"\\" + baseName);
					std::wstring archiveName = targetBase + L".xp3";
					bool foundAny = false;
					if (IsExistingRegularFile(ToGameAbsolutePath(gameDir, archiveName)))
					{
						AppendUniquePath(customPakFiles, archiveName);
						foundAny = true;
						++resolvedArchiveCount;
					}
					if (IsExistingDirectory(ToGameAbsolutePath(gameDir, targetBase)))
					{
						foundAny = true;
					}
					if (!foundAny)
					{
						LogMessage(LogLevel::Warn, L"KrkrPatch: target missing, skip: %s (dir/.xp3)", targetBase.c_str());
					}
				}
			}

			if (enginePatchSettings.krkrPatchVerboseLog)
			{
				LogMessage(LogLevel::Info, L"KrkrPatch: configured=%u activeFolders=%u activeArchives=%u",
					(uint32_t)baseNames.size(),
					resolvedFolderCount,
					resolvedArchiveCount);
			}
		}

		static bool IsExistingDirectory(const std::wstring& path)
		{
			if (path.empty())
			{
				return false;
			}
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		static bool IsExistingRegularFile(const std::wstring& path)
		{
			if (path.empty())
			{
				return false;
			}
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		static std::wstring GetBaseNameWithoutExtension(const std::wstring& path)
		{
			std::wstring fileName = PathRemoveExtension(path);
			size_t pos = fileName.find_last_of(L"\\/");
			return (pos == std::wstring::npos) ? fileName : fileName.substr(pos + 1);
		}

		static void AppendUniqueFace(std::vector<std::wstring>& faces, const std::wstring& face)
		{
			if (face.empty())
			{
				return;
			}
			for (const std::wstring& existing : faces)
			{
				if (_wcsicmp(existing.c_str(), face.c_str()) == 0)
				{
					return;
				}
			}
			faces.push_back(face);
		}

		static int CALLBACK CollectFontFaceProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD, LPARAM lParam)
		{
			auto* faces = reinterpret_cast<std::vector<std::wstring>*>(lParam);
			if (faces && logFont)
			{
				AppendUniqueFace(*faces, logFont->lfFaceName);
			}
			return 1;
		}

		static std::vector<std::wstring> EnumerateAllFontFacesSnapshot()
		{
			std::vector<std::wstring> faces;
			HDC hdc = GetDC(nullptr);
			if (!hdc)
			{
				return faces;
			}
			LOGFONTW lf = {};
			lf.lfCharSet = DEFAULT_CHARSET;
			EnumFontFamiliesExW(hdc, &lf, reinterpret_cast<FONTENUMPROCW>(CollectFontFaceProc), reinterpret_cast<LPARAM>(&faces), 0);
			ReleaseDC(nullptr, hdc);
			return faces;
		}

		static std::vector<std::wstring> DiffFontFaceSnapshots(const std::vector<std::wstring>& before, const std::vector<std::wstring>& after)
		{
			std::vector<std::wstring> diff;
			for (const std::wstring& face : after)
			{
				bool exists = false;
				for (const std::wstring& oldFace : before)
				{
					if (_wcsicmp(face.c_str(), oldFace.c_str()) == 0)
					{
						exists = true;
						break;
					}
				}
				if (!exists)
				{
					AppendUniqueFace(diff, face);
				}
			}
			return diff;
		}

		struct ScopedGdiplusSession
		{
			ULONG_PTR token = 0;
			bool started = false;

			ScopedGdiplusSession()
			{
				Gdiplus::GdiplusStartupInput input;
				started = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
			}

			~ScopedGdiplusSession()
			{
				if (started)
				{
					Gdiplus::GdiplusShutdown(token);
				}
			}
		};

		static std::vector<std::wstring> DiscoverSfntFontFaces(const std::wstring& fontFilePath)
		{
			std::vector<std::wstring> faces;
			ScopedGdiplusSession gdiplus;
			if (!gdiplus.started)
			{
				return faces;
			}

			std::vector<std::wstring> familyNames;
			{
				Gdiplus::PrivateFontCollection collection;
				if (collection.AddFontFile(fontFilePath.c_str()) != Gdiplus::Ok)
				{
					return faces;
				}
				INT familyCount = collection.GetFamilyCount();
				if (familyCount <= 0)
				{
					return faces;
				}
				std::vector<Gdiplus::FontFamily> families(static_cast<size_t>(familyCount));
				INT foundFamilies = 0;
				collection.GetFamilies(familyCount, families.data(), &foundFamilies);
				for (INT i = 0; i < foundFamilies; ++i)
				{
					wchar_t familyName[LF_FACESIZE] = {};
					if (families[static_cast<size_t>(i)].GetFamilyName(familyName, LANG_NEUTRAL) == Gdiplus::Ok)
					{
						AppendUniqueFace(familyNames, familyName);
					}
				}
			}

			if (familyNames.empty())
			{
				return faces;
			}

			if (AddFontResourceExW(fontFilePath.c_str(), FR_PRIVATE, nullptr) > 0)
			{
				HDC hdc = GetDC(nullptr);
				if (hdc)
				{
					for (const std::wstring& familyName : familyNames)
					{
						LOGFONTW lf = {};
						lf.lfCharSet = DEFAULT_CHARSET;
						wcsncpy_s(lf.lfFaceName, familyName.c_str(), _TRUNCATE);
						EnumFontFamiliesExW(hdc, &lf, reinterpret_cast<FONTENUMPROCW>(CollectFontFaceProc), reinterpret_cast<LPARAM>(&faces), 0);
					}
					ReleaseDC(nullptr, hdc);
				}
				RemoveFontResourceExW(fontFilePath.c_str(), FR_PRIVATE, nullptr);
			}

			if (faces.empty())
			{
				faces = familyNames;
			}
			return faces;
		}

		static std::vector<std::wstring> DiscoverRasterFontFaces(const std::wstring& fontFilePath)
		{
			std::vector<std::wstring> before = EnumerateAllFontFacesSnapshot();
			if (AddFontResourceExW(fontFilePath.c_str(), FR_PRIVATE, nullptr) <= 0)
			{
				return {};
			}
			std::vector<std::wstring> after = EnumerateAllFontFacesSnapshot();
			RemoveFontResourceExW(fontFilePath.c_str(), FR_PRIVATE, nullptr);
			return DiffFontFaceSnapshots(before, after);
		}

		static std::vector<std::wstring> DiscoverFontFacesFromFile(const std::wstring& fontFilePath, const std::wstring& ext, std::wstring& discoveryMethod)
		{
			discoveryMethod.clear();
			std::vector<std::wstring> faces;
			if (ext == L".ttf" || ext == L".otf" || ext == L".ttc" || ext == L".otc")
			{
				discoveryMethod = L"sfnt-gdiplus+gdi";
				faces = DiscoverSfntFontFaces(fontFilePath);
				if (faces.empty())
				{
					discoveryMethod = L"sfnt-gdi-diff";
					faces = DiscoverRasterFontFaces(fontFilePath);
				}
			}
			else if (ext == L".fon" || ext == L".fnt")
			{
				discoveryMethod = L"raster-gdi-diff";
				faces = DiscoverRasterFontFaces(fontFilePath);
			}
			else
			{
				/* Unknown extension (e.g. prefix-resolved font with .dll/.dat/etc).
				 * Try sfnt first (covers TrueType/OpenType), then raster as fallback. */
				discoveryMethod = L"universal-sfnt";
				faces = DiscoverSfntFontFaces(fontFilePath);
				if (faces.empty())
				{
					discoveryMethod = L"universal-gdi-diff";
					faces = DiscoverRasterFontFaces(fontFilePath);
				}
			}
			return faces;
		}

		static bool ResolveFontFileSourceFromCustomPakPath(const std::wstring& path, ResolvedFontFileSource& sourceOut, bool& foundCandidate)
		{
			foundCandidate = false;
			std::shared_ptr<const std::vector<uint8_t>> data;
			if (!ResolveCustomPakVFSData(path.c_str(), data) || !data || data->empty())
			{
				return false;
			}
			foundCandidate = true;
			std::wstring cachePath;
			if (!TryGetCustomPakDiskCachePath(path.c_str(), cachePath) || cachePath.empty())
			{
				LogMessage(LogLevel::Error, L"Font CustomPak extraction failed: shared cache unavailable for %s", path.c_str());
				return false;
			}
			sourceOut.resolvedDiskPath = cachePath;
			sourceOut.fromCustomPak = true;
			sourceOut.usedTempCache = true;
			return true;
		}

		static bool FindCustomPakFontFileByName(const std::wstring& fileName, std::wstring& virtualPathOut)
		{
			virtualPathOut.clear();
			std::vector<std::wstring> pendingDirs;
			pendingDirs.push_back(L"");
			for (size_t index = 0; index < pendingDirs.size(); ++index)
			{
				const std::wstring currentDir = pendingDirs[index];
				std::vector<CustomPakVFSDirectoryEntry> entries;
				if (!EnumerateCustomPakVFSDirectory(currentDir.c_str(), entries))
				{
					continue;
				}
				for (const CustomPakVFSDirectoryEntry& entry : entries)
				{
					std::wstring childPath = currentDir.empty() ? entry.name : (currentDir + L"\\" + entry.name);
					if (entry.isDirectory)
					{
						pendingDirs.push_back(childPath);
						continue;
					}
					if (_wcsicmp(entry.name.c_str(), fileName.c_str()) == 0)
					{
						virtualPathOut = childPath;
						return true;
					}
				}
			}
			return false;
		}

		/* ---- Font file prefix search ----
		 * When Font config has no recognized extension (e.g. "我是可爱字体"),
		 * search the game directory and VFS for any file whose stem matches.
		 * This allows using arbitrary file extensions (e.g. .dll, .dat) for font files. */

		static bool FindFontFileByPrefixLocal(const std::wstring& searchDir, const std::wstring& prefix, std::wstring& foundPath)
		{
			if (prefix.empty() || searchDir.empty())
			{
				return false;
			}
			std::wstring pattern = JoinPath(searchDir, prefix) + L".*";
			WIN32_FIND_DATAW fd = {};
			HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
			if (hFind == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			do
			{
				if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					continue;
				}
				/* Verify the stem matches exactly (case-insensitive) */
				std::wstring candidateName = fd.cFileName;
				size_t dot = candidateName.find_last_of(L'.');
				std::wstring stem = (dot != std::wstring::npos) ? candidateName.substr(0, dot) : candidateName;
				if (_wcsicmp(stem.c_str(), prefix.c_str()) == 0)
				{
					foundPath = JoinPath(searchDir, candidateName);
					FindClose(hFind);
					return true;
				}
			} while (FindNextFileW(hFind, &fd));
			FindClose(hFind);
			return false;
		}

		static bool FindFontFileByPrefixInCustomPak(const std::wstring& prefix, std::wstring& virtualPathOut)
		{
			virtualPathOut.clear();
			if (prefix.empty())
			{
				return false;
			}
			std::vector<std::wstring> pendingDirs;
			pendingDirs.push_back(L"");
			for (size_t index = 0; index < pendingDirs.size(); ++index)
			{
				const std::wstring currentDir = pendingDirs[index];
				std::vector<CustomPakVFSDirectoryEntry> entries;
				if (!EnumerateCustomPakVFSDirectory(currentDir.c_str(), entries))
				{
					continue;
				}
				for (const CustomPakVFSDirectoryEntry& entry : entries)
				{
					std::wstring childPath = currentDir.empty() ? entry.name : (currentDir + L"\\" + entry.name);
					if (entry.isDirectory)
					{
						pendingDirs.push_back(childPath);
						continue;
					}
					/* Check if the file stem matches the prefix (case-insensitive) */
					size_t dot = entry.name.find_last_of(L'.');
					std::wstring stem = (dot != std::wstring::npos) ? entry.name.substr(0, dot) : entry.name;
					if (_wcsicmp(stem.c_str(), prefix.c_str()) == 0)
					{
						virtualPathOut = childPath;
						return true;
					}
				}
			}
			return false;
		}

		/* Try to load a candidate file as a font. Returns true if AddFontResourceExW accepts it. */
		static bool TryLoadCandidateAsFont(const std::wstring& filePath)
		{
			if (filePath.empty())
			{
				return false;
			}
			int result = AddFontResourceExW(filePath.c_str(), FR_PRIVATE, nullptr);
			if (result > 0)
			{
				/* It's a valid font file. Remove it now; the caller will load it properly later. */
				RemoveFontResourceExW(filePath.c_str(), FR_PRIVATE, nullptr);
				return true;
			}
			return false;
		}

		static bool IsSameOrNestedRelativePath(const std::wstring& path, const std::wstring& folder)
		{
			if (path.empty() || folder.empty())
			{
				return false;
			}
			std::wstring normalizedPath = NormalizeLocalPathSlashes(path);
			std::wstring normalizedFolder = NormalizeLocalPathSlashes(folder);
			if (_wcsicmp(normalizedPath.c_str(), normalizedFolder.c_str()) == 0)
			{
				return true;
			}
			std::wstring folderPrefix = normalizedFolder + L"\\";
			return normalizedPath.size() >= folderPrefix.size()
				&& _wcsnicmp(normalizedPath.c_str(), folderPrefix.c_str(), folderPrefix.size()) == 0;
		}


		struct ModuleAssetResolution
		{
			std::wstring resolvedPath;
			std::wstring source;
			bool fromCustomPak = false;
		};

		static bool TryResolveModuleAssetDiskPath(const std::wstring& configuredPath, ModuleAssetResolution& resolvedOut)
		{
			resolvedOut = {};
			std::wstring gameDir = GetGameDirectory();
			std::wstring relativePath;
			if (TryBuildGameRelativePath(gameDir, configuredPath, relativePath))
			{
				for (size_t i = sg_activeModuleAssetPatchFolders.size(); i > 0; --i)
				{
					const std::wstring& folder = sg_activeModuleAssetPatchFolders[i - 1];
					if (folder.empty() || IsSameOrNestedRelativePath(relativePath, folder))
					{
						continue;
					}
					std::wstring patchCandidate = ToGameAbsolutePath(gameDir, JoinPath(folder, relativePath));
					if (IsExistingRegularFile(patchCandidate))
					{
						resolvedOut.resolvedPath = patchCandidate;
						resolvedOut.source = L"PatchFolder";
						return true;
					}
				}

				if (sg_activeModuleAssetCustomPak)
				{
					std::wstring cachePath;
					std::wstring queryPath = ToGameAbsolutePath(gameDir, relativePath);
					if (TryGetCustomPakDiskCachePath(queryPath.c_str(), cachePath) && !cachePath.empty())
					{
						resolvedOut.resolvedPath = cachePath;
						resolvedOut.source = L"CustomPak";
						resolvedOut.fromCustomPak = true;
						return true;
					}
				}

				std::wstring rootCandidate = ToGameAbsolutePath(gameDir, relativePath);
				if (IsExistingRegularFile(rootCandidate))
				{
					resolvedOut.resolvedPath = rootCandidate;
					resolvedOut.source = L"Root";
					return true;
				}
			}
			else if (IsAbsolutePath(configuredPath) && IsExistingRegularFile(configuredPath))
			{
				resolvedOut.resolvedPath = configuredPath;
				resolvedOut.source = L"Root";
				return true;
			}
			return false;
		}

		static bool ResolveLocalFontFileSource(const std::wstring& fontPathOrFileName, ResolvedFontFileSource& sourceOut, bool& foundCandidate, bool includeGameFile)
		{
			foundCandidate = false;
			std::wstring gameDir = GetGameDirectory();
			std::wstring relativePath;
			if (TryBuildGameRelativePath(gameDir, fontPathOrFileName, relativePath))
			{
				for (size_t i = sg_activeFontPatchFolders.size(); i > 0; --i)
				{
					const std::wstring& folder = sg_activeFontPatchFolders[i - 1];
					if (folder.empty() || IsSameOrNestedRelativePath(relativePath, folder))
					{
						continue;
					}
					std::wstring patchCandidate = ToGameAbsolutePath(gameDir, JoinPath(folder, relativePath));
					if (IsRegularFilePath(patchCandidate))
					{
						foundCandidate = true;
						sourceOut.resolvedDiskPath = patchCandidate;
						LogMessage(LogLevel::Info, L"Font local patch resolved: %s => %s", fontPathOrFileName.c_str(), patchCandidate.c_str());
						return true;
					}
				}

				if (includeGameFile)
				{
					std::wstring gameCandidate = ToGameAbsolutePath(gameDir, relativePath);
					if (IsRegularFilePath(gameCandidate))
					{
						foundCandidate = true;
						sourceOut.resolvedDiskPath = gameCandidate;
						return true;
					}
				}
			}
			else if (includeGameFile && IsAbsolutePath(fontPathOrFileName) && IsRegularFilePath(fontPathOrFileName))
			{
				foundCandidate = true;
				sourceOut.resolvedDiskPath = fontPathOrFileName;
				return true;
			}

			return false;
		}

		static bool ResolveFontFileSourcePreferLocal(const std::wstring& fontPathOrFileName, ResolvedFontFileSource& sourceOut)
		{
			sourceOut = {};
			bool localFileExists = false;
			if (ResolveLocalFontFileSource(fontPathOrFileName, sourceOut, localFileExists, false))
			{
				return true;
			}
			bool customPakFound = false;
			if (ResolveFontFileSourceFromCustomPakPath(fontPathOrFileName, sourceOut, customPakFound))
			{
				return true;
			}

			/* Try common font subdirectory prefixes in VFS */
			if (!HasPathSeparator(fontPathOrFileName))
			{
				bool nestedCustomPakFound = false;
				if (ResolveFontFileSourceFromCustomPakPath(L"fonts\\" + fontPathOrFileName, sourceOut, nestedCustomPakFound))
				{
					return true;
				}
				customPakFound = customPakFound || nestedCustomPakFound;
				nestedCustomPakFound = false;
				if (ResolveFontFileSourceFromCustomPakPath(L"font\\" + fontPathOrFileName, sourceOut, nestedCustomPakFound))
				{
					return true;
				}
				customPakFound = customPakFound || nestedCustomPakFound;
			}

			/* Always try VFS recursive search by filename, regardless of path separators.
			 * This fixes the bug where fonts in VFS subdirectories could only be found
			 * when placed at the root level. Extract the bare filename for the search. */
			{
				std::wstring bareFileName = fontPathOrFileName;
				size_t lastSep = fontPathOrFileName.find_last_of(L"\\/");
				if (lastSep != std::wstring::npos)
				{
					bareFileName = fontPathOrFileName.substr(lastSep + 1);
				}
				if (!bareFileName.empty())
				{
					std::wstring discoveredCustomPakPath;
					if (FindCustomPakFontFileByName(bareFileName, discoveredCustomPakPath))
					{
						LogMessage(LogLevel::Info, L"Font CustomPak discovered by file name: %s => %s", fontPathOrFileName.c_str(), discoveredCustomPakPath.c_str());
						bool discoveredCustomPakFound = false;
						if (ResolveFontFileSourceFromCustomPakPath(discoveredCustomPakPath, sourceOut, discoveredCustomPakFound))
						{
							return true;
						}
						customPakFound = customPakFound || discoveredCustomPakFound;
					}
				}
			}

			if (!customPakFound && ResolveLocalFontFileSource(fontPathOrFileName, sourceOut, localFileExists, true))
			{
				return true;
			}

			if (localFileExists || customPakFound)
			{
				std::wstring errMsg = std::wstring(L"Failed to load font file: ") + fontPathOrFileName;
				MessageBoxW(nullptr, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
			}
			else
			{
				std::wstring errMsg = std::wstring(L"Font file not found: ") + fontPathOrFileName;
				MessageBoxW(nullptr, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
			}
			return false;
		}

		static std::wstring FormatFaceListForLog(const std::vector<std::wstring>& faces)
		{
			if (faces.empty())
			{
				return L"[]";
			}
			std::wstring text = L"[";
			for (size_t i = 0; i < faces.size(); ++i)
			{
				if (i != 0)
				{
					text += L", ";
				}
				text += L"{";
				text += std::to_wstring(i);
				text += L":";
				text += faces[i];
				text += L"}";
			}
			text += L"]";
			return text;
		}

		static void LogResolvedFontTarget(const std::wstring& logContext, const std::wstring& originalConfig, const ResolvedFontTarget& target)
		{
			LogMessage(LogLevel::Info,
				L"ResolveFont: context=%s config=%s source=%s method=%s candidates=%s override=%d selectedIndex=%d selected=%s fallbackBasename=%d",
				logContext.c_str(),
				originalConfig.c_str(),
				target.sourcePath.c_str(),
				target.discoveryMethod.c_str(),
				FormatFaceListForLog(target.candidateFaces).c_str(),
				target.usedManualOverride ? 1 : 0,
				target.selectedIndex,
				target.effectiveFaceName.c_str(),
				target.usedFallbackBasename ? 1 : 0);
		}

		static ResolvedFontTarget ResolveConfiguredFontTarget(const std::wstring& fontConfig, const std::wstring& fontNameOverride, const std::wstring& logContext)
		{
			ResolvedFontTarget result = {};
			if (fontConfig.empty())
			{
				return result;
			}

			std::wstring ext = GetLowerExtension(fontConfig);
			std::wstring resolvedFontFilePath;
			bool resolvedViaPrefix = false;

			if (!IsSupportedFontFileExtension(ext))
			{
				/* Extension is not a known font format. Before treating fontConfig as
				 * a literal font family name, try prefix-based file search:
				 * search the game directory and VFS for any file whose stem matches
				 * fontConfig, then validate it as a font file. This allows font files
				 * with arbitrary extensions (e.g. .dll, .dat) to be loaded by prefix. */

				/* Only attempt prefix search when fontConfig has no extension at all,
				 * or has a non-font extension (i.e. user is likely specifying a prefix) */
				std::wstring prefix = fontConfig;
				/* If fontConfig contains a dot but not a recognized font extension,
				 * treat the whole thing as a prefix (stem) */
				size_t lastSepPos = fontConfig.find_last_of(L"\\/");
				size_t lastDotPos = fontConfig.find_last_of(L'.');
				bool hasNonFontExt = (lastDotPos != std::wstring::npos
					&& (lastSepPos == std::wstring::npos || lastDotPos > lastSepPos));
				if (!hasNonFontExt)
				{
					/* No dot at all — pure prefix like "我是可爱字体" */
				}
				else
				{
					/* Has a dot but unrecognized extension — also treat as prefix search candidate.
					 * Use the part before the extension as the prefix. */
					prefix = fontConfig.substr(0, lastDotPos);
				}

				/* Extract just the stem name (no directory part) for file search */
				std::wstring prefixStem = prefix;
				size_t prefixSepPos = prefix.find_last_of(L"\\/");
				if (prefixSepPos != std::wstring::npos)
				{
					prefixStem = prefix.substr(prefixSepPos + 1);
				}

				if (!prefixStem.empty())
				{
					std::wstring gameDir = GetGameDirectory();
					std::wstring candidatePath;
					bool found = false;

					/* 1. Search game directory for file with matching stem */
					if (!found && FindFontFileByPrefixLocal(gameDir, prefixStem, candidatePath))
					{
						if (TryLoadCandidateAsFont(candidatePath))
						{
							resolvedFontFilePath = candidatePath;
							resolvedViaPrefix = true;
							found = true;
							LogMessage(LogLevel::Info, L"ResolveFont: context=%s prefix=%s found local file=%s",
								logContext.c_str(), prefixStem.c_str(), candidatePath.c_str());
						}
						else
						{
							LogMessage(LogLevel::Debug, L"ResolveFont: context=%s prefix=%s local file=%s is not a valid font, skip",
								logContext.c_str(), prefixStem.c_str(), candidatePath.c_str());
						}
					}

					/* 2. Search VFS (CustomPak) for file with matching stem */
					if (!found)
					{
						std::wstring virtualPath;
						if (FindFontFileByPrefixInCustomPak(prefixStem, virtualPath))
						{
							ResolvedFontFileSource vfsSource = {};
							bool vfsFound = false;
							if (ResolveFontFileSourceFromCustomPakPath(virtualPath, vfsSource, vfsFound) && !vfsSource.resolvedDiskPath.empty())
							{
								if (TryLoadCandidateAsFont(vfsSource.resolvedDiskPath))
								{
									resolvedFontFilePath = vfsSource.resolvedDiskPath;
									resolvedViaPrefix = true;
									found = true;
									LogMessage(LogLevel::Info, L"ResolveFont: context=%s prefix=%s found VFS file=%s (disk=%s)",
										logContext.c_str(), prefixStem.c_str(), virtualPath.c_str(), vfsSource.resolvedDiskPath.c_str());
									if (vfsSource.usedTempCache)
									{
										TrackLoadedTempFontFile(vfsSource.resolvedDiskPath);
									}
								}
								else
								{
									LogMessage(LogLevel::Debug, L"ResolveFont: context=%s prefix=%s VFS file=%s is not a valid font, skip",
										logContext.c_str(), prefixStem.c_str(), virtualPath.c_str());
								}
							}
						}
					}

					if (!found)
					{
						/* No font file found by prefix; fall back to treating as font family name */
						CIALLOHOOK_VERBOSE_INFO_LOG(L"ResolveFont: context=%s prefix=%s no matching font file found, treating as font family name",
							logContext.c_str(), prefixStem.c_str());
					}
				}

				if (!resolvedViaPrefix)
				{
					result.effectiveFaceName = fontConfig;
					result.discoveryMethod = L"literal-font-name";
					return result;
				}

				/* A font file was found via prefix. Determine its actual extension for face discovery. */
				ext = GetLowerExtension(resolvedFontFilePath);
			}

			/* ---- Font file loading path ---- */
			ResolvedFontFileSource source = {};
			if (resolvedViaPrefix)
			{
				/* Already resolved and validated above */
				source.resolvedDiskPath = resolvedFontFilePath;
			}
			else if (!ResolveFontFileSourcePreferLocal(fontConfig, source))
			{
				result.effectiveFaceName = L"SimSun";
				result.discoveryMethod = L"fallback-load-failed";
				LogResolvedFontTarget(logContext, fontConfig, result);
				return result;
			}

			result.sourcePath = source.resolvedDiskPath;
			result.candidateFaces = DiscoverFontFacesFromFile(source.resolvedDiskPath, ext, result.discoveryMethod);
			if (result.discoveryMethod.empty())
			{
				result.discoveryMethod = resolvedViaPrefix ? L"prefix-match" : L"fallback-basename";
			}
			else if (resolvedViaPrefix)
			{
				result.discoveryMethod = L"prefix-" + result.discoveryMethod;
			}

			if (!resolvedViaPrefix && !LoadFontFromFile(source.resolvedDiskPath.c_str(), false))
			{
				std::wstring errMsg = std::wstring(L"Failed to load font file: ") + fontConfig;
				MessageBoxW(nullptr, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
				LogMessage(LogLevel::Error, L"ResolveFont: context=%s AddFontResourceExW rejected source=%s", logContext.c_str(), source.resolvedDiskPath.c_str());
				result.effectiveFaceName = L"SimSun";
				LogResolvedFontTarget(logContext, fontConfig, result);
				return result;
			}
			if (resolvedViaPrefix)
			{
				/* For prefix-resolved files, we already validated via TryLoadCandidateAsFont.
				 * Now do the real load (the validate step removed it). */
				if (!LoadFontFromFile(source.resolvedDiskPath.c_str(), false))
				{
					LogMessage(LogLevel::Error, L"ResolveFont: context=%s prefix-resolved file rejected on reload source=%s",
						logContext.c_str(), source.resolvedDiskPath.c_str());
					result.effectiveFaceName = L"SimSun";
					result.discoveryMethod = L"prefix-fallback-load-failed";
					LogResolvedFontTarget(logContext, fontConfig, result);
					return result;
				}
			}
			if (source.usedTempCache && !resolvedViaPrefix)
			{
				TrackLoadedTempFontFile(source.resolvedDiskPath);
			}

			if (!fontNameOverride.empty())
			{
				result.usedManualOverride = true;
				result.effectiveFaceName = fontNameOverride;
			}
			else if (!result.candidateFaces.empty())
			{
				result.selectedIndex = 0;
				result.effectiveFaceName = result.candidateFaces[0];
			}
			else
			{
				result.usedFallbackBasename = true;
				result.selectedIndex = 0;
				result.discoveryMethod = resolvedViaPrefix ? L"prefix-fallback-basename" : L"fallback-basename";
				result.effectiveFaceName = GetBaseNameWithoutExtension(
					resolvedViaPrefix ? resolvedFontFilePath : fontConfig);
			}

			LogResolvedFontTarget(logContext, fontConfig, result);
			return result;
		}

		static ResolvedFontTarget ResolveFontTarget(const FontSettings& settings)
		{
			return ResolveConfiguredFontTarget(settings.font, settings.fontNameOverride, L"Font");
		}

		void ApplyFontHooks(const FontSettings& settings)
		{
			const bool hasFontOverride = !IsBlankText(settings.font);
			if (!hasFontOverride && !settings.enableCnJpMap)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: Font is empty and cn-jp map disabled, skip font hooks");
				return;
			}

			EnableCnJpMap(false);
			EnableCnJpMapVerboseLog(settings.cnJpMapVerboseLog);
			SetCnJpMapEncoding(settings.cnJpMapReadEncoding);
			if (settings.enableCnJpMap)
			{
				ModuleAssetResolution mapJson = {};
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: cn-jp map requested path=%s", settings.cnJpMapJson.c_str());
				if (!TryResolveModuleAssetDiskPath(settings.cnJpMapJson, mapJson))
				{
					LogMessage(LogLevel::Error, L"ApplyFontHooks: cn-jp map json missing: %s", settings.cnJpMapJson.c_str());
				}
				else
				{
					bool loaded = LoadCnJpMapFile(mapJson.resolvedPath.c_str());
					EnableCnJpMap(loaded);
					LogMessage(loaded ? LogLevel::Info : LogLevel::Error,
						L"ApplyFontHooks: cn-jp map file=%s source=%s load=%s",
						mapJson.resolvedPath.c_str(),
						mapJson.source.c_str(),
						loaded ? L"success" : L"failed");
				}
			}

			ResolvedFontTarget resolvedMainFont = ResolveFontTarget(settings);
			std::wstring fontName = resolvedMainFont.effectiveFaceName;
			std::vector<ResolvedFontRedirectRule> resolvedRedirectRules;
			resolvedRedirectRules.reserve(settings.redirectRules.size());
			for (size_t i = 0; i < settings.redirectRules.size(); ++i)
			{
				const FontRedirectRule& rule = settings.redirectRules[i];
				ResolvedFontRedirectRule resolvedRule;
				resolvedRule.sourceFont = TrimWide(rule.sourceFont);
				ResolvedFontTarget resolvedTarget = ResolveConfiguredFontTarget(
					rule.targetFont,
					rule.targetFontNameOverride,
					L"RedirectToFont_" + std::to_wstring(i));
				resolvedRule.targetFont = resolvedTarget.effectiveFaceName;
				if (resolvedRule.sourceFont.empty() || resolvedRule.targetFont.empty())
				{
					continue;
				}
				resolvedRedirectRules.push_back(std::move(resolvedRule));
			}
			std::vector<const wchar_t*> skipFontNames;
			skipFontNames.reserve(settings.skipFonts.size());
			for (const std::wstring& font : settings.skipFonts)
			{
				if (!TrimWide(font).empty())
				{
					skipFontNames.push_back(font.c_str());
				}
			}
			std::vector<const wchar_t*> redirectFromFontNames;
			std::vector<const wchar_t*> redirectToFontNames;
			redirectFromFontNames.reserve(resolvedRedirectRules.size());
			redirectToFontNames.reserve(resolvedRedirectRules.size());
			for (const ResolvedFontRedirectRule& rule : resolvedRedirectRules)
			{
				redirectFromFontNames.push_back(rule.sourceFont.c_str());
				redirectToFontNames.push_back(rule.targetFont.c_str());
			}
			SetFontHookRules(
				skipFontNames.empty() ? nullptr : skipFontNames.data(),
				skipFontNames.size(),
				redirectFromFontNames.empty() ? nullptr : redirectFromFontNames.data(),
				redirectToFontNames.empty() ? nullptr : redirectToFontNames.data(),
				redirectFromFontNames.size());
			LogMessage(LogLevel::Info, L"ApplyFontHooks: font=%s charset=0x%X spoof=%d (0x%X->0x%X) unlock=%d cnJpMap=%s verboseLog=%d cnJpMapCp=%u",
				fontName.c_str(),
				settings.charset,
				settings.enableCharsetSpoof ? 1 : 0,
				settings.spoofFromCharset,
				settings.spoofToCharset,
				settings.unlockFontSelection ? 1 : 0,
				settings.enableCnJpMap ? L"enabled" : L"disabled",
				settings.cnJpMapVerboseLog ? 1 : 0,
				settings.cnJpMapReadEncoding);
			CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: skipRules=%u redirectRules=%u",
				(uint32_t)skipFontNames.size(),
				(uint32_t)resolvedRedirectRules.size());
			for (size_t i = 0; i < skipFontNames.size(); ++i)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: skipFont[%u]=%s", (uint32_t)i, skipFontNames[i]);
			}
			for (size_t i = 0; i < resolvedRedirectRules.size(); ++i)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: redirectFont[%u] %s -> %s",
					(uint32_t)i,
					resolvedRedirectRules[i].sourceFont.c_str(),
					resolvedRedirectRules[i].targetFont.c_str());
			}
			CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: glyphOffset=(%d,%d) metricsOffset=(L%d,R%d,T%d,B%d)",
				settings.glyphOffsetX, settings.glyphOffsetY,
				settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFontHooks: GDI+ hooks draw=%d drawDriver=%d measure=%d measureRanges=%d measureDriver=%d lateLoad=%d",
				settings.hookGdipDrawString ? 1 : 0,
				settings.hookGdipDrawDriverString ? 1 : 0,
				settings.hookGdipMeasureString ? 1 : 0,
				settings.hookGdipMeasureCharacterRanges ? 1 : 0,
				settings.hookGdipMeasureDriverString ? 1 : 0,
				settings.hookLoadLibraryExW || settings.hookLoadLibraryW ? 1 : 0);
			std::string fontNameA = WideToCodePage(fontName, 0);
			uint32_t attachOkCount = 0;
			uint32_t attachFailCount = 0;
			std::wstring attachFailedApis;
			const bool detourBatchStarted = BeginDetourBatch();
			auto logHookAttach = [&](const wchar_t* hookName, bool detourResult)
			{
				const bool ok = !detourResult;
				if (ok)
				{
					++attachOkCount;
					return;
				}
				++attachFailCount;
				if (!attachFailedApis.empty())
				{
					attachFailedApis += L", ";
				}
				attachFailedApis += hookName;
			};

			if (settings.hookCreateFontA)
			{
				HookCreateFontA(settings.charset, settings.enableCharsetSpoof, settings.spoofFromCharset, settings.spoofToCharset, SaveStrOnHeap(fontNameA), settings.fontHeight, settings.fontWidth, settings.fontWeight, settings.fontScale, settings.fontSpacingScale, settings.glyphAspectRatio, settings.glyphOffsetX, settings.glyphOffsetY, settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			}

			if (settings.hookCreateFontIndirectA)
			{
				HookCreateFontIndirectA(settings.charset, settings.enableCharsetSpoof, settings.spoofFromCharset, settings.spoofToCharset, SaveStrOnHeap(fontNameA), settings.fontHeight, settings.fontWidth, settings.fontWeight, settings.fontScale, settings.fontSpacingScale, settings.glyphAspectRatio, settings.glyphOffsetX, settings.glyphOffsetY, settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			}

			if (settings.hookCreateFontW)
			{
				HookCreateFontW(settings.charset, settings.enableCharsetSpoof, settings.spoofFromCharset, settings.spoofToCharset, SaveWStrOnHeap(fontName), settings.fontHeight, settings.fontWidth, settings.fontWeight, settings.fontScale, settings.fontSpacingScale, settings.glyphAspectRatio, settings.glyphOffsetX, settings.glyphOffsetY, settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			}

			if (settings.hookCreateFontIndirectW)
			{
				HookCreateFontIndirectW(settings.charset, settings.enableCharsetSpoof, settings.spoofFromCharset, settings.spoofToCharset, SaveWStrOnHeap(fontName), settings.fontHeight, settings.fontWidth, settings.fontWeight, settings.fontScale, settings.fontSpacingScale, settings.glyphAspectRatio, settings.glyphOffsetX, settings.glyphOffsetY, settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			}

			if (settings.hookEnumFontFamiliesExA)
			{
				logHookAttach(L"HookEnumFontFamiliesExA", HookEnumFontFamiliesExA(settings.unlockFontSelection));
			}

			if (settings.hookEnumFontFamiliesExW)
			{
				logHookAttach(L"HookEnumFontFamiliesExW", HookEnumFontFamiliesExW(settings.unlockFontSelection));
			}

			if (settings.hookEnumFontsA)
			{
				logHookAttach(L"HookEnumFontsA", HookEnumFontsA(settings.unlockFontSelection));
			}

			if (settings.hookEnumFontsW)
			{
				logHookAttach(L"HookEnumFontsW", HookEnumFontsW(settings.unlockFontSelection));
			}

			if (settings.hookEnumFontFamiliesA)
			{
				logHookAttach(L"HookEnumFontFamiliesA", HookEnumFontFamiliesA(settings.unlockFontSelection));
			}

			if (settings.hookEnumFontFamiliesW)
			{
				logHookAttach(L"HookEnumFontFamiliesW", HookEnumFontFamiliesW(settings.unlockFontSelection));
			}

			if (settings.unlockFontSelection)
			{
				HookChooseFontA();
				HookChooseFontW();
			}

			if (settings.hookCreateFontIndirectExA)
			{
				HookCreateFontIndirectExA();
			}

			if (settings.hookCreateFontIndirectExW)
			{
				HookCreateFontIndirectExW();
			}

			if (settings.hookGetObjectA)
			{
				HookGetObjectA();
			}

			if (settings.hookGetObjectW)
			{
				HookGetObjectW();
			}

			if (settings.hookGetTextFaceA)
			{
				HookGetTextFaceA();
			}

			if (settings.hookGetTextFaceW)
			{
				HookGetTextFaceW();
			}

			if (settings.hookGetTextMetricsA)
			{
				HookGetTextMetricsA();
			}

			if (settings.hookGetTextMetricsW)
			{
				HookGetTextMetricsW();
			}

			if (settings.hookGetCharABCWidthsA)
			{
				HookGetCharABCWidthsA();
			}

			if (settings.hookGetCharABCWidthsW)
			{
				HookGetCharABCWidthsW();
			}

			if (settings.hookGetCharABCWidthsFloatA)
			{
				HookGetCharABCWidthsFloatA();
			}

			if (settings.hookGetCharABCWidthsFloatW)
			{
				HookGetCharABCWidthsFloatW();
			}

			if (settings.hookGetCharWidthA)
			{
				HookGetCharWidthA();
			}

			if (settings.hookGetCharWidthW)
			{
				HookGetCharWidthW();
			}

			if (settings.hookGetCharWidth32A)
			{
				HookGetCharWidth32A();
			}

			if (settings.hookGetCharWidth32W)
			{
				HookGetCharWidth32W();
			}

			if (settings.hookGetKerningPairsA)
			{
				HookGetKerningPairsA();
			}

			if (settings.hookGetKerningPairsW)
			{
				HookGetKerningPairsW();
			}

			if (settings.hookGetOutlineTextMetricsA)
			{
				HookGetOutlineTextMetricsA();
			}

			if (settings.hookGetOutlineTextMetricsW)
			{
				HookGetOutlineTextMetricsW();
			}

			if (settings.hookAddFontResourceA)
			{
				logHookAttach(L"HookAddFontResourceA", HookAddFontResourceA());
			}

			if (settings.hookAddFontResourceW)
			{
				logHookAttach(L"HookAddFontResourceW", HookAddFontResourceW());
			}

			if (settings.hookAddFontResourceExA)
			{
				logHookAttach(L"HookAddFontResourceExA", HookAddFontResourceExA());
			}

			if (settings.hookAddFontMemResourceEx)
			{
				logHookAttach(L"HookAddFontMemResourceEx", HookAddFontMemResourceEx());
			}

			if (settings.hookRemoveFontResourceA)
			{
				logHookAttach(L"HookRemoveFontResourceA", HookRemoveFontResourceA());
			}

			if (settings.hookRemoveFontResourceW)
			{
				logHookAttach(L"HookRemoveFontResourceW", HookRemoveFontResourceW());
			}

			if (settings.hookRemoveFontResourceExA)
			{
				logHookAttach(L"HookRemoveFontResourceExA", HookRemoveFontResourceExA());
			}

			if (settings.hookRemoveFontMemResourceEx)
			{
				logHookAttach(L"HookRemoveFontMemResourceEx", HookRemoveFontMemResourceEx());
			}

			if (settings.hookGetCharWidthFloatA)
			{
				logHookAttach(L"HookGetCharWidthFloatA", HookGetCharWidthFloatA());
			}

			if (settings.hookGetCharWidthFloatW)
			{
				logHookAttach(L"HookGetCharWidthFloatW", HookGetCharWidthFloatW());
			}

			if (settings.hookGetCharWidthI)
			{
				logHookAttach(L"HookGetCharWidthI", HookGetCharWidthI());
			}

			if (settings.hookGetCharABCWidthsI)
			{
				logHookAttach(L"HookGetCharABCWidthsI", HookGetCharABCWidthsI());
			}

			if (settings.hookGetTextExtentPointI)
			{
				logHookAttach(L"HookGetTextExtentPointI", HookGetTextExtentPointI());
			}

			if (settings.hookGetTextExtentExPointI)
			{
				logHookAttach(L"HookGetTextExtentExPointI", HookGetTextExtentExPointI());
			}

			if (settings.hookGetFontData)
			{
				logHookAttach(L"HookGetFontData", HookGetFontData());
			}

			if (settings.hookGetFontLanguageInfo)
			{
				logHookAttach(L"HookGetFontLanguageInfo", HookGetFontLanguageInfo());
			}

			if (settings.hookGetFontUnicodeRanges)
			{
				logHookAttach(L"HookGetFontUnicodeRanges", HookGetFontUnicodeRanges());
			}

			if (settings.hookLoadLibraryW)
			{
				logHookAttach(L"HookLoadLibraryW", HookLoadLibraryW());
			}

			if (settings.hookLoadLibraryExW)
			{
				logHookAttach(L"HookLoadLibraryExW", HookLoadLibraryExW());
			}

			if (settings.hookDWriteCreateFactory)
			{
				logHookAttach(L"HookDWriteCreateFactory", HookDWriteCreateFactory());
					logHookAttach(L"HookD2D1CreateFactory", HookD2D1CreateFactory());
			}

			if (settings.hookGdipCreateFontFamilyFromName)
			{
				logHookAttach(L"HookGdipCreateFontFamilyFromName", HookGdipCreateFontFamilyFromName());
			}

			if (settings.hookGdipCreateFontFromLogfontW)
			{
				logHookAttach(L"HookGdipCreateFontFromLogfontW", HookGdipCreateFontFromLogfontW());
			}

			if (settings.hookGdipCreateFontFromLogfontA)
			{
				logHookAttach(L"HookGdipCreateFontFromLogfontA", HookGdipCreateFontFromLogfontA());
			}

			if (settings.hookGdipCreateFontFromHFONT)
			{
				logHookAttach(L"HookGdipCreateFontFromHFONT", HookGdipCreateFontFromHFONT());
			}

			if (settings.hookGdipCreateFontFromDC)
			{
				logHookAttach(L"HookGdipCreateFontFromDC", HookGdipCreateFontFromDC());
			}

			if (settings.hookGdipCreateFont)
			{
				logHookAttach(L"HookGdipCreateFont", HookGdipCreateFont());
			}

			if (settings.hookGdipDrawString)
			{
				logHookAttach(L"HookGdipDrawString", HookGdipDrawString());
			}

			if (settings.hookGdipDrawDriverString)
			{
				logHookAttach(L"HookGdipDrawDriverString", HookGdipDrawDriverString());
			}

			if (settings.hookGdipMeasureString)
			{
				logHookAttach(L"HookGdipMeasureString", HookGdipMeasureString());
			}

			if (settings.hookGdipMeasureCharacterRanges)
			{
				logHookAttach(L"HookGdipMeasureCharacterRanges", HookGdipMeasureCharacterRanges());
			}

			if (settings.hookGdipMeasureDriverString)
			{
				logHookAttach(L"HookGdipMeasureDriverString", HookGdipMeasureDriverString());
			}

			const bool detourBatchCommitted = CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyFontHooks detour batch");
			if (!detourBatchCommitted)
			{
				LogMessage(LogLevel::Warn, L"ApplyFontHooks: detour batch commit failed");
			}
			if (attachOkCount || attachFailCount)
			{
				LogMessage((attachFailCount || !detourBatchCommitted) ? LogLevel::Warn : LogLevel::Info,
					L"ApplyFontHooks: extended attach summary ok=%u failed=%u",
					attachOkCount, attachFailCount);
				if (attachFailCount)
				{
					LogMessage(LogLevel::Warn, L"ApplyFontHooks: failed apis: %s", attachFailedApis.c_str());
				}
			}
			LogMessage(LogLevel::Debug, L"Font hooks enabled: A=%d IA=%d W=%d IW=%d EFA=%d EFW=%d",
				settings.hookCreateFontA ? 1 : 0,
				settings.hookCreateFontIndirectA ? 1 : 0,
				settings.hookCreateFontW ? 1 : 0,
				settings.hookCreateFontIndirectW ? 1 : 0,
				settings.hookEnumFontFamiliesExA ? 1 : 0,
				settings.hookEnumFontFamiliesExW ? 1 : 0);
		}


		void ApplyTextHooks(const TextReplaceSettings& settings, const EnginePatchSettings& enginePatchSettings)
		{
			UINT textReadCp = settings.readEncoding != 0
				? (UINT)settings.readEncoding
				: (settings.encoding == 0 ? CP_ACP : (UINT)settings.encoding);
			UINT textWriteCp = settings.writeEncoding != 0 ? (UINT)settings.writeEncoding : textReadCp;
			SetTextReplaceEncodings(textReadCp, textWriteCp);
			EnableTextReplaceVerboseLog(settings.enableVerboseLog);
			bool enableWaffleTextCrashPatch = enginePatchSettings.enableWafflePatch && enginePatchSettings.waffleFixGetTextCrash;
			SetWaffleGetTextCrashPatchEnabled(enableWaffleTextCrashPatch);
			CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyTextHooks: rule count=%u, readEncoding=%u, writeEncoding=%u, verbose=%d, waffleGetTextCrash=%d",
				(uint32_t)settings.rules.size(), textReadCp, textWriteCp, settings.enableVerboseLog ? 1 : 0, enableWaffleTextCrashPatch ? 1 : 0);
			if (settings.rules.empty())
			{
				LogMessage(LogLevel::Debug, L"ApplyTextHooks: no replacement rules, keep api hooks for metric/outline adjustments");
			}

			for (size_t i = 0; i < settings.rules.size(); ++i)
			{
				const auto& rule = settings.rules[i];
				std::string originalA = WideToCodePage(rule.first, textReadCp);
				std::string replacementA = WideToCodePage(rule.second, textWriteCp);
				AddTextReplaceRule(SaveStrOnHeap(originalA), SaveStrOnHeap(replacementA));
				AddTextReplaceRuleW(SaveWStrOnHeap(rule.first), SaveWStrOnHeap(rule.second));
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyTextHooks: rule[%u] \"%s\" -> \"%s\"",
					(uint32_t)i, rule.first.c_str(), rule.second.c_str());
			}

			uint32_t successCount = 0;
			uint32_t failCount = 0;
			uint32_t disabledCount = 0;
			uint32_t enabledCount = 0;
			std::wstring failedApis;
			const bool detourBatchStarted = BeginDetourBatch();
			auto hookAndLog = [&](const wchar_t* apiName, bool enabled, bool(*hookFunc)())
			{
				if (!enabled)
				{
					++disabledCount;
					return;
				}
				++enabledCount;
				bool failed = hookFunc();
				if (!failed)
				{
					++successCount;
				}
				else
				{
					++failCount;
					if (!failedApis.empty())
					{
						failedApis += L", ";
					}
					failedApis += apiName;
				}
			};

			hookAndLog(L"TextOutA", settings.hookTextOutA, HookTextOutA);
			hookAndLog(L"TextOutW", settings.hookTextOutW, HookTextOutW);
			hookAndLog(L"ExtTextOutA", settings.hookExtTextOutA, HookExtTextOutA);
			hookAndLog(L"ExtTextOutW", settings.hookExtTextOutW, HookExtTextOutW);
			hookAndLog(L"DrawTextA", settings.hookDrawTextA, HookDrawTextA);
			hookAndLog(L"DrawTextW", settings.hookDrawTextW, HookDrawTextW);
			hookAndLog(L"DrawTextExA", settings.hookDrawTextExA, HookDrawTextExA);
			hookAndLog(L"DrawTextExW", settings.hookDrawTextExW, HookDrawTextExW);
			hookAndLog(L"PolyTextOutA", settings.hookPolyTextOutA, HookPolyTextOutA);
			hookAndLog(L"PolyTextOutW", settings.hookPolyTextOutW, HookPolyTextOutW);
			hookAndLog(L"TabbedTextOutA", settings.hookTabbedTextOutA, HookTabbedTextOutA);
			hookAndLog(L"TabbedTextOutW", settings.hookTabbedTextOutW, HookTabbedTextOutW);
			hookAndLog(L"GetTabbedTextExtentA", settings.hookGetTabbedTextExtentA, HookGetTabbedTextExtentA);
			hookAndLog(L"GetTabbedTextExtentW", settings.hookGetTabbedTextExtentW, HookGetTabbedTextExtentW);
			hookAndLog(L"GetTextExtentPoint32A", settings.hookGetTextExtentPoint32A || enableWaffleTextCrashPatch, HookGetTextExtentPoint32A);
			hookAndLog(L"GetTextExtentPoint32W", settings.hookGetTextExtentPoint32W, HookGetTextExtentPoint32W);
			hookAndLog(L"GetTextExtentExPointA", settings.hookGetTextExtentExPointA, HookGetTextExtentExPointA);
			hookAndLog(L"GetTextExtentExPointW", settings.hookGetTextExtentExPointW, HookGetTextExtentExPointW);
			hookAndLog(L"GetTextExtentPointA", settings.hookGetTextExtentPointA, HookGetTextExtentPointA);
			hookAndLog(L"GetTextExtentPointW", settings.hookGetTextExtentPointW, HookGetTextExtentPointW);
			hookAndLog(L"GetCharacterPlacementA", settings.hookGetCharacterPlacementA, HookGetCharacterPlacementA);
			hookAndLog(L"GetCharacterPlacementW", settings.hookGetCharacterPlacementW, HookGetCharacterPlacementW);
			hookAndLog(L"GetGlyphIndicesA", settings.hookGetGlyphIndicesA, HookGetGlyphIndicesA);
			hookAndLog(L"GetGlyphIndicesW", settings.hookGetGlyphIndicesW, HookGetGlyphIndicesW);
			hookAndLog(L"GetGlyphOutlineA", settings.hookGetGlyphOutlineA, HookGetGlyphOutlineA);
			hookAndLog(L"GetGlyphOutlineW", settings.hookGetGlyphOutlineW, HookGetGlyphOutlineW);
				hookAndLog(L"MessageBoxA", settings.hookMessageBoxA, HookMessageBoxA);
				hookAndLog(L"SetDlgItemTextA", settings.hookSetDlgItemTextA, HookSetDlgItemTextA);
				hookAndLog(L"SendDlgItemMessageA", settings.hookSendDlgItemMessageA, HookSendDlgItemMessageA);
				hookAndLog(L"SendDlgItemMessageW", settings.hookSendDlgItemMessageW, HookSendDlgItemMessageW);
				hookAndLog(L"SendMessageA", settings.hookSendMessageA, HookSendMessageA);
				hookAndLog(L"SendMessageW", settings.hookSendMessageW, HookSendMessageW);
				hookAndLog(L"AppendMenuA", settings.hookAppendMenuA, HookAppendMenuA);
				hookAndLog(L"ModifyMenuA", settings.hookModifyMenuA, HookModifyMenuA);
				hookAndLog(L"InsertMenuA", settings.hookInsertMenuA, HookInsertMenuA);
				hookAndLog(L"InsertMenuItemA", settings.hookInsertMenuItemA, HookInsertMenuItemA);
				hookAndLog(L"SetMenuItemInfoA", settings.hookSetMenuItemInfoA, HookSetMenuItemInfoA);
				hookAndLog(L"MessageBoxIndirectA", settings.hookMessageBoxIndirectA, HookMessageBoxIndirectA);
				hookAndLog(L"DefWindowProcA", settings.hookDefWindowProcA, HookDefWindowProcA);
				hookAndLog(L"DefWindowProcW", settings.hookDefWindowProcW, HookDefWindowProcW);
				hookAndLog(L"DialogBoxParamA", settings.hookDialogBoxParamA, HookDialogBoxParamA);
				hookAndLog(L"DialogBoxParamW", settings.hookDialogBoxParamW, HookDialogBoxParamW);
				hookAndLog(L"CreateDialogParamA", settings.hookCreateDialogParamA, HookCreateDialogParamA);
				hookAndLog(L"CreateDialogParamW", settings.hookCreateDialogParamW, HookCreateDialogParamW);
				hookAndLog(L"DialogBoxIndirectParamA", settings.hookDialogBoxIndirectParamA, HookDialogBoxIndirectParamA);
				hookAndLog(L"DialogBoxIndirectParamW", settings.hookDialogBoxIndirectParamW, HookDialogBoxIndirectParamW);
				hookAndLog(L"CreateDialogIndirectParamA", settings.hookCreateDialogIndirectParamA, HookCreateDialogIndirectParamA);
				hookAndLog(L"CreateDialogIndirectParamW", settings.hookCreateDialogIndirectParamW, HookCreateDialogIndirectParamW);
				hookAndLog(L"DrawThemeText", settings.hookDrawThemeText, HookDrawThemeText);
				hookAndLog(L"DrawThemeTextEx", settings.hookDrawThemeTextEx, HookDrawThemeTextEx);
				hookAndLog(L"PropertySheetA", settings.hookPropertySheetA, HookPropertySheetA);
				hookAndLog(L"ExitProcess", settings.hookExitProcessGuard, HookExitProcessGuard);

			const bool detourBatchCommitted = CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyTextHooks detour batch");
			if (!detourBatchCommitted)
			{
				successCount = 0;
				failCount = enabledCount;
				if (!failedApis.empty())
				{
					failedApis += L", ";
				}
				failedApis += L"Detour batch commit";
			}
			LogMessage(failCount > 0 ? LogLevel::Warn : LogLevel::Info,
				L"ApplyTextHooks: summary enabled=%u disabled=%u ok=%u failed=%u",
				enabledCount, disabledCount, successCount, failCount);
			if (!failedApis.empty())
			{
				LogMessage(LogLevel::Warn, L"ApplyTextHooks: failed apis=%s", failedApis.c_str());
			}
		}

		void ApplyWindowTitleHooks(const WindowTitleSettings& settings)
		{
			if (settings.rules.empty())
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyWindowTitleHooks: no rules");
				return;
			}

			for (const auto& rule : settings.rules)
			{
				AddWindowTitleRule(rule.first.c_str(), rule.second.c_str());
			}
			UINT titleReadCp = settings.readEncoding != 0
				? (UINT)settings.readEncoding
				: (settings.encoding == 0 ? CP_ACP : (UINT)settings.encoding);
			UINT titleWriteCp = settings.writeEncoding != 0 ? (UINT)settings.writeEncoding : titleReadCp;
			SetWindowTitleEncodings(titleReadCp, titleWriteCp);
			EnableWindowTitleVerboseLog(settings.enableVerboseLog);

			const bool detourBatchStarted = BeginDetourBatch();
			HookWindowTitleAPIs(settings.titleMode);
			if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyWindowTitleHooks detour batch"))
			{
				LogMessage(LogLevel::Warn, L"ApplyWindowTitleHooks: detour batch commit failed");
			}
			CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyWindowTitleHooks: rule count=%u, mode=%d, readEncoding=%u, writeEncoding=%u, verbose=%d",
				(uint32_t)settings.rules.size(), settings.titleMode, titleReadCp, titleWriteCp, settings.enableVerboseLog ? 1 : 0);
		}

		static bool NeedsTextApiHooks(const AppSettings& settings)
		{
			const bool hasTextRules = !settings.textReplace.rules.empty();
			const bool hasWaffleTextPatch = settings.enginePatches.enableWafflePatch && settings.enginePatches.waffleFixGetTextCrash;
			const FontSettings& font = settings.font;
			const bool hasFontTextWork = !IsBlankText(font.font)
				|| font.enableCnJpMap
				|| font.fontSpacingScale != 1.0f
				|| font.glyphAspectRatio != 1.0f
				|| font.glyphOffsetX != 0
				|| font.glyphOffsetY != 0
				|| font.metricsOffsetLeft != 0
				|| font.metricsOffsetRight != 0
				|| font.metricsOffsetTop != 0
				|| font.metricsOffsetBottom != 0;
			return hasTextRules || hasWaffleTextPatch || hasFontTextWork;
		}

		void ApplyEarlyStartupHooks(const AppSettings& settings, uint32_t bypassThreadId)
		{
			if (!settings.startupTiming.enableStartupWindowGate)
			{
				return;
			}

			EnableStartupWindowGate(true, bypassThreadId);
			const bool detourBatchStarted = BeginDetourBatch();
			bool hookOk = HookWindowTitleAPIs(0);
			if (!hookOk)
			{
				LogMessage(LogLevel::Warn, L"ApplyEarlyStartupHooks: failed to attach startup gate window hooks");
				ReleaseStartupWindowGate();
				return;
			}
			if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyEarlyStartupHooks detour batch"))
			{
				LogMessage(LogLevel::Warn, L"ApplyEarlyStartupHooks: detour batch commit failed");
				ReleaseStartupWindowGate();
				return;
			}
			LogMessage(LogLevel::Info, L"ApplyEarlyStartupHooks: startup gate enabled with bypassTid=%lu", static_cast<unsigned long>(bypassThreadId));
		}

		void ApplyPostStartupHooks(const AppSettings& settings)
		{
			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: file patch");
			ApplyFilePatchHooks(settings.filePatch, settings.fileSpoof, settings.directoryRedirect, settings.enginePatches);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: binary patch");
			ApplyBinaryPatches(settings.binaryPatch);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: alice system3x");
			ApplyAliceSystem3xHooks(settings.aliceSystem3x, settings.filePatch);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: siglus key extract");
			ApplySiglusKeyExtract(settings.siglusKeyExtract);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: rio shiina");
			ApplyRioShiinaHooks(settings.rioShiina, settings.filePatch);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: text");
			if (NeedsTextApiHooks(settings))
			{
				ApplyTextHooks(settings.textReplace, settings.enginePatches);
			}
			else
			{
				LogMessage(LogLevel::Debug, L"ApplyTextHooks: no text/font work, skip text api hooks");
			}

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: window title");
			ApplyWindowTitleHooks(settings.windowTitle);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: registry bootstrap");
			ApplyRegistryBootstrap(settings.registryBootstrap);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: registry");
			ApplyRegistryHooks(settings.registry);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: code page");
			ApplyCodePageHooks(settings.codePage);

			CIALLOHOOK_VERBOSE_INFO_LOG(L"Apply hooks: font");
			ApplyFontHooks(settings.font);
			ReleaseStartupWindowGate();
		}

		void ApplyAliceSystem3xHooks(const AliceSystem3xSettings& settings, const FilePatchSettings& filePatchSettings)
		{
			AliceSystem3xHooks::Apply(settings, filePatchSettings);
		}

		void ApplyRioShiinaHooks(const RioShiinaSettings& settings, const FilePatchSettings& filePatchSettings)
		{
			RioShiinaHooks::Apply(settings, filePatchSettings);
		}

		void ApplySiglusKeyExtract(const SiglusKeyExtractSettings& settings)
		{
			if (!settings.enable)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplySiglusKeyExtract: disabled");
				return;
			}

			std::wstring gameDir = GetGameDirectory();
			std::wstring gameexePath = ToGameAbsolutePath(gameDir, settings.gameexePath);
			std::wstring keyOutputPath = ToGameAbsolutePath(gameDir, settings.keyOutputPath);
			SetKeyExtractConfig(gameexePath.c_str(), keyOutputPath.c_str(), settings.showMessageBox);

			const bool detourBatchStarted = BeginDetourBatch();
			bool keyExtractEnabled = EnableSiglusKeyExtract();
			if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplySiglusKeyExtract detour batch"))
			{
				keyExtractEnabled = false;
				LogMessage(LogLevel::Warn, L"ApplySiglusKeyExtract: detour batch commit failed");
			}
			LogMessage(keyExtractEnabled ? LogLevel::Info : LogLevel::Warn,
				L"ApplySiglusKeyExtract: enabled=%d gameexe=%s output=%s showMessageBox=%d",
				keyExtractEnabled ? 1 : 0,
				gameexePath.c_str(),
				keyOutputPath.c_str(),
				settings.showMessageBox ? 1 : 0);

			if (settings.debugMode)
			{
				if (keyExtractEnabled)
				{
					MessageBoxA(nullptr,
						"Siglus Key Extract: Hook Enabled!\nKey will be extracted when the game reads Gameexe.dat.",
						"CialloHook - Debug",
						MB_OK | MB_ICONINFORMATION);
				}
				else
				{
					MessageBoxA(nullptr,
						"Siglus Key Extract: Not needed or hook failed!\nGameexe.dat may not use second-layer encryption.",
						"CialloHook - Debug",
						MB_OK | MB_ICONWARNING);
				}
			}
		}

		void ApplyFilePatchHooks(const FilePatchSettings& patchSettings, const FileSpoofSettings& spoofSettings, const DirectoryRedirectSettings& directoryRedirectSettings, const EnginePatchSettings& enginePatchSettings)
		{
			sg_activeFontPatchFolders.clear();
			sg_activeModuleAssetPatchFolders.clear();
			sg_activeModuleAssetCustomPak = false;
			if (!patchSettings.enable && !patchSettings.customPakEnable && !spoofSettings.enable && !directoryRedirectSettings.enable && !enginePatchSettings.enableKrkrPatch)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyFilePatchHooks: disabled (patch, custom pak, spoof, redirect and krkrpatch)");
				return;
			}
			std::wstring gameDir = GetGameDirectory();
			std::vector<std::wstring> filePatchFolders;
			if (patchSettings.enable)
			{
				for (const std::wstring& folder : patchSettings.patchFolders)
				{
					std::wstring absFolder = ToGameAbsolutePath(gameDir, folder);
					if (IsExistingDirectory(absFolder))
					{
						AppendUniquePath(filePatchFolders, folder);
					}
					else
					{
						LogMessage(LogLevel::Warn, L"ApplyFilePatchHooks: patch folder missing, skip: %s", absFolder.c_str());
					}
				}
			}

			std::vector<std::wstring> krkrPatchRoots;
			if (!filePatchFolders.empty())
			{
				for (const std::wstring& folder : filePatchFolders)
				{
					AppendUniquePath(krkrPatchRoots, folder);
				}
			}
			krkrPatchRoots.push_back(L"");

			std::vector<std::wstring> krkrPatchFolders;
			std::vector<std::wstring> krkrPatchArchives;
			std::vector<std::wstring> krkrPatchBaseNames;
			CollectKrkrPatchTargets(enginePatchSettings, gameDir, krkrPatchRoots, krkrPatchFolders, krkrPatchArchives);
			CollectKrkrPatchBaseNames(enginePatchSettings, krkrPatchBaseNames);

			std::vector<const wchar_t*> spoofFiles;
			for (const std::wstring& file : spoofSettings.spoofFiles)
			{
				spoofFiles.push_back(file.c_str());
			}
			std::vector<const wchar_t*> spoofDirectories;
			for (const std::wstring& directory : spoofSettings.spoofDirectories)
			{
				spoofDirectories.push_back(directory.c_str());
			}
			bool enableSpoof = spoofSettings.enable && (!spoofFiles.empty() || !spoofDirectories.empty());

			std::vector<const wchar_t*> redirectSources;
			std::vector<const wchar_t*> redirectTargets;
			for (const auto& rule : directoryRedirectSettings.rules)
			{
				if (rule.first.empty() || rule.second.empty())
				{
					continue;
				}
				redirectSources.push_back(rule.first.c_str());
				redirectTargets.push_back(rule.second.c_str());
			}
			bool enableRedirect = directoryRedirectSettings.enable
				&& !redirectSources.empty()
				&& redirectSources.size() == redirectTargets.size();

			std::vector<std::wstring> filePatchCustomPakFiles;
			if (patchSettings.customPakEnable)
			{
				for (const std::wstring& pakFile : patchSettings.customPakFiles)
				{
					std::wstring absPak = ToGameAbsolutePath(gameDir, GetArchiveSpecOuterPath(pakFile));
					if (IsExistingRegularFile(absPak))
					{
						AppendUniquePath(filePatchCustomPakFiles, pakFile);
					}
					else
					{
						LogMessage(LogLevel::Warn, L"ApplyFilePatchHooks: custom pak missing, skip: %s", absPak.c_str());
					}
				}
			}

			std::vector<std::wstring> activePatchFolders;
			for (const std::wstring& folder : krkrPatchFolders)
			{
				if (!IsNestedUnderAnyPatchFolder(folder, filePatchFolders))
				{
					AppendUniquePath(activePatchFolders, folder);
				}
			}
			for (const std::wstring& folder : filePatchFolders)
			{
				AppendUniquePath(activePatchFolders, folder);
			}
			for (const std::wstring& folder : krkrPatchFolders)
			{
				if (IsNestedUnderAnyPatchFolder(folder, filePatchFolders))
				{
					AppendUniquePath(activePatchFolders, folder);
				}
			}
			sg_activeFontPatchFolders = activePatchFolders;
				sg_activeModuleAssetPatchFolders = activePatchFolders;

			std::vector<std::wstring> activeCustomPakFiles;
			for (const std::wstring& pakFile : filePatchCustomPakFiles)
			{
				AppendUniquePath(activeCustomPakFiles, GetArchiveSpecOuterPath(pakFile));
			}
			for (const std::wstring& pakFile : krkrPatchArchives)
			{
				std::wstring outerPakFile = GetArchiveSpecOuterPath(pakFile);
				if (!IsNestedUnderAnyPatchFolder(outerPakFile, filePatchFolders))
				{
					AppendUniquePath(activeCustomPakFiles, outerPakFile);
				}
			}
			for (const std::wstring& pakFile : krkrPatchArchives)
			{
				std::wstring outerPakFile = GetArchiveSpecOuterPath(pakFile);
				if (IsNestedUnderAnyPatchFolder(outerPakFile, filePatchFolders))
				{
					AppendUniquePath(activeCustomPakFiles, outerPakFile);
				}
			}

			bool enablePatch = !activePatchFolders.empty();
			bool enableCustomPak = !activeCustomPakFiles.empty();
				sg_activeModuleAssetCustomPak = enableCustomPak;
			if (!enablePatch && !enableSpoof && !enableCustomPak && !enableRedirect)
			{
				LogMessage(LogLevel::Warn, L"ApplyFilePatchHooks: no valid patch/spoof/redirect targets, skip file api hooks");
				return;
			}

			const bool detourBatchStarted = BeginDetourBatch();
			bool hookSuccess = HookFileAPIs();
			if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyFilePatchHooks detour batch"))
			{
				hookSuccess = false;
				LogMessage(LogLevel::Warn, L"ApplyFilePatchHooks: detour batch commit failed");
			}

			std::vector<const wchar_t*> patchFolders;
			for (const std::wstring& folder : activePatchFolders)
			{
				patchFolders.push_back(folder.c_str());
			}
			Rut::HookX::SetPatchFolders(
				enablePatch ? patchFolders.data() : nullptr,
				enablePatch ? patchFolders.size() : 0,
				patchSettings.enableLog
			);

			Rut::HookX::SetSpoofRules(
				enableSpoof ? spoofFiles.data() : nullptr,
				enableSpoof ? spoofFiles.size() : 0,
				enableSpoof ? spoofDirectories.data() : nullptr,
				enableSpoof ? spoofDirectories.size() : 0,
				spoofSettings.enableLog
			);

			Rut::HookX::SetDirectoryRedirectRules(
				enableRedirect ? redirectSources.data() : nullptr,
				enableRedirect ? redirectTargets.data() : nullptr,
				enableRedirect ? redirectSources.size() : 0,
				directoryRedirectSettings.enableLog
			);

			std::vector<const wchar_t*> customPakFiles;
			for (const std::wstring& pakFile : activeCustomPakFiles)
			{
				customPakFiles.push_back(pakFile.c_str());
			}
			Rut::HookX::SetCustomPakVFS(
				enableCustomPak,
				enableCustomPak ? customPakFiles.data() : nullptr,
				enableCustomPak ? customPakFiles.size() : 0,
				patchSettings.enableLog
			);
			Rut::HookX::SetCustomPakReadMode(patchSettings.vfsMode);
			ConfigureKrkrPluginPatchTargets(
				enginePatchSettings.enableKrkrPatch,
				enginePatchSettings.krkrPatchVerboseLog,
				enginePatchSettings.krkrBootstrapBypass,
				enginePatchSettings.enableKrkrCxdecBridge,
				gameDir,
				filePatchFolders,
				filePatchCustomPakFiles,
				krkrPatchBaseNames,
				krkrPatchFolders,
				krkrPatchArchives);

			LogMessage(hookSuccess ? LogLevel::Info : LogLevel::Error, L"ApplyFilePatchHooks: patchFolders=%u/%u spoofFiles=%u spoofDirectories=%u redirectRules=%u/%u pakFiles=%u/%u customPak=%d vfsMode=%d hook=%s",
				(uint32_t)patchFolders.size(),
				(uint32_t)patchSettings.patchFolders.size(),
				(uint32_t)spoofFiles.size(),
				(uint32_t)spoofDirectories.size(),
				(uint32_t)redirectSources.size(),
				(uint32_t)directoryRedirectSettings.rules.size(),
				(uint32_t)customPakFiles.size(),
				(uint32_t)patchSettings.customPakFiles.size(),
				enableCustomPak ? 1 : 0,
				patchSettings.vfsMode,
				hookSuccess ? L"success" : L"failed");

			if (patchSettings.debugMode)
			{
				if (hookSuccess)
				{
					std::wstring msg = L"File Patch Enabled!\nPatch Folder Count: " + std::to_wstring(patchFolders.size());
					MessageBoxW(NULL, msg.c_str(), L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
				}
				else
				{
					MessageBoxW(NULL, L"File Patch Hook Failed!", L"CialloHook - Debug", MB_OK | MB_ICONERROR);
				}
			}
		}

		void ApplyRegistryBootstrap(const RegistryBootstrapSettings& settings)
			{
				sg_registryBootstrapCleanupOnExit = settings.cleanupOnExit;
				sg_registryBootstrapKeys.clear();
				sg_registryBootstrapValues.clear();
				if (!settings.enable)
				{
					CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyRegistryBootstrap: disabled");
					return;
				}

				uint32_t appliedCount = 0;
				for (const auto& rule : settings.rules)
				{
					HKEY rootKey = nullptr;
					if (!TryResolveRegistryRoot(rule.root, rootKey))
					{
						LogMessage(LogLevel::Warn, L"ApplyRegistryBootstrap: unsupported root=%s key=%s", rule.root.c_str(), rule.key.c_str());
						continue;
					}

					std::wstring keyPath = Rut::StrX::Trim(rule.key);
					if (keyPath.empty())
					{
						LogMessage(LogLevel::Warn, L"ApplyRegistryBootstrap: empty key path, skip");
						continue;
					}

					if (!HasRegistryBootstrapKeyRecord(rootKey, keyPath))
					{
						bool keyExisted = false;
						HKEY existingKey = nullptr;
						LSTATUS openStatus = RegOpenKeyExW(rootKey, keyPath.c_str(), 0, KEY_READ, &existingKey);
						if (openStatus == ERROR_SUCCESS)
						{
							keyExisted = true;
							RegCloseKey(existingKey);
						}
						sg_registryBootstrapKeys.push_back({ rootKey, keyPath, keyExisted });
					}

					HKEY targetKey = nullptr;
					DWORD disposition = 0;
					LSTATUS createStatus = RegCreateKeyExW(rootKey, keyPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &targetKey, &disposition);
					if (createStatus != ERROR_SUCCESS || targetKey == nullptr)
					{
						LogMessage(LogLevel::Error, L"ApplyRegistryBootstrap: create key failed root=%s key=%s error=%ld", rule.root.c_str(), keyPath.c_str(), createStatus);
						continue;
					}

					std::wstring valueName = rule.valueName;
					if (!HasRegistryBootstrapValueRecord(rootKey, keyPath, valueName))
					{
						RegistryBootstrapValueRecord valueRecord = {};
						valueRecord.root = rootKey;
						valueRecord.keyPath = keyPath;
						valueRecord.valueName = valueName;
						DWORD originalType = REG_NONE;
						DWORD dataSize = 0;
						LSTATUS queryStatus = RegQueryValueExW(targetKey, valueName.empty() ? nullptr : valueName.c_str(), nullptr, &originalType, nullptr, &dataSize);
						if (queryStatus == ERROR_SUCCESS)
						{
							valueRecord.valueExisted = true;
							valueRecord.originalType = originalType;
							valueRecord.originalData.resize(dataSize);
							if (dataSize != 0)
							{
								RegQueryValueExW(targetKey, valueName.empty() ? nullptr : valueName.c_str(), nullptr, &valueRecord.originalType, valueRecord.originalData.data(), &dataSize);
							}
						}
						sg_registryBootstrapValues.push_back(std::move(valueRecord));
					}

					std::wstring typeName = Rut::StrX::Trim(rule.type);
					LSTATUS setStatus = ERROR_INVALID_DATA;
					if (typeName.empty() || EqualsInsensitive(typeName, L"SZ") || EqualsInsensitive(typeName, L"STRING") || EqualsInsensitive(typeName, L"REG_SZ"))
					{
						const BYTE* dataPtr = reinterpret_cast<const BYTE*>(rule.data.c_str());
						DWORD dataBytes = static_cast<DWORD>((rule.data.size() + 1) * sizeof(wchar_t));
						setStatus = RegSetValueExW(targetKey, valueName.empty() ? nullptr : valueName.c_str(), 0, REG_SZ, dataPtr, dataBytes);
					}
					else if (EqualsInsensitive(typeName, L"DWORD") || EqualsInsensitive(typeName, L"REG_DWORD"))
					{
						DWORD dwordValue = 0;
						if (TryParseRegistryDword(rule.data, dwordValue))
						{
							setStatus = RegSetValueExW(targetKey, valueName.empty() ? nullptr : valueName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwordValue), sizeof(dwordValue));
						}
						else
						{
							LogMessage(LogLevel::Warn, L"ApplyRegistryBootstrap: invalid DWORD data root=%s key=%s value=%s data=%s", rule.root.c_str(), keyPath.c_str(), valueName.c_str(), rule.data.c_str());
						}
					}
					else
					{
						LogMessage(LogLevel::Warn, L"ApplyRegistryBootstrap: unsupported type=%s root=%s key=%s value=%s", typeName.c_str(), rule.root.c_str(), keyPath.c_str(), valueName.c_str());
					}

					RegCloseKey(targetKey);
					if (setStatus != ERROR_SUCCESS)
					{
						LogMessage(LogLevel::Error, L"ApplyRegistryBootstrap: set value failed root=%s key=%s value=%s error=%ld", rule.root.c_str(), keyPath.c_str(), valueName.c_str(), setStatus);
						continue;
					}

					++appliedCount;
					if (settings.enableLog)
					{
						LogMessage(LogLevel::Info, L"ApplyRegistryBootstrap: applied root=%s key=%s value=%s type=%s", rule.root.c_str(), keyPath.c_str(), valueName.c_str(), typeName.c_str());
					}
				}

				LogMessage(LogLevel::Info, L"ApplyRegistryBootstrap: rules=%u applied=%u cleanupOnExit=%d", static_cast<uint32_t>(settings.rules.size()), appliedCount, settings.cleanupOnExit ? 1 : 0);
			}

			void CleanupRegistryBootstrap()
			{
				if (sg_registryBootstrapKeys.empty() && sg_registryBootstrapValues.empty())
				{
					return;
				}
				if (!sg_registryBootstrapCleanupOnExit)
				{
					sg_registryBootstrapKeys.clear();
					sg_registryBootstrapValues.clear();
					return;
				}

				for (auto it = sg_registryBootstrapValues.rbegin(); it != sg_registryBootstrapValues.rend(); ++it)
				{
					HKEY key = nullptr;
					if (RegOpenKeyExW(it->root, it->keyPath.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS || key == nullptr)
					{
						continue;
					}
					if (it->valueExisted)
					{
						const BYTE* dataPtr = it->originalData.empty() ? nullptr : it->originalData.data();
						DWORD dataBytes = static_cast<DWORD>(it->originalData.size());
						RegSetValueExW(key, it->valueName.empty() ? nullptr : it->valueName.c_str(), 0, it->originalType, dataPtr, dataBytes);
					}
					else
					{
						RegDeleteValueW(key, it->valueName.empty() ? nullptr : it->valueName.c_str());
					}
					RegCloseKey(key);
				}

				for (auto it = sg_registryBootstrapKeys.rbegin(); it != sg_registryBootstrapKeys.rend(); ++it)
				{
					if (!it->keyExisted)
					{
						RegDeleteTreeW(it->root, it->keyPath.c_str());
					}
				}

				sg_registryBootstrapKeys.clear();
				sg_registryBootstrapValues.clear();
			}

			void ApplyRegistryHooks(const RegistrySettings& settings)
		{
			if (!settings.enable)
			{
				CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyRegistryHooks: disabled");
				return;
			}

			std::vector<std::wstring> registryFilePaths;
			registryFilePaths.reserve(settings.files.size());
			for (const auto& file : settings.files)
			{
				ModuleAssetResolution registryFile = {};
				if (!TryResolveModuleAssetDiskPath(file, registryFile))
				{
					LogMessage(LogLevel::Error, L"ApplyRegistryHooks: registry file missing: %s", file.c_str());
					return;
				}
				LogMessage(LogLevel::Info, L"ApplyRegistryHooks: registry file=%s source=%s resolved=%s",
					file.c_str(), registryFile.source.c_str(), registryFile.resolvedPath.c_str());
				registryFilePaths.push_back(std::move(registryFile.resolvedPath));
			}

			std::vector<const wchar_t*> registryFilePathPointers;
			registryFilePathPointers.reserve(registryFilePaths.size());
			for (const auto& path : registryFilePaths)
			{
				registryFilePathPointers.push_back(path.c_str());
			}

			bool loaded = Rut::HookX::LoadVirtualRegistryFiles(registryFilePathPointers.data(), registryFilePathPointers.size(), settings.enableLog);
			const bool detourBatchStarted = loaded && BeginDetourBatch();
			bool hooked = loaded ? Rut::HookX::HookRegistryAPIs() : false;
			if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyRegistryHooks detour batch"))
			{
				hooked = false;
				LogMessage(LogLevel::Warn, L"ApplyRegistryHooks: detour batch commit failed");
			}
			LogMessage((loaded && hooked) ? LogLevel::Info : LogLevel::Error,
				L"ApplyRegistryHooks: files=%u load=%s hook=%s",
				static_cast<uint32_t>(registryFilePaths.size()),
				loaded ? L"success" : L"failed",
				hooked ? L"success" : L"failed");
		}

		void ApplyCodePageHooks(const CodePageSettings& settings)
			{
				if (!settings.enable)
				{
					CIALLOHOOK_VERBOSE_INFO_LOG(L"ApplyCodePageHooks: disabled");
					return;
				}

				if (!settings.hookMultiByteToWideChar && !settings.hookWideCharToMultiByte)
				{
					LogMessage(LogLevel::Warn, L"ApplyCodePageHooks: enabled but no API hooks selected");
					return;
				}

				Rut::HookX::SetCodePageMapping(settings.fromCodePage, settings.toCodePage);
				const bool detourBatchStarted = BeginDetourBatch();
				uint32_t enabledCount = 0;
				uint32_t successCount = 0;
				uint32_t failCount = 0;
				std::wstring failedApis;
				auto hookAndLog = [&](const wchar_t* apiName, bool enabled, bool(*hookFunc)())
				{
					if (!enabled)
					{
						return;
					}
					++enabledCount;
					if (hookFunc())
					{
						++successCount;
					}
					else
					{
						++failCount;
						if (!failedApis.empty())
						{
							failedApis += L", ";
						}
						failedApis += apiName;
					}
				};
				hookAndLog(L"MultiByteToWideChar", settings.hookMultiByteToWideChar, Rut::HookX::HookMultiByteToWideChar);
				hookAndLog(L"WideCharToMultiByte", settings.hookWideCharToMultiByte, Rut::HookX::HookWideCharToMultiByte);
				if (!CommitDetourBatchIfStarted(detourBatchStarted, L"ApplyCodePageHooks detour batch"))
				{
					successCount = 0;
					failCount = enabledCount;
					if (!failedApis.empty())
					{
						failedApis += L", ";
					}
					failedApis += L"Detour batch commit";
					LogMessage(LogLevel::Warn, L"ApplyCodePageHooks: detour batch commit failed");
				}
				LogMessage(failCount == 0 ? LogLevel::Info : LogLevel::Error,
					L"ApplyCodePageHooks: %u -> %u enabled=%u ok=%u failed=%u",
					settings.fromCodePage,
					settings.toCodePage,
					enabledCount,
					successCount,
					failCount);
				if (!failedApis.empty())
				{
					LogMessage(LogLevel::Warn, L"ApplyCodePageHooks: failed apis=%s", failedApis.c_str());
				}
			}
	}
}
