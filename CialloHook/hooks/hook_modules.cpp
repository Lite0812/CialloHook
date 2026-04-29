#include "hook_modules.h"
#include "krkr_plugin_bridge.h"

#include <Windows.h>
#include <string>
#include <cwctype>
#include <vector>
#include <memory>

#include "../../RuntimeCore/io/File.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../RuntimeCore/hook/Hook_API.h"

using namespace Rut::FileX;
using namespace Rut::HookX;

namespace CialloHook
{
	namespace HookModules
	{
		struct ResolvedFontRedirectRule
		{
			std::wstring sourceFont;
			std::wstring targetFont;
		};

		static std::vector<std::wstring> sg_loadedTempFontFiles;

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

		static bool TryLoadFontFromCustomPakPath(const std::wstring& path, bool& foundCandidate)
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

			if (LoadFontFromFile(cachePath.c_str(), false))
			{
				TrackLoadedTempFontFile(cachePath);
				return true;
			}
			LogMessage(LogLevel::Error, L"Font CustomPak extraction failed: AddFontResourceExW rejected %s", cachePath.c_str());
			return false;
		}

		static bool TryLoadFontFilePreferLocal(const std::wstring& fontPathOrFileName)
		{
			bool localFileExists = IsRegularFilePath(fontPathOrFileName);
			if (localFileExists && LoadFontFromFile(fontPathOrFileName.c_str(), false))
			{
				return true;
			}
			bool customPakFound = false;
			if (TryLoadFontFromCustomPakPath(fontPathOrFileName, customPakFound))
			{
				return true;
			}
			if (!HasPathSeparator(fontPathOrFileName))
			{
				bool nestedCustomPakFound = false;
				if (TryLoadFontFromCustomPakPath(L"fonts\\" + fontPathOrFileName, nestedCustomPakFound))
				{
					return true;
				}
				customPakFound = customPakFound || nestedCustomPakFound;
				nestedCustomPakFound = false;
				if (TryLoadFontFromCustomPakPath(L"font\\" + fontPathOrFileName, nestedCustomPakFound))
				{
					return true;
				}
				customPakFound = customPakFound || nestedCustomPakFound;
			}
			if (localFileExists || customPakFound)
			{
				std::wstring errMsg = std::wstring(L"Failed to load font file: ") + fontPathOrFileName;
				MessageBoxW(NULL, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
			}
			else
			{
				std::wstring errMsg = std::wstring(L"Font file not found: ") + fontPathOrFileName;
				MessageBoxW(NULL, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
			}
			return false;
		}

		static std::wstring ResolveConfiguredFontName(const std::wstring& fontConfig, const std::wstring& fontNameOverride)
		{
			if (fontConfig.empty())
			{
				return fontConfig;
			}

			std::wstring ext = GetLowerExtension(fontConfig);
			if (!IsSupportedFontFileExtension(ext))
			{
				return fontConfig;
			}

			if (!TryLoadFontFilePreferLocal(fontConfig))
			{
				return L"SimSun";
			}

			if (!fontNameOverride.empty())
			{
				return fontNameOverride;
			}

			std::wstring fileName = PathRemoveExtension(fontConfig);
			size_t pos = fileName.find_last_of(L"\\/");
			return (pos == std::wstring::npos) ? fileName : fileName.substr(pos + 1);
		}

		static std::wstring ResolveFontName(const FontSettings& settings)
		{
			return ResolveConfiguredFontName(settings.font, settings.fontNameOverride);
		}

		void ApplyFontHooks(const FontSettings& settings)
		{
			const bool hasFontOverride = !IsBlankText(settings.font);
			if (!hasFontOverride && !settings.enableCnJpMap)
			{
				LogMessage(LogLevel::Info, L"ApplyFontHooks: Font is empty and cn-jp map disabled, skip font hooks");
				return;
			}

			EnableCnJpMap(false);
			EnableCnJpMapVerboseLog(settings.cnJpMapVerboseLog);
			SetCnJpMapEncoding(settings.cnJpMapReadEncoding);
			if (settings.enableCnJpMap)
			{
				std::wstring gameDir = GetGameDirectory();
				std::wstring mapJsonPath = ToGameAbsolutePath(gameDir, settings.cnJpMapJson);
				LogMessage(LogLevel::Info, L"ApplyFontHooks: cn-jp map requested path=%s", mapJsonPath.c_str());
				if (!IsExistingRegularFile(mapJsonPath))
				{
					LogMessage(LogLevel::Error, L"ApplyFontHooks: cn-jp map json missing: %s", mapJsonPath.c_str());
				}
				else
				{
					bool loaded = LoadCnJpMapFile(mapJsonPath.c_str());
					EnableCnJpMap(loaded);
					LogMessage(loaded ? LogLevel::Info : LogLevel::Error,
						L"ApplyFontHooks: cn-jp map file=%s load=%s",
						mapJsonPath.c_str(),
						loaded ? L"success" : L"failed");
				}
			}

			std::wstring fontName = ResolveFontName(settings);
			std::vector<ResolvedFontRedirectRule> resolvedRedirectRules;
			resolvedRedirectRules.reserve(settings.redirectRules.size());
			for (const FontRedirectRule& rule : settings.redirectRules)
			{
				ResolvedFontRedirectRule resolvedRule;
				resolvedRule.sourceFont = TrimWide(rule.sourceFont);
				resolvedRule.targetFont = ResolveConfiguredFontName(rule.targetFont, rule.targetFontNameOverride);
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
			LogMessage(LogLevel::Info, L"ApplyFontHooks: skipRules=%u redirectRules=%u",
				(uint32_t)skipFontNames.size(),
				(uint32_t)resolvedRedirectRules.size());
			for (size_t i = 0; i < skipFontNames.size(); ++i)
			{
				LogMessage(LogLevel::Info, L"ApplyFontHooks: skipFont[%u]=%s", (uint32_t)i, skipFontNames[i]);
			}
			for (size_t i = 0; i < resolvedRedirectRules.size(); ++i)
			{
				LogMessage(LogLevel::Info, L"ApplyFontHooks: redirectFont[%u] %s -> %s",
					(uint32_t)i,
					resolvedRedirectRules[i].sourceFont.c_str(),
					resolvedRedirectRules[i].targetFont.c_str());
			}
			LogMessage(LogLevel::Info, L"ApplyFontHooks: glyphOffset=(%d,%d) metricsOffset=(L%d,R%d,T%d,B%d)",
				settings.glyphOffsetX, settings.glyphOffsetY,
				settings.metricsOffsetLeft, settings.metricsOffsetRight, settings.metricsOffsetTop, settings.metricsOffsetBottom);
			LogMessage(LogLevel::Info, L"ApplyFontHooks: GDI+ hooks draw=%d drawDriver=%d measure=%d measureRanges=%d measureDriver=%d lateLoad=%d",
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
			if (attachOkCount || attachFailCount)
			{
				LogMessage(attachFailCount ? LogLevel::Warn : LogLevel::Info,
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
			LogMessage(LogLevel::Info, L"ApplyTextHooks: rule count=%u, readEncoding=%u, writeEncoding=%u, verbose=%d, waffleGetTextCrash=%d",
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
				LogMessage(LogLevel::Info, L"ApplyTextHooks: rule[%u] \"%s\" -> \"%s\"",
					(uint32_t)i, rule.first.c_str(), rule.second.c_str());
			}

			uint32_t successCount = 0;
			uint32_t failCount = 0;
			uint32_t disabledCount = 0;
			uint32_t enabledCount = 0;
			std::wstring failedApis;
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
				LogMessage(LogLevel::Info, L"ApplyWindowTitleHooks: no rules");
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

			HookWindowTitleAPIs(settings.titleMode);
			LogMessage(LogLevel::Info, L"ApplyWindowTitleHooks: rule count=%u, mode=%d, readEncoding=%u, writeEncoding=%u, verbose=%d",
				(uint32_t)settings.rules.size(), settings.titleMode, titleReadCp, titleWriteCp, settings.enableVerboseLog ? 1 : 0);
		}

		void ApplyPreStartupHooks(const AppSettings& settings)
		{
			if (settings.startupMessage.enable)
			{
				LogMessage(LogLevel::Info, L"Apply hooks: startup window gate");
				UINT titleReadCp = settings.windowTitle.readEncoding != 0
					? (UINT)settings.windowTitle.readEncoding
					: (settings.windowTitle.encoding == 0 ? CP_ACP : (UINT)settings.windowTitle.encoding);
				UINT titleWriteCp = settings.windowTitle.writeEncoding != 0 ? (UINT)settings.windowTitle.writeEncoding : titleReadCp;
				SetWindowTitleEncodings(titleReadCp, titleWriteCp);
				EnableWindowTitleVerboseLog(settings.windowTitle.enableVerboseLog);
				EnableStartupWindowGate(true, GetCurrentThreadId());
				HookWindowTitleAPIs(settings.windowTitle.titleMode);
			}
			else
			{
				LogMessage(LogLevel::Info, L"Apply hooks: startup window gate skipped");
			}
		}

		void ApplyPostStartupHooks(const AppSettings& settings)
		{
			LogMessage(LogLevel::Info, L"Apply hooks: file patch");
			ApplyFilePatchHooks(settings.filePatch, settings.fileSpoof, settings.directoryRedirect, settings.enginePatches);

			LogMessage(LogLevel::Info, L"Apply hooks: siglus key extract");
			ApplySiglusKeyExtract(settings.siglusKeyExtract);

			LogMessage(LogLevel::Info, L"Apply hooks: text");
			ApplyTextHooks(settings.textReplace, settings.enginePatches);

			LogMessage(LogLevel::Info, L"Apply hooks: window title");
			ApplyWindowTitleHooks(settings.windowTitle);

			LogMessage(LogLevel::Info, L"Apply hooks: registry");
			ApplyRegistryHooks(settings.registry);

			LogMessage(LogLevel::Info, L"Apply hooks: code page");
			ApplyCodePageHooks(settings.codePage);

			LogMessage(LogLevel::Info, L"Apply hooks: font");
			ApplyFontHooks(settings.font);
			ReleaseStartupWindowGate();
		}

		void ApplySiglusKeyExtract(const SiglusKeyExtractSettings& settings)
		{
			if (!settings.enable)
			{
				LogMessage(LogLevel::Info, L"ApplySiglusKeyExtract: disabled");
				return;
			}

			std::wstring gameDir = GetGameDirectory();
			std::wstring gameexePath = ToGameAbsolutePath(gameDir, settings.gameexePath);
			std::wstring keyOutputPath = ToGameAbsolutePath(gameDir, settings.keyOutputPath);
			SetKeyExtractConfig(gameexePath.c_str(), keyOutputPath.c_str(), settings.showMessageBox);

			bool keyExtractEnabled = EnableSiglusKeyExtract();
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
			if (!patchSettings.enable && !spoofSettings.enable && !directoryRedirectSettings.enable && !enginePatchSettings.enableKrkrPatch)
			{
				LogMessage(LogLevel::Info, L"ApplyFilePatchHooks: disabled (patch, spoof, redirect and krkrpatch)");
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
			if (!enablePatch && !enableSpoof && !enableCustomPak && !enableRedirect)
			{
				LogMessage(LogLevel::Warn, L"ApplyFilePatchHooks: no valid patch/spoof/redirect targets, skip file api hooks");
				return;
			}

			bool hookSuccess = HookFileAPIs();

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

		void ApplyRegistryHooks(const RegistrySettings& settings)
		{
			if (!settings.enable)
			{
				LogMessage(LogLevel::Info, L"ApplyRegistryHooks: disabled");
				return;
			}

			std::wstring gameDir = GetGameDirectory();
			std::vector<std::wstring> registryFilePaths;
			registryFilePaths.reserve(settings.files.size());
			for (const auto& file : settings.files)
			{
				std::wstring registryFilePath = ToGameAbsolutePath(gameDir, file);
				if (!IsExistingRegularFile(registryFilePath))
				{
					LogMessage(LogLevel::Error, L"ApplyRegistryHooks: registry file missing: %s", registryFilePath.c_str());
					return;
				}
				registryFilePaths.push_back(std::move(registryFilePath));
			}

			std::vector<const wchar_t*> registryFilePathPointers;
			registryFilePathPointers.reserve(registryFilePaths.size());
			for (const auto& path : registryFilePaths)
			{
				registryFilePathPointers.push_back(path.c_str());
			}

			bool loaded = Rut::HookX::LoadVirtualRegistryFiles(registryFilePathPointers.data(), registryFilePathPointers.size(), settings.enableLog);
			bool hooked = loaded ? Rut::HookX::HookRegistryAPIs() : false;
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
				LogMessage(LogLevel::Info, L"ApplyCodePageHooks: disabled");
				return;
			}

			Rut::HookX::SetCodePageMapping(settings.fromCodePage, settings.toCodePage);
			bool hookSuccess = HookCodePageAPIs();
			LogMessage(hookSuccess ? LogLevel::Info : LogLevel::Error, L"ApplyCodePageHooks: %u -> %u (%s)",
				settings.fromCodePage, settings.toCodePage, hookSuccess ? L"success" : L"failed");
		}
	}
}
