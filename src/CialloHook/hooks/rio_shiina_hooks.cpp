#include "rio_shiina_hooks.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../RuntimeCore/base/Mem.h"
#include "../../RuntimeCore/base/Str.h"
#include "../../RuntimeCore/hook/Hook.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"

namespace fs = std::filesystem;

using namespace Rut::HookX;

namespace CialloHook
{
	namespace RioShiinaHooks
	{
		namespace
		{
#if defined(_M_IX86)
			using RioGetResFn = void* (__cdecl*)(const char* lpFileName, uint32_t* pBytesRead);
			using RioReadFileFn = void* (__cdecl*)(const char* currentWorkDir, const char* logicFileName, uint32_t* pBytesRead);
			using FindAndOpenFileInWarcFn = HANDLE(__cdecl*)(const char* warcName, char* pureFileName, DWORD dwDesiredAccess, void* a4Zero, void* a5Zero, void* a6Zero, char** pPureFileName);
			using RegOpenKeyExAFn = LSTATUS(WINAPI*)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);

			constexpr std::string_view kRioGetResSign = "81 EC 04 01 00 00 53 55 56";
			constexpr std::string_view kRioReadFileSign = "81 EC 04 01 00 00 53 55 8B AC 24 10 01 00 00 8A 45 00";
			constexpr std::string_view kPushWarcStrSign = "6A 06 8D ?? ?? 68 ?? ?? ?? ?? 5? E8";
			constexpr std::string_view kAfterDecompressCallSign = "E8 ?? ?? ?? ?? 83 C4 14";
			constexpr std::string_view kGetDvdDrivePathRetSign = "C6 00 00 33 C0 5B C3";
			constexpr DWORD kRegInstallDelayMs = 40;

			RioShiinaSettings sg_settings;
			bool sg_installed = false;
			bool sg_efclibLoaded = false;
			bool sg_extractCompleted = false;
			std::optional<size_t> sg_entrySize;
			std::wstring sg_gameDir;
			RioGetResFn sg_rawRioGetRes = nullptr;
			RioReadFileFn sg_rawRioReadFile = nullptr;
				FindAndOpenFileInWarcFn sg_rawFindAndOpenFileInWarc = nullptr;
				RegOpenKeyExAFn sg_rawRegOpenKeyExA = RegOpenKeyExA;
				void* sg_afterDecompressHookTarget = nullptr;
				void* sg_afterDecompressResume = nullptr;
				JumpPatchHandle sg_afterDecompressPatch;
				void* sg_dvdCompareTarget = nullptr;
				void* sg_dvdCompareBranchTarget = nullptr;
				void* sg_dvdCompareFallthrough = nullptr;
				HANDLE sg_runtimeInstallEvent = nullptr;
				bool sg_dvdSyntheticRuleInstalled = false;
				bool sg_loggedFirstRegOpenKeyExA = false;
				bool sg_loggedFirstDvdCompare = false;
				bool sg_loggedFirstFindAndOpenFileInWarc = false;
				bool sg_loggedFirstAfterDecompress = false;
				bool sg_mode2CaptureEnabled = false;
				bool sg_mode2PrimingDone = false;
				uint32_t sg_mode2RioGetResLogCount = 0;
				PVOID sg_mode2ExtractExceptionHandler = nullptr;
				volatile DWORD sg_mode2ExtractThreadId = 0;
				volatile LONG sg_mode2ExtractExceptionSerial = 0;
				volatile LONG sg_runtimeInstallRequested = 0;
				volatile LONG sg_runtimeInstallInProgress = 0;
				volatile LONG sg_runtimeWorkerStarted = 0;
				volatile LONG sg_runtimeHooksInstalled = 0;
				volatile LONG sg_modeRuntimeInstalled = 0;
				volatile LONG sg_processDvdInstalled = 0;
				volatile LONG sg_regObserved = 0;
				volatile LONG sg_regInstallDelayElapsed = 0;
				volatile LONG sg_findAndOpenDetached = 0;
				volatile LONG sg_processRegDetached = 0;
				bool sg_patchSourcesSuspended = false;
				FilePatchSettings sg_filePatchSettings;
				std::vector<std::wstring> sg_patchRootFolders;
				std::vector<std::wstring> sg_patchCustomPakFiles;
				fs::path sg_currentArchivePath;
				std::unordered_set<fs::path> sg_archivesToExtract;
				std::unordered_map<fs::path, std::unordered_set<std::string>> sg_fileNamesInArchives;

				struct PatternByte
				{
					uint8_t data = 0;
					uint8_t mask = 0;
				};

				static std::optional<uint32_t> ReadU32(const uint8_t* address);
				static void* FindPattern(const void* startAddress, size_t memorySize, std::string_view pattern);
				static bool AttachDetour(void** target, void* detour);
				static void* CallRioGetRes(RioGetResFn function, const char* fileName, uint32_t* pBytesRead);
				static bool InstallProcessDvd();
				static void InstallMode1();
				static void InstallMode2();
				static void InstallDeferredRuntimeHooksIfNeeded();
				static void InitializePatchSources(const FilePatchSettings& filePatchSettings);
				static void SuspendPatchSourcesForExtraction();
				static void RestorePatchSourcesAfterExtraction();
				static bool DetachDetourIfNeeded(void** target, void* detour, volatile LONG& detachedFlag, const wchar_t* operationName);
				static bool StartRioRuntimeInstallWorker();
				static bool ShouldInstallRuntimeHooksNow();
				static void SignalRuntimeInstallWorker(bool allowInlineFallback);
				static void MarkRuntimeInstallRequested(const wchar_t* reason, bool allowInlineFallback);
				static HANDLE __cdecl HookFindAndOpenFileInWarc(const char* warcName, char* pureFileName, DWORD dwDesiredAccess, void* a4Zero, void* a5Zero, void* a6Zero, char** pPureFileName);
				static LONG NTAPI RioMode2ExtractExceptionHandler(PEXCEPTION_POINTERS exceptionInfo);
				static DWORD WINAPI RioRuntimeInstallWorkerThread(LPVOID);

				static std::wstring GetGameDirectory()
				{
				wchar_t modulePath[MAX_PATH] = {};
				DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(_countof(modulePath)));
				if (length == 0 || length >= _countof(modulePath))
				{
					return L".";
				}
				fs::path path(modulePath);
				return path.parent_path().wstring();
			}

			static std::wstring ToAbsoluteGamePath(const std::wstring& inputPath)
			{
				fs::path path(inputPath);
				if (path.is_absolute())
				{
					return path.wstring();
				}
				return (fs::path(sg_gameDir) / path).wstring();
			}

			static HMODULE GetMainModule()
			{
				return GetModuleHandleW(nullptr);
			}

			static size_t GetMainModuleSize()
			{
				auto* base = reinterpret_cast<const uint8_t*>(GetMainModule());
				if (!base)
				{
					return 0;
				}
				auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
				if (dos->e_magic != IMAGE_DOS_SIGNATURE)
				{
					return 0;
				}
				auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
				if (nt->Signature != IMAGE_NT_SIGNATURE)
				{
					return 0;
				}
				return nt->OptionalHeader.SizeOfImage;
			}

			static std::wstring SjisToWide(const std::string& text)
			{
				if (text.empty())
				{
					return {};
				}
				int length = MultiByteToWideChar(932, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
				if (length <= 0)
				{
					return {};
				}
				std::wstring result(length, L'\0');
				MultiByteToWideChar(932, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
				return result;
			}

			static std::wstring SjisToWide(const char* text)
			{
				return text ? SjisToWide(std::string(text)) : std::wstring();
			}

			static std::string WideToAnsi(const std::wstring& text)
			{
				if (text.empty())
				{
					return {};
				}
				UINT codePage = GetACP();
				int length = WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
				if (length <= 0)
				{
					return {};
				}
				std::string result(length, '\0');
				WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
				return result;
			}

			static std::string ToLowerAscii(std::string text)
			{
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
					{
						return static_cast<char>(std::tolower(c));
					});
				return text;
			}

			static bool EndsWith(const std::string& value, const char* suffix)
			{
				if (!suffix)
				{
					return false;
				}
				size_t suffixLength = std::strlen(suffix);
				return value.size() >= suffixLength && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
			}

			static bool StartsWith(std::string_view value, const char* prefix)
			{
				if (!prefix)
				{
					return false;
				}
				size_t prefixLength = std::strlen(prefix);
				return value.size() >= prefixLength && value.compare(0, prefixLength, prefix) == 0;
			}

			static bool CommitDetourBatchIfStarted(bool batchStarted, const wchar_t* operationName)
			{
				return !batchStarted || EndDetourBatch(operationName);
			}

			static bool StartsWith(const std::wstring& value, const wchar_t* prefix)
				{
					if (!prefix)
					{
						return false;
					}
					size_t prefixLength = wcslen(prefix);
					return value.size() >= prefixLength && value.compare(0, prefixLength, prefix) == 0;
				}

				static bool SetRegistryStringValue(HKEY key, const wchar_t* valueName, const wchar_t* value)
				{
					const wchar_t* safeValue = value ? value : L"";
					DWORD size = static_cast<DWORD>((wcslen(safeValue) + 1) * sizeof(wchar_t));
					return RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(safeValue), size) == ERROR_SUCCESS;
				}

				static bool WriteRioRegistryBootstrap(const std::wstring& regPath)
				{
					if (regPath.empty())
					{
						return false;
					}
					HKEY key = nullptr;
					DWORD disposition = 0;
					LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
					if (status != ERROR_SUCCESS || !key)
					{
						LogMessage(LogLevel::Warn, L"RioShiina: RegCreateKeyExW failed for %s (error=%ld)", regPath.c_str(), status);
						return false;
					}
					DWORD instMode = 0;
					bool ok = SetRegistryStringValue(key, L"DataPath", L".\\savedata\\")
						&& SetRegistryStringValue(key, L"InstPath", L".\\")
						&& RegSetValueExW(key, L"InstMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&instMode), sizeof(instMode)) == ERROR_SUCCESS;
					RegCloseKey(key);
					if (!ok)
					{
						LogMessage(LogLevel::Warn, L"RioShiina: failed to write bootstrap registry values for %s", regPath.c_str());
						return false;
					}
					std::error_code ec;
					fs::create_directories(fs::path(sg_gameDir) / L"savedata", ec);
					return true;
				}

				static bool TryDiscoverRioRegistryPath(const fs::path& iniPath, std::wstring& regPathOut)
				{
					regPathOut.clear();
					std::string iniPathA = WideToAnsi(iniPath.wstring());
					if (iniPathA.empty())
					{
						return false;
					}
					std::vector<char> sections(65536, '\0');
					DWORD sectionLength = GetPrivateProfileSectionNamesA(sections.data(), static_cast<DWORD>(sections.size()), iniPathA.c_str());
					if (sectionLength == 0)
					{
						return false;
					}
					for (const char* section = sections.data(); *section != '\0'; section += std::strlen(section) + 1)
					{
						std::wstring sectionName = SjisToWide(section);
						if (!StartsWith(sectionName, L"椎名里緒"))
						{
							continue;
						}
						std::vector<char> regValue(4096, '\0');
						DWORD valueLength = GetPrivateProfileStringA(section, "Reg", "", regValue.data(), static_cast<DWORD>(regValue.size()), iniPathA.c_str());
						if (valueLength == 0)
						{
							break;
						}
						regPathOut = SjisToWide(std::string(regValue.data(), valueLength));
						return !regPathOut.empty();
					}
					return false;
				}

				static LSTATUS WINAPI HookRegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
					{
						if (!lpSubKey)
						{
							return sg_rawRegOpenKeyExA ? sg_rawRegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult) : ERROR_INVALID_PARAMETER;
						}
						std::wstring subKey = SjisToWide(lpSubKey);
						if (!sg_loggedFirstRegOpenKeyExA)
						{
							sg_loggedFirstRegOpenKeyExA = true;
							LogMessage(LogLevel::Info, L"RioShiina: first RegOpenKeyExA subKey=%s", subKey.c_str());
						}
						InterlockedExchange(&sg_regObserved, 1);
						MarkRuntimeInstallRequested(L"RegOpenKeyExA", false);
						return RegOpenKeyExW(hKey, subKey.c_str(), ulOptions, samDesired, phkResult);
					}

					static bool InstallProcessReg()
				{
					if (!sg_settings.processReg)
					{
						return true;
					}
					std::error_code ec;
					for (const auto& entry : fs::directory_iterator(fs::path(sg_gameDir), ec))
					{
						if (ec)
						{
							break;
						}
						if (!entry.is_regular_file())
						{
							continue;
						}
						std::wstring fileNameLower = Rut::StrX::Trim(entry.path().filename().wstring());
						std::transform(fileNameLower.begin(), fileNameLower.end(), fileNameLower.begin(), towlower);
						if (fileNameLower.size() < 4 || fileNameLower.compare(fileNameLower.size() - 4, 4, L".ini") != 0 || (fileNameLower.size() >= 8 && fileNameLower.compare(fileNameLower.size() - 8, 8, L".jlx.ini") == 0))
						{
							continue;
						}
						std::wstring regPath;
						if (!TryDiscoverRioRegistryPath(entry.path(), regPath))
						{
							continue;
						}
						WriteRioRegistryBootstrap(regPath);
						break;
					}
					bool batchStarted = BeginDetourBatch();
					bool failed = !AttachDetour(reinterpret_cast<void**>(&sg_rawRegOpenKeyExA), reinterpret_cast<void*>(HookRegOpenKeyExA));
					if (!CommitDetourBatchIfStarted(batchStarted, L"RioShiina ProcessReg detours"))
					{
						failed = true;
					}
					LogMessage(failed ? LogLevel::Warn : LogLevel::Info, L"RioShiina: ProcessReg=%d RegOpenKeyExA=%p", failed ? 0 : 1, sg_rawRegOpenKeyExA);
					return !failed;
				}

				static bool InstallInstructionJumpHook(void* target, void* detour, size_t patchSize)
				{
					if (!target || !detour || patchSize < 5)
					{
						return false;
					}
					auto* targetBytes = static_cast<uint8_t*>(target);
					DWORD oldProtect = 0;
					if (!VirtualProtect(targetBytes, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
					{
						return false;
					}
					memset(targetBytes, 0x90, patchSize);
					targetBytes[0] = 0xE9;
					int32_t detourRelative = static_cast<int32_t>(static_cast<uint8_t*>(detour) - (targetBytes + 5));
					memcpy(targetBytes + 1, &detourRelative, sizeof(detourRelative));
					FlushInstructionCache(GetCurrentProcess(), targetBytes, patchSize);
					DWORD ignoreProtect = 0;
					VirtualProtect(targetBytes, patchSize, oldProtect, &ignoreProtect);
					return true;
				}

				static void __cdecl DvdCompareBridge(uint32_t* savedEbx)
				{
					if (!savedEbx)
					{
						return;
					}
					uint8_t driveLetter = static_cast<uint8_t>(*savedEbx & 0xFFu);
					if (!sg_loggedFirstDvdCompare)
					{
						sg_loggedFirstDvdCompare = true;
						LogMessage(LogLevel::Info, L"RioShiina: first DvdCompare driveLetter=0x%02X(%c)", driveLetter, driveLetter >= 0x20 && driveLetter <= 0x7E ? driveLetter : L'?');
					}
					if (driveLetter <= static_cast<uint8_t>('Z'))
					{
						return;
					}
					*savedEbx = (*savedEbx & 0xFFFFFF00u) | static_cast<uint8_t>('W');
					if (sg_settings.specDvdFileSize != 0 && !sg_dvdSyntheticRuleInstalled)
					{
						SetSyntheticFilePrefixSizeRule(L"W:\\", sg_settings.specDvdFileSize, sg_settings.enableLog);
						sg_dvdSyntheticRuleInstalled = true;
					}
				}

				__declspec(naked) static void DvdCompareDetour()
				{
					__asm
					{
						pushfd
						pushad
						lea eax, [esp + 16]
						push eax
						call DvdCompareBridge
						add esp, 4
						popad
						popfd
						cmp bl, 5Ah
						jle branch_target
						jmp dword ptr [sg_dvdCompareFallthrough]
					branch_target:
						jmp dword ptr [sg_dvdCompareBranchTarget]
					}
				}

				static bool ResolveDvdCompareTarget()
				{
					auto* base = reinterpret_cast<uint8_t*>(GetMainModule());
					size_t size = GetMainModuleSize();
					if (!base || size == 0)
					{
						return false;
					}
					void* getDvdDrivePathRetAddr = FindPattern(base, size, kGetDvdDrivePathRetSign);
					if (!getDvdDrivePathRetAddr)
					{
						return false;
					}
					for (size_t i = 0; i < 0x50; ++i)
					{
						auto* candidate = reinterpret_cast<uint8_t*>(getDvdDrivePathRetAddr) - i;
						auto bytes = ReadU32(candidate);
						if (bytes && *bytes == 0x7E5AFB80u)
						{
							int8_t branchOffset = 0;
							if (!Rut::MemX::ReadMemory(candidate + 4, &branchOffset, sizeof(branchOffset)))
							{
								return false;
							}
							sg_dvdCompareTarget = candidate;
							sg_dvdCompareFallthrough = candidate + 5;
							sg_dvdCompareBranchTarget = candidate + 5 + branchOffset;
							return true;
						}
					}
					return false;
				}

				static bool ShouldInstallRuntimeHooksNow()
				{
					return InterlockedCompareExchange(&sg_runtimeHooksInstalled, 0, 0) == 0
						&& InterlockedCompareExchange(&sg_runtimeInstallRequested, 0, 0) != 0
						&& InterlockedCompareExchange(&sg_regInstallDelayElapsed, 0, 0) != 0;
				}

				static void SignalRuntimeInstallWorker(bool allowInlineFallback)
				{
					HANDLE eventHandle = sg_runtimeInstallEvent;
					if (eventHandle)
					{
						SetEvent(eventHandle);
						return;
					}
					if (allowInlineFallback
						&& InterlockedCompareExchange(&sg_runtimeWorkerStarted, 0, 0) == 0
						&& ShouldInstallRuntimeHooksNow())
					{
						InstallDeferredRuntimeHooksIfNeeded();
					}
				}

				static void MarkRuntimeInstallRequested(const wchar_t* reason, bool allowInlineFallback)
				{
					LONG wasRequested = InterlockedExchange(&sg_runtimeInstallRequested, 1);
					if (reason && wasRequested == 0)
					{
						LogMessage(LogLevel::Info, L"RioShiina: runtime install requested (%s)", reason);
					}
					SignalRuntimeInstallWorker(allowInlineFallback);
				}

				static bool StartRioRuntimeInstallWorker()
				{
					if (InterlockedCompareExchange(&sg_runtimeWorkerStarted, 1, 0) != 0)
					{
						return true;
					}
					if (!sg_runtimeInstallEvent)
					{
						sg_runtimeInstallEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
						if (!sg_runtimeInstallEvent)
						{
							InterlockedExchange(&sg_runtimeWorkerStarted, 0);
							LogMessage(LogLevel::Warn, L"RioShiina: CreateEventW failed for runtime worker");
							return false;
						}
					}
					HANDLE threadHandle = CreateThread(nullptr, 0, RioRuntimeInstallWorkerThread, nullptr, 0, nullptr);
					if (!threadHandle)
					{
						InterlockedExchange(&sg_runtimeWorkerStarted, 0);
						LogMessage(LogLevel::Warn, L"RioShiina: CreateThread failed for runtime worker");
						CloseHandle(sg_runtimeInstallEvent);
						sg_runtimeInstallEvent = nullptr;
						return false;
					}
					CloseHandle(threadHandle);
					return true;
				}

				static void InstallDeferredRuntimeHooksIfNeeded()
				{
					if (InterlockedCompareExchange(&sg_runtimeHooksInstalled, 0, 0) != 0)
					{
						return;
					}
					LogMessage(LogLevel::Info, L"RioShiina: install deferred runtime hooks");
					if (sg_settings.mode == 1 && InterlockedCompareExchange(&sg_modeRuntimeInstalled, 0, 0) == 0)
					{
						InstallMode1();
					}
					else if (sg_settings.mode == 2 && InterlockedCompareExchange(&sg_modeRuntimeInstalled, 0, 0) == 0)
					{
						InstallMode2();
					}
					if (sg_settings.processDvd && InterlockedCompareExchange(&sg_processDvdInstalled, 0, 0) == 0)
					{
						InstallProcessDvd();
					}
					const bool modeSatisfied = sg_settings.mode == 0 || InterlockedCompareExchange(&sg_modeRuntimeInstalled, 0, 0) != 0;
					const bool dvdSatisfied = !sg_settings.processDvd || InterlockedCompareExchange(&sg_processDvdInstalled, 0, 0) != 0;
					if (modeSatisfied && dvdSatisfied)
					{
						InterlockedExchange(&sg_runtimeHooksInstalled, 1);
						LogMessage(LogLevel::Info, L"RioShiina: runtime hooks fully installed");
					}
				}

				static DWORD WINAPI RioRuntimeInstallWorkerThread(LPVOID)
				{
					while (sg_runtimeInstallEvent && WaitForSingleObject(sg_runtimeInstallEvent, INFINITE) == WAIT_OBJECT_0)
					{
						if (InterlockedCompareExchange(&sg_regInstallDelayElapsed, 0, 0) == 0)
						{
							Sleep(kRegInstallDelayMs);
							InterlockedExchange(&sg_regInstallDelayElapsed, 1);
						}
						if (!ShouldInstallRuntimeHooksNow())
						{
							continue;
						}
						if (InterlockedCompareExchange(&sg_runtimeInstallInProgress, 1, 0) != 0)
						{
							continue;
						}
						InstallDeferredRuntimeHooksIfNeeded();
						InterlockedExchange(&sg_runtimeInstallInProgress, 0);
						if (InterlockedCompareExchange(&sg_runtimeHooksInstalled, 0, 0) != 0)
						{
							break;
						}
					}
					return 0;
				}

				static bool InstallProcessDvd()

				{
					if (!sg_settings.processDvd)
					{
						return true;
					}
					if (sg_settings.specDvdFileSize != 0)
					{
						if (!HookFileAPIs())
						{
							LogMessage(LogLevel::Warn, L"RioShiina: HookFileAPIs failed while enabling ProcessDvd");
							return false;
						}
						SetSyntheticFilePrefixSizeRule(L"W:\\", sg_settings.specDvdFileSize, sg_settings.enableLog);
						sg_dvdSyntheticRuleInstalled = true;
					}
					if (!ResolveDvdCompareTarget())
					{
						LogMessage(LogLevel::Warn, L"RioShiina: ProcessDvd compare target not found");
						return false;
					}
					if (!InstallInstructionJumpHook(sg_dvdCompareTarget, reinterpret_cast<void*>(DvdCompareDetour), 5))
					{
						LogMessage(LogLevel::Warn, L"RioShiina: ProcessDvd instruction hook install failed");
						return false;
					}
					LogMessage(LogLevel::Info, L"RioShiina: ProcessDvd=1 cmp=%p branch=%p fallthrough=%p size=%llu", sg_dvdCompareTarget, sg_dvdCompareBranchTarget, sg_dvdCompareFallthrough, sg_settings.specDvdFileSize);
					InterlockedExchange(&sg_processDvdInstalled, 1);
					return true;
				}

				static std::wstring GetBasename(const std::wstring& path)
			{
				size_t pos = path.find_last_of(L"\\/");
				return pos == std::wstring::npos ? path : path.substr(pos + 1);
			}

			static bool IsExistingFile(const std::wstring& path)
			{
				std::error_code ec;
				return fs::is_regular_file(fs::path(path), ec);
			}

			static bool IsValidAsciiFileName(const std::string& fileName)
			{
				for (unsigned char c : fileName)
				{
					if (c < ' ' || c > '~')
					{
						return false;
					}
				}
				return true;
			}

			static std::optional<uint32_t> ReadU32(const uint8_t* address)
			{
				uint32_t value = 0;
				if (!Rut::MemX::ReadMemory(const_cast<uint8_t*>(address), &value, sizeof(value)))
				{
					return std::nullopt;
				}
				return value;
			}

			static std::optional<std::string_view> TryReadCStringView(uint32_t address)
			{
				const char* text = reinterpret_cast<const char*>(static_cast<uintptr_t>(address));
				if (!text)
				{
					return std::nullopt;
				}
				size_t length = 0;
				for (; length < 256; ++length)
				{
					char ch = 0;
					if (!Rut::MemX::ReadMemory(const_cast<char*>(text + length), &ch, sizeof(ch)))
					{
						return std::nullopt;
					}
					if (ch == '\0')
					{
						return std::string_view(text, length);
					}
				}
				return std::nullopt;
			}

			static std::vector<PatternByte> ParsePattern(std::string_view pattern)
			{
				std::vector<PatternByte> result;
				result.reserve((pattern.size() + 1) / 3);
				auto hexToInt = [](char c) -> int
				{
					if (c >= '0' && c <= '9') return c - '0';
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					if (c >= 'a' && c <= 'f') return c - 'a' + 10;
					return -1;
				};
				for (size_t i = 0; i + 1 < pattern.size(); ++i)
				{
					if (pattern[i] == ' ')
					{
						continue;
					}
					int high = hexToInt(pattern[i]);
					int low = hexToInt(pattern[i + 1]);
					++i;
					uint8_t mask = 0;
					uint8_t data = 0;
					if (high != -1)
					{
						mask |= 0xF0;
						data |= static_cast<uint8_t>(high << 4);
					}
					if (low != -1)
					{
						mask |= 0x0F;
						data |= static_cast<uint8_t>(low);
					}
					result.push_back({ data, mask });
				}
				return result;
			}

			static void* FindPattern(const void* startAddress, size_t memorySize, std::string_view pattern)
			{
				std::vector<PatternByte> parsed = ParsePattern(pattern);
				if (parsed.empty() || memorySize < parsed.size())
				{
					return nullptr;
				}
				auto* memory = static_cast<const uint8_t*>(startAddress);
				size_t end = memorySize - parsed.size();
				for (size_t i = 0; i <= end; ++i)
				{
					bool matched = true;
					for (size_t j = 0; j < parsed.size(); ++j)
					{
						if ((memory[i + j] & parsed[j].mask) != parsed[j].data)
						{
							matched = false;
							break;
						}
					}
					if (matched)
					{
						return const_cast<uint8_t*>(memory + i);
					}
				}
				return nullptr;
			}

			static fs::path ResolveArchivePath(const std::wstring& configuredPath)
			{
				std::error_code ec;
				fs::path path(configuredPath);
				if (!path.is_absolute())
				{
					path = fs::path(sg_gameDir) / path;
				}
				path = fs::absolute(path, ec);
				if (ec)
				{
					return {};
				}
				return path;
			}

			static void InitializeConfiguredArchives()
			{
				sg_archivesToExtract.clear();
				sg_fileNamesInArchives.clear();
				for (const auto& archive : sg_settings.archivesToExtract)
				{
					fs::path resolved = ResolveArchivePath(archive);
					std::error_code ec;
					if (resolved.empty() || !fs::exists(resolved, ec))
					{
						LogMessage(LogLevel::Warn, L"RioShiina: archive missing, skip: %s", archive.c_str());
						continue;
					}
					sg_archivesToExtract.insert(resolved);
					if (sg_settings.enableLog)
					{
						LogMessage(LogLevel::Info, L"RioShiina: queued archive %s", resolved.wstring().c_str());
					}
				}
			}

			static void InitializePatchSources(const FilePatchSettings& filePatchSettings)
			{
				sg_filePatchSettings = filePatchSettings;
				sg_patchRootFolders.clear();
				sg_patchCustomPakFiles.clear();
				if (filePatchSettings.enable)
				{
					for (const auto& folder : filePatchSettings.patchFolders)
					{
						fs::path resolved = fs::path(ToAbsoluteGamePath(folder));
						std::error_code ec;
						if (fs::is_directory(resolved, ec))
						{
							sg_patchRootFolders.push_back(resolved.wstring());
						}
					}
					if (filePatchSettings.customPakEnable)
					{
						for (const auto& pakFile : filePatchSettings.customPakFiles)
						{
							fs::path resolved = fs::path(ToAbsoluteGamePath(pakFile));
							std::error_code ec;
							if (fs::is_regular_file(resolved, ec))
							{
								sg_patchCustomPakFiles.push_back(resolved.wstring());
							}
						}
					}
				}
			}

			static void* CopyDataToGlobalBuffer(const uint8_t* data, size_t size, uint32_t* pBytesRead)
			{
				if (!data || size == 0 || !pBytesRead || size > 0xFFFFFFFFull)
				{
					return nullptr;
				}
				void* buffer = GlobalAlloc(GMEM_FIXED, static_cast<SIZE_T>(size));
				if (!buffer)
				{
					return nullptr;
				}
				memcpy(buffer, data, size);
				*pBytesRead = static_cast<uint32_t>(size);
				return buffer;
			}

			static void* ReadDiskOverrideFile(const fs::path& path, uint32_t* pBytesRead)
			{
				std::ifstream ifs(path, std::ios::binary);
				if (!ifs)
				{
					LogMessage(LogLevel::Error, L"RioShiina: cannot open override file: %s", path.wstring().c_str());
					return nullptr;
				}
				std::error_code ec;
				auto size = fs::file_size(path, ec);
				if (ec || size == static_cast<uintmax_t>(-1) || size > 0xFFFFFFFFull)
				{
					LogMessage(LogLevel::Error, L"RioShiina: invalid override file size: %s", path.wstring().c_str());
					return nullptr;
				}
				void* buffer = GlobalAlloc(GMEM_FIXED, static_cast<SIZE_T>(size));
				if (!buffer)
				{
					LogMessage(LogLevel::Error, L"RioShiina: GlobalAlloc failed for %s", path.wstring().c_str());
					return nullptr;
				}
				ifs.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
				*pBytesRead = static_cast<uint32_t>(ifs.gcount());
				return buffer;
			}

			static void* TryReadOverrideFromCustomPaks(const std::wstring& relativePath, uint32_t* pBytesRead, std::wstring& sourceLabel)
			{
				std::vector<std::wstring> nestedArchives;
				for (auto it = sg_patchCustomPakFiles.rbegin(); it != sg_patchCustomPakFiles.rend(); ++it)
				{
					std::shared_ptr<const std::vector<uint8_t>> archiveData;
					if (!ResolveCustomPakArchiveDataEx(it->c_str(), nestedArchives, relativePath.c_str(), archiveData) || !archiveData || archiveData->empty())
					{
						continue;
					}
					void* buffer = CopyDataToGlobalBuffer(archiveData->data(), archiveData->size(), pBytesRead);
					if (!buffer)
					{
						LogMessage(LogLevel::Error, L"RioShiina: GlobalAlloc failed for custom pak item %s>%s", it->c_str(), relativePath.c_str());
						return nullptr;
					}
					sourceLabel = *it + L">" + relativePath;
					return buffer;
				}
				return nullptr;
			}

			static void SuspendPatchSourcesForExtraction()
			{
				if (sg_patchSourcesSuspended)
				{
					return;
				}
				SetPatchFolders(nullptr, 0, false);
				SetCustomPakVFS(false, nullptr, 0, false);
				sg_patchSourcesSuspended = true;
			}

			static void RestorePatchSourcesAfterExtraction()
			{
				if (!sg_patchSourcesSuspended)
				{
					return;
				}
				std::vector<const wchar_t*> patchFolders;
				for (const auto& folder : sg_patchRootFolders)
				{
					patchFolders.push_back(folder.c_str());
				}
				SetPatchFolders(patchFolders.empty() ? nullptr : patchFolders.data(), patchFolders.size(), sg_filePatchSettings.enableLog);
				std::vector<const wchar_t*> customPakFiles;
				for (const auto& pakFile : sg_patchCustomPakFiles)
				{
					customPakFiles.push_back(pakFile.c_str());
				}
				SetCustomPakVFS(!customPakFiles.empty(), customPakFiles.empty() ? nullptr : customPakFiles.data(), customPakFiles.size(), sg_filePatchSettings.enableLog);
				SetCustomPakReadMode(sg_filePatchSettings.vfsMode);
				sg_patchSourcesSuspended = false;
			}

			static bool DetachDetourIfNeeded(void** target, void* detour, volatile LONG& detachedFlag, const wchar_t* operationName)
			{
				if (InterlockedCompareExchange(&detachedFlag, 1, 0) != 0)
				{
					return true;
				}
				bool batchStarted = BeginDetourBatch();
				bool failed = !TryDetourDetach(target, detour);
				if (!CommitDetourBatchIfStarted(batchStarted, operationName))
				{
					failed = true;
				}
				if (failed)
				{
					InterlockedExchange(&detachedFlag, 0);
				}
				return !failed;
			}

			static bool IsMode2ExtractExceptionCode(DWORD code)
				{
					switch (code)
					{
					case EXCEPTION_ACCESS_VIOLATION:
					case EXCEPTION_IN_PAGE_ERROR:
					case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
					case EXCEPTION_DATATYPE_MISALIGNMENT:
					case EXCEPTION_ILLEGAL_INSTRUCTION:
					case EXCEPTION_PRIV_INSTRUCTION:
					case EXCEPTION_STACK_OVERFLOW:
						return true;
					default:
						return false;
					}
				}

				static LONG NTAPI RioMode2ExtractExceptionHandler(PEXCEPTION_POINTERS exceptionInfo)
				{
					if (sg_mode2ExtractThreadId == GetCurrentThreadId()
						&& exceptionInfo
						&& exceptionInfo->ExceptionRecord
						&& IsMode2ExtractExceptionCode(exceptionInfo->ExceptionRecord->ExceptionCode))
					{
						InterlockedIncrement(&sg_mode2ExtractExceptionSerial);
					}
					return EXCEPTION_CONTINUE_SEARCH;
				}

				static bool TryGlobalFree(void* data)
				{
					if (!data)
					{
						return true;
					}
					__try
					{
						return GlobalFree(data) == nullptr;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return false;
					}
				}

				static void* TryRedirectFile(const char* logicalFileName, uint32_t* pBytesRead)
			{
				if (!logicalFileName || !pBytesRead)
				{
					return nullptr;
				}
				std::wstring fileName = GetBasename(SjisToWide(logicalFileName));
				if (fileName.empty())
				{
					return nullptr;
				}
				for (auto patchIt = sg_settings.patchNames.rbegin(); patchIt != sg_settings.patchNames.rend(); ++patchIt)
				{
					fs::path relativePath = fs::path(*patchIt) / fileName;
					for (auto rootIt = sg_patchRootFolders.rbegin(); rootIt != sg_patchRootFolders.rend(); ++rootIt)
					{
						fs::path overridePath = fs::path(*rootIt) / relativePath;
						if (!IsExistingFile(overridePath.wstring()))
						{
							continue;
						}
						if (void* buffer = ReadDiskOverrideFile(overridePath, pBytesRead))
						{
							if (sg_settings.enableLog)
							{
								LogMessage(LogLevel::Info, L"RioShiina: override hit %s", overridePath.wstring().c_str());
							}
							return buffer;
						}
					}
					std::wstring sourceLabel;
					if (void* buffer = TryReadOverrideFromCustomPaks(relativePath.wstring(), pBytesRead, sourceLabel))
					{
						if (sg_settings.enableLog)
						{
							LogMessage(LogLevel::Info, L"RioShiina: override hit %s", sourceLabel.c_str());
						}
						return buffer;
					}
					fs::path overridePath = fs::path(ToAbsoluteGamePath(relativePath.wstring()));
					if (!IsExistingFile(overridePath.wstring()))
					{
						continue;
					}
					if (void* buffer = ReadDiskOverrideFile(overridePath, pBytesRead))
					{
						if (sg_settings.enableLog)
						{
							LogMessage(LogLevel::Info, L"RioShiina: override hit %s", overridePath.wstring().c_str());
						}
						return buffer;
					}
				}
				return nullptr;
			}

			static void ExtractArchive(const fs::path& archivePath)
			{
				auto iter = sg_fileNamesInArchives.find(archivePath);
				if (iter == sg_fileNamesInArchives.end() || iter->second.empty() || !sg_rawRioGetRes)
				{
					return;
				}
				fs::path outputRoot = fs::path(ToAbsoluteGamePath(sg_settings.extractOutputDir)) / archivePath.stem();
				std::error_code ec;
				fs::create_directories(outputRoot, ec);
				if (!sg_mode2ExtractExceptionHandler)
				{
					sg_mode2ExtractExceptionHandler = AddVectoredExceptionHandler(1, RioMode2ExtractExceptionHandler);
				}
				InterlockedExchange(&sg_mode2ExtractExceptionSerial, 0);
				sg_mode2ExtractThreadId = GetCurrentThreadId();
				for (const auto& fileName : iter->second)
				{
					if (sg_settings.skipInvalidFileName && !IsValidAsciiFileName(fileName))
					{
						if (sg_settings.enableLog)
						{
							LogMessage(LogLevel::Warn, L"RioShiina: skip invalid filename in %s: %s", archivePath.filename().wstring().c_str(), SjisToWide(fileName).c_str());
						}
						continue;
					}
					LONG beforeExceptionSerial = InterlockedCompareExchange(&sg_mode2ExtractExceptionSerial, 0, 0);
					uint32_t bytesRead = 0;
					void* data = CallRioGetRes(sg_rawRioGetRes, fileName.c_str(), &bytesRead);
					if (InterlockedCompareExchange(&sg_mode2ExtractExceptionSerial, 0, 0) != beforeExceptionSerial)
					{
						TryGlobalFree(data);
						if (sg_settings.enableLog)
						{
							LogMessage(LogLevel::Warn, L"RioShiina: skip file because RioGetRes raised VEH exception %s from %s", SjisToWide(fileName).c_str(), archivePath.filename().wstring().c_str());
						}
						continue;
					}
					if (data)
					{
						fs::path outputPath = outputRoot / SjisToWide(fileName);
						fs::create_directories(outputPath.parent_path(), ec);
						std::ofstream ofs(outputPath, std::ios::binary);
						if (!ofs)
						{
							LogMessage(LogLevel::Error, L"RioShiina: cannot create output file: %s", outputPath.wstring().c_str());
							TryGlobalFree(data);
							continue;
						}
						ofs.write(static_cast<const char*>(data), bytesRead);
						ofs.close();
						TryGlobalFree(data);
						if (InterlockedCompareExchange(&sg_mode2ExtractExceptionSerial, 0, 0) != beforeExceptionSerial)
						{
							std::error_code removeEc;
							fs::remove(outputPath, removeEc);
							if (sg_settings.enableLog)
							{
								LogMessage(LogLevel::Warn, L"RioShiina: removed file because extraction raised VEH exception %s from %s", outputPath.filename().wstring().c_str(), archivePath.filename().wstring().c_str());
							}
							continue;
						}
						if (sg_settings.enableLog)
						{
							LogMessage(LogLevel::Info, L"RioShiina: extracted %s", outputPath.wstring().c_str());
						}
					}
					else if (sg_settings.enableLog)
					{
						LogMessage(LogLevel::Error, L"RioShiina: failed to read file %s from %s", SjisToWide(fileName).c_str(), archivePath.filename().wstring().c_str());
					}
				}
				sg_mode2ExtractThreadId = 0;
				if (sg_mode2ExtractExceptionHandler)
				{
					RemoveVectoredExceptionHandler(sg_mode2ExtractExceptionHandler);
					sg_mode2ExtractExceptionHandler = nullptr;
				}
			}

			static void CaptureArchiveIndex(uintptr_t originalEsp)
			{
				if (sg_currentArchivePath.empty() || sg_fileNamesInArchives[sg_currentArchivePath].size() != 0)
				{
					return;
				}
				auto* espPtr = reinterpret_cast<uint8_t**>(originalEsp);
				uint8_t* entryAddress = *espPtr;
				if (!entryAddress)
				{
					return;
				}
				if (!sg_entrySize.has_value())
				{
					std::string_view firstName(reinterpret_cast<const char*>(entryAddress));
					uint8_t* zeroAddress = entryAddress + firstName.length();
					size_t zeroCount = 0;
					while (*zeroAddress == 0)
					{
						++zeroCount;
						++zeroAddress;
					}
					sg_entrySize = (firstName.length() + zeroCount >= 0x20) ? 0x38u : 0x28u;
				}
				auto& names = sg_fileNamesInArchives[sg_currentArchivePath];
				while (*entryAddress != 0)
				{
					names.insert(reinterpret_cast<const char*>(entryAddress));
					entryAddress += sg_entrySize.value();
				}
				if (sg_settings.enableLog)
				{
					LogMessage(LogLevel::Info, L"RioShiina: indexed %u files in %s", static_cast<uint32_t>(names.size()), sg_currentArchivePath.filename().wstring().c_str());
				}
			}

			static void* __cdecl HookRioGetResMode1(const char* lpFileName, uint32_t* pBytesRead)
			{
				if (void* redirected = TryRedirectFile(lpFileName, pBytesRead))
				{
					return redirected;
				}
				return CallRioGetRes(sg_rawRioGetRes, lpFileName, pBytesRead);
			}

			static void* __cdecl HookRioReadFileMode1(const char* currentWorkDir, const char* logicFileName, uint32_t* pBytesRead)
			{
				if (void* redirected = TryRedirectFile(logicFileName, pBytesRead))
				{
					return redirected;
				}
				return sg_rawRioReadFile ? sg_rawRioReadFile(currentWorkDir, logicFileName, pBytesRead) : nullptr;
			}

			static void* __cdecl HookRioGetResMode2(const char* lpFileName, uint32_t* pBytesRead)
				{
					if (sg_settings.enableLog && lpFileName && sg_mode2RioGetResLogCount < 32)
					{
						++sg_mode2RioGetResLogCount;
						LogMessage(LogLevel::Info, L"RioShiina: Mode2 RioGetRes[%u] %s", sg_mode2RioGetResLogCount, SjisToWide(lpFileName).c_str());
					}
					void* result = CallRioGetRes(sg_rawRioGetRes, lpFileName, pBytesRead);
					if (!sg_efclibLoaded)
					{
						std::string lowered = ToLowerAscii(lpFileName ? std::string(lpFileName) : std::string());
						if (lowered.size() >= 10 && EndsWith(lowered, "efclib.scn"))
						{
							sg_efclibLoaded = true;
							LogMessage(LogLevel::Info, L"RioShiina: detected archive-init script load, mode 2 extraction is now armed");
						}
						return result;
					}
					if (!sg_extractCompleted)
					{
						sg_extractCompleted = true;
						LogMessage(LogLevel::Info, L"RioShiina: first Mode2 extraction pass filePtr=%p", lpFileName);
						SuspendPatchSourcesForExtraction();
						for (const auto& archive : sg_archivesToExtract)
						{
							ExtractArchive(archive);
						}
						RestorePatchSourcesAfterExtraction();
						LogMessage(LogLevel::Info, L"RioShiina: extraction finished");
					}
					return result;
				}

				static HANDLE __cdecl HookFindAndOpenFileInWarc(const char* warcName, char* pureFileName, DWORD dwDesiredAccess, void* a4Zero, void* a5Zero, void* a6Zero, char** pPureFileName)
				{
					if (!sg_loggedFirstFindAndOpenFileInWarc)
					{
						sg_loggedFirstFindAndOpenFileInWarc = true;
						LogMessage(LogLevel::Info, L"RioShiina: first FindAndOpenFileInWarc warcPtr=%p", warcName);
					}
					if (sg_mode2PrimingDone)
					{
						return sg_rawFindAndOpenFileInWarc ? sg_rawFindAndOpenFileInWarc(warcName, pureFileName, dwDesiredAccess, a4Zero, a5Zero, a6Zero, pPureFileName) : INVALID_HANDLE_VALUE;
					}
					sg_mode2CaptureEnabled = true;
					for (const auto& archive : sg_archivesToExtract)
					{
						sg_currentArchivePath = archive;
						HANDLE result = sg_rawFindAndOpenFileInWarc ? sg_rawFindAndOpenFileInWarc(WideToAnsi(archive.wstring()).c_str(), pureFileName, dwDesiredAccess, nullptr, nullptr, nullptr, nullptr) : INVALID_HANDLE_VALUE;
						if (result != INVALID_HANDLE_VALUE && result != nullptr)
						{
							CloseHandle(result);
						}
					}
					sg_mode2CaptureEnabled = false;
					sg_currentArchivePath.clear();
					RemoveJumpPatch(sg_afterDecompressPatch);
					sg_mode2PrimingDone = true;
					if (sg_settings.enableLog)
					{
						LogMessage(LogLevel::Info, L"RioShiina: after-decompress instruction hook removed before original FindAndOpenFileInWarc");
						LogMessage(LogLevel::Info, L"RioShiina: mode 2 transient phase finished, bypass future priming");
					}
					HANDLE ret = sg_rawFindAndOpenFileInWarc ? sg_rawFindAndOpenFileInWarc(warcName, pureFileName, dwDesiredAccess, a4Zero, a5Zero, a6Zero, pPureFileName) : INVALID_HANDLE_VALUE;
					DetachDetourIfNeeded(reinterpret_cast<void**>(&sg_rawFindAndOpenFileInWarc), reinterpret_cast<void*>(HookFindAndOpenFileInWarc), sg_findAndOpenDetached, L"RioShiina mode 2 FindAndOpen detach");
					return ret;
				}

				static void __cdecl AfterDecompressBridge(uintptr_t originalEsp)
				{
					if (!sg_mode2CaptureEnabled)
					{
						return;
					}
					if (!sg_loggedFirstAfterDecompress)
					{
						sg_loggedFirstAfterDecompress = true;
						LogMessage(LogLevel::Info, L"RioShiina: first AfterDecompress esp=0x%p", reinterpret_cast<void*>(originalEsp));
					}
					CaptureArchiveIndex(originalEsp);
				}

				__declspec(naked) static void AfterDecompressDetour()
			{
				__asm
				{
					push eax
					lea eax, [esp + 4]
					pushfd
					pushad
					push eax
					call AfterDecompressBridge
					add esp, 4
					popad
					popfd
					pop eax
					add esp, 14h
					test eax, eax
					jmp dword ptr [sg_afterDecompressResume]
				}
			}

			static bool AttachDetour(void** target, void* detour)
			{
				return TryDetourAttach(target, detour);
			}

			static void* CallRioGetRes(RioGetResFn function, const char* fileName, uint32_t* pBytesRead)
			{
				if (!function || !fileName || !pBytesRead)
				{
					return nullptr;
				}
				*pBytesRead = 0;
				void* result = nullptr;
				__asm
				{
					xor eax, eax
					push pBytesRead
					push fileName
					call function
					add esp, 8
					mov result, eax
				}
				return result;
			}

			static bool ResolveMode1Targets(bool& rioReadFileFound)
			{
				auto* base = reinterpret_cast<uint8_t*>(GetMainModule());
				size_t size = GetMainModuleSize();
				if (!base || size == 0)
				{
					return false;
				}
				sg_rawRioGetRes = reinterpret_cast<RioGetResFn>(FindPattern(base, size, kRioGetResSign));
				sg_rawRioReadFile = reinterpret_cast<RioReadFileFn>(FindPattern(base, size, kRioReadFileSign));
				rioReadFileFound = sg_rawRioReadFile != nullptr;
				return sg_rawRioGetRes != nullptr;
			}

			static bool ResolveMode2Targets()
			{
				auto* base = reinterpret_cast<uint8_t*>(GetMainModule());
				size_t size = GetMainModuleSize();
				if (!base || size == 0)
				{
					return false;
				}
				sg_rawRioGetRes = reinterpret_cast<RioGetResFn>(FindPattern(base, size, kRioGetResSign));
				void* pushWarcString = FindPattern(base, size, kPushWarcStrSign);
				while (pushWarcString)
				{
					auto stringAddress = ReadU32(reinterpret_cast<uint8_t*>(pushWarcString) + 6);
					auto stringView = stringAddress ? TryReadCStringView(*stringAddress) : std::nullopt;
					if (stringView && StartsWith(*stringView, "WARC 1."))
					{
						break;
					}
					size_t offset = reinterpret_cast<uint8_t*>(pushWarcString) + 1 - base;
					if (offset >= size)
					{
						pushWarcString = nullptr;
						break;
					}
					pushWarcString = FindPattern(base + offset, size - offset, kPushWarcStrSign);
				}
				if (!sg_rawRioGetRes || !pushWarcString)
				{
					return false;
				}
				uintptr_t functionStart = 0;
				for (size_t i = 0; i < 0x300; ++i)
				{
					auto candidate = ReadU32(reinterpret_cast<uint8_t*>(pushWarcString) - i);
					if (candidate && *candidate == 0xEC8B5590u)
					{
						functionStart = reinterpret_cast<uintptr_t>(pushWarcString) - i + 1;
						break;
					}
				}
				if (!functionStart)
				{
					return false;
				}
				void* decompressPush = FindPattern(pushWarcString, 0x200, "51 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68");
				if (!decompressPush)
				{
					return false;
				}
				void* decompressCall = FindPattern(reinterpret_cast<uint8_t*>(decompressPush) + 0x10, 0x10, kAfterDecompressCallSign);
				if (!decompressCall)
				{
					return false;
				}
				sg_rawFindAndOpenFileInWarc = reinterpret_cast<FindAndOpenFileInWarcFn>(functionStart);
				sg_afterDecompressHookTarget = reinterpret_cast<uint8_t*>(decompressCall) + 0x5;
					sg_afterDecompressResume = reinterpret_cast<uint8_t*>(sg_afterDecompressHookTarget) + 5;
				return true;
			}

			static void InstallMode1()
				{
					bool rioReadFileFound = false;
					if (!ResolveMode1Targets(rioReadFileFound))
					{
						LogMessage(LogLevel::Warn, L"RioShiina: mode 1 signatures not found");
						return;
					}
					bool batchStarted = BeginDetourBatch();
					bool rioReadFileEnabled = false;
					bool failed = false;
					if (rioReadFileFound)
					{
						rioReadFileEnabled = AttachDetour(reinterpret_cast<void**>(&sg_rawRioReadFile), reinterpret_cast<void*>(HookRioReadFileMode1));
						if (!rioReadFileEnabled)
						{
							LogMessage(LogLevel::Warn, L"RioShiina: RioReadFile hook failed, fallback to RioGetRes");
						}
					}
					if (!rioReadFileEnabled)
					{
						failed = !AttachDetour(reinterpret_cast<void**>(&sg_rawRioGetRes), reinterpret_cast<void*>(HookRioGetResMode1));
					}
					if (!CommitDetourBatchIfStarted(batchStarted, L"RioShiina mode 1 detours"))
					{
						failed = true;
					}
					if (failed)
					{
						LogMessage(LogLevel::Warn, L"RioShiina: mode 1 hook attach failed");
						return;
					}
					InterlockedExchange(&sg_modeRuntimeInstalled, 1);
					LogMessage(LogLevel::Info, L"RioShiina: mode 1 enabled, RioGetRes=%p RioReadFile=%p activePath=%s", sg_rawRioGetRes, sg_rawRioReadFile, rioReadFileEnabled ? L"RioReadFile" : L"RioGetRes");
					(void)batchStarted;
				}

				static void InstallMode2()
			{
				InitializeConfiguredArchives();
				if (sg_archivesToExtract.empty())
				{
					LogMessage(LogLevel::Warn, L"RioShiina: no valid archives configured for mode 2");
					return;
				}
				if (!ResolveMode2Targets())
				{
					LogMessage(LogLevel::Warn, L"RioShiina: mode 2 signatures not found");
					return;
				}
				bool batchStarted = BeginDetourBatch();
				bool failed = !AttachDetour(reinterpret_cast<void**>(&sg_rawRioGetRes), reinterpret_cast<void*>(HookRioGetResMode2));
				failed = !AttachDetour(reinterpret_cast<void**>(&sg_rawFindAndOpenFileInWarc), reinterpret_cast<void*>(HookFindAndOpenFileInWarc)) || failed;
				if (!CommitDetourBatchIfStarted(batchStarted, L"RioShiina mode 2 detours"))
				{
					failed = true;
				}
				if (failed)
				{
					LogMessage(LogLevel::Warn, L"RioShiina: mode 2 hook attach failed");
					return;
				}
				if (!InstallJumpPatch(sg_afterDecompressHookTarget, reinterpret_cast<void*>(AfterDecompressDetour), 5, sg_afterDecompressPatch))
				{
					LogMessage(LogLevel::Warn, L"RioShiina: mode 2 after-decompress jump patch install failed");
					return;
				}
				InterlockedExchange(&sg_modeRuntimeInstalled, 1);
				LogMessage(LogLevel::Info, L"RioShiina: mode 2 enabled, archives=%u", static_cast<uint32_t>(sg_archivesToExtract.size()));
				(void)batchStarted;
			}
#endif
		}

		void Apply(const RioShiinaSettings& settings, const FilePatchSettings& filePatchSettings)
		{
			if (!settings.enable)
			{
				return;
			}
#if !defined(_M_IX86)
			LogMessage(LogLevel::Warn, L"RioShiina: only x86 is currently supported; skip on this build");
#else
			if (sg_installed)
			{
				LogMessage(LogLevel::Info, L"RioShiina: hooks already installed, skip duplicate apply");
				return;
			}
				sg_installed = true;
				sg_settings = settings;
				sg_gameDir = GetGameDirectory();
					InitializePatchSources(filePatchSettings);
				sg_efclibLoaded = false;
				sg_extractCompleted = false;
				sg_entrySize.reset();
				sg_dvdSyntheticRuleInstalled = false;
				sg_loggedFirstRegOpenKeyExA = false;
				sg_loggedFirstDvdCompare = false;
				sg_loggedFirstFindAndOpenFileInWarc = false;
				sg_loggedFirstAfterDecompress = false;
				sg_mode2CaptureEnabled = false;
				sg_mode2PrimingDone = false;
				sg_mode2RioGetResLogCount = 0;
				sg_mode2ExtractThreadId = 0;
				InterlockedExchange(&sg_mode2ExtractExceptionSerial, 0);
				if (sg_mode2ExtractExceptionHandler)
				{
					RemoveVectoredExceptionHandler(sg_mode2ExtractExceptionHandler);
					sg_mode2ExtractExceptionHandler = nullptr;
				}
				sg_patchSourcesSuspended = false;
				sg_currentArchivePath.clear();
				sg_archivesToExtract.clear();
				sg_fileNamesInArchives.clear();
					sg_patchRootFolders.clear();
					sg_patchCustomPakFiles.clear();
				InterlockedExchange(&sg_runtimeInstallRequested, 0);
				InterlockedExchange(&sg_runtimeInstallInProgress, 0);
				InterlockedExchange(&sg_runtimeWorkerStarted, 0);
				InterlockedExchange(&sg_runtimeHooksInstalled, 0);
				InterlockedExchange(&sg_modeRuntimeInstalled, 0);
				InterlockedExchange(&sg_processDvdInstalled, 0);
				InterlockedExchange(&sg_regObserved, 0);
				InterlockedExchange(&sg_regInstallDelayElapsed, 0);
					InterlockedExchange(&sg_findAndOpenDetached, 0);
					InterlockedExchange(&sg_processRegDetached, 0);
				RemoveJumpPatch(sg_afterDecompressPatch);
				if (sg_runtimeInstallEvent)
				{
					CloseHandle(sg_runtimeInstallEvent);
					sg_runtimeInstallEvent = nullptr;
				}
				if (settings.mode != 0 || settings.processDvd)
				{
					bool runtimeWorkerStarted = StartRioRuntimeInstallWorker();
					MarkRuntimeInstallRequested(L"Apply", false);
					if (!runtimeWorkerStarted && ShouldInstallRuntimeHooksNow())
					{
						InstallDeferredRuntimeHooksIfNeeded();
					}
				}
				if (settings.processReg)
				{
					InstallProcessReg();
				}
#endif
		}
	}
}
