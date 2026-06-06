#include "binary_patch_1337.h"

#include <Windows.h>

#include "../../RuntimeCore/base/Mem.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <exception>

using namespace Rut::HookX;

namespace CialloHook
{
	namespace HookModules
	{
		namespace
		{
			static volatile LONG sg_preEntryBinaryPatchApplied = 0;
			static volatile LONG sg_preEntryBinaryPatchActive = 0;
			static volatile LONG sg_preEntryBinaryPatchOk = 0;
			static volatile LONG sg_preEntryBinaryPatchThreadId = 0;
			static HANDLE sg_preEntryBinaryPatchDoneEvent = nullptr;
			static volatile LONG sg_hwbpBinaryPatchInstalled = 0;
			static volatile LONG sg_hwbpBinaryPatchApplying = 0;
			static PVOID sg_hwbpBinaryPatchVeh = nullptr;
			static uintptr_t sg_hwbpBinaryPatchTarget = 0;
			static BinaryPatchSettings sg_hwbpBinaryPatchSettings;
			static const DWORD kBinaryPatchSetHwbpException = 0xEABC1337u;


			struct PatchByte
			{
				uint32_t rva = 0;
				uint8_t oldByte = 0;
				uint8_t newByte = 0;
				uint32_t lineNumber = 0;
				uint32_t order = 0;
			};

			struct PatchBlock
			{
				uint32_t rva = 0;
				std::vector<uint8_t> oldBytes;
				std::vector<uint8_t> newBytes;
			};

			struct ModulePatch
			{
				std::wstring moduleName;
				std::vector<PatchByte> bytes;
				std::vector<PatchBlock> blocks;
			};

			struct PatchFileData
			{
				std::wstring configuredPath;
				std::wstring resolvedPath;
				std::wstring source;
				std::string text;
			};

			struct ApplyStats
			{
				uint32_t files = 0;
				uint32_t modules = 0;
				uint32_t blocks = 0;
				uint32_t bytes = 0;
				uint32_t ok = 0;
				uint32_t failed = 0;
				uint32_t skipped = 0;
			};

			static std::string TrimString(std::string value)
			{
				size_t begin = 0;
				while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
				{
					++begin;
				}
				size_t end = value.size();
				while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
				{
					--end;
				}
				return value.substr(begin, end - begin);
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

			static bool IsAbsolutePath(const std::wstring& path)
			{
				if (path.size() >= 2 && path[1] == L':')
				{
					return true;
				}
				return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
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

			static std::wstring GetGameDirectory()
			{
				wchar_t exePath[MAX_PATH] = {};
				if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
				{
					return L"";
				}
				std::wstring gameDir = exePath;
				size_t pos = gameDir.find_last_of(L"\\/");
				return pos == std::wstring::npos ? std::wstring() : gameDir.substr(0, pos);
			}

			static std::wstring ToGameAbsolutePath(const std::wstring& gameDir, const std::wstring& inputPath)
			{
				if (inputPath.empty() || IsAbsolutePath(inputPath))
				{
					return inputPath;
				}
				return JoinPath(gameDir, inputPath);
			}

			static bool IsExistingRegularFile(const std::wstring& path)
			{
				DWORD attr = GetFileAttributesW(path.c_str());
				return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
			}

			static std::wstring Utf8OrAcpToWide(const std::string& text)
			{
				if (text.empty())
				{
					return L"";
				}
				int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
				UINT codePage = CP_UTF8;
				if (len <= 0)
				{
					codePage = CP_ACP;
					len = MultiByteToWideChar(codePage, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
				}
				if (len <= 0)
				{
					return L"";
				}
				std::wstring result(static_cast<size_t>(len), L'\0');
				MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text.c_str(), static_cast<int>(text.size()), &result[0], len);
				return result;
			}

			static bool ReadDiskFileBytes(const std::wstring& path, std::vector<uint8_t>& bytes)
			{
				bytes.clear();
				HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE)
				{
					return false;
				}

				LARGE_INTEGER size = {};
				if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64ll * 1024ll * 1024ll)
				{
					CloseHandle(file);
					return false;
				}

				bytes.resize(static_cast<size_t>(size.QuadPart));
				DWORD read = 0;
				BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
				CloseHandle(file);
				if (!ok || read != bytes.size())
				{
					bytes.clear();
					return false;
				}
				return true;
			}

			static std::string DecodePatchBytesToText(const std::vector<uint8_t>& bytes)
			{
				if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
				{
					const wchar_t* wideText = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
					int wideLen = static_cast<int>((bytes.size() - 2) / sizeof(wchar_t));
					int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideText, wideLen, nullptr, 0, nullptr, nullptr);
					if (utf8Len > 0)
					{
						std::string result(static_cast<size_t>(utf8Len), '\0');
						WideCharToMultiByte(CP_UTF8, 0, wideText, wideLen, &result[0], utf8Len, nullptr, nullptr);
						return result;
					}
				}

				size_t offset = 0;
				if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
				{
					offset = 3;
				}
				return std::string(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
			}

			static bool LoadPatchFileText(const std::wstring& gameDir, const std::wstring& configuredPath, bool preferCustomPak, PatchFileData& out)
			{
				out = {};
				out.configuredPath = configuredPath;
				out.resolvedPath = ToGameAbsolutePath(gameDir, configuredPath);

				auto loadCustomPak = [&]() -> bool
				{
					std::shared_ptr<const std::vector<uint8_t>> data;
					if (!ResolveCustomPakVFSData(out.resolvedPath.c_str(), data) || !data || data->empty())
					{
						return false;
					}
					out.text = DecodePatchBytesToText(*data);
					out.source = L"custompak";
					out.resolvedPath = L"CustomPak:" + configuredPath;
					return true;
				};

				auto loadDisk = [&]() -> bool
				{
					if (!IsExistingRegularFile(out.resolvedPath))
					{
						return false;
					}
					std::vector<uint8_t> bytes;
					if (!ReadDiskFileBytes(out.resolvedPath, bytes))
					{
						return false;
					}
					out.text = DecodePatchBytesToText(bytes);
					out.source = L"disk";
					return true;
				};

				UNREFERENCED_PARAMETER(preferCustomPak);
					return loadDisk() || loadCustomPak();
			}

			static bool TryParseHexUInt32(std::string text, uint32_t& value)
			{
				text = TrimString(text);
				if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
				{
					text = text.substr(2);
				}
				if (text.empty())
				{
					return false;
				}
				unsigned long long parsed = 0;
				for (char ch : text)
				{
					int digit = 0;
					if (ch >= '0' && ch <= '9') digit = ch - '0';
					else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
					else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
					else return false;
					parsed = (parsed << 4) | static_cast<unsigned>(digit);
					if (parsed > 0xFFFFFFFFull)
					{
						return false;
					}
				}
				value = static_cast<uint32_t>(parsed);
				return true;
			}

			static bool TryParseHexByte(std::string text, uint8_t& value)
			{
				uint32_t parsed = 0;
				if (!TryParseHexUInt32(std::move(text), parsed) || parsed > 0xFF)
				{
					return false;
				}
				value = static_cast<uint8_t>(parsed);
				return true;
			}

			static ModulePatch* FindOrAddModule(std::vector<ModulePatch>& modules, const std::wstring& moduleName)
			{
				for (ModulePatch& module : modules)
				{
					if (_wcsicmp(module.moduleName.c_str(), moduleName.c_str()) == 0)
					{
						return &module;
					}
				}
				modules.push_back({});
				modules.back().moduleName = moduleName;
				return &modules.back();
			}

			static void BuildPatchBlocks(ModulePatch& module, const std::wstring& filePath)
			{
				module.blocks.clear();
				if (module.bytes.empty())
				{
					return;
				}

				std::stable_sort(module.bytes.begin(), module.bytes.end(), [](const PatchByte& left, const PatchByte& right)
				{
					if (left.rva != right.rva)
					{
						return left.rva < right.rva;
					}
					return left.order < right.order;
				});

				std::vector<PatchByte> uniqueBytes;
				for (const PatchByte& byte : module.bytes)
				{
					if (!uniqueBytes.empty() && uniqueBytes.back().rva == byte.rva)
					{
						LogMessage(LogLevel::Warn, L"BinaryPatch: duplicate RVA file=%s module=%s rva=0x%08X, use later line=%u",
							filePath.c_str(), module.moduleName.c_str(), byte.rva, byte.lineNumber);
						uniqueBytes.back() = byte;
					}
					else
					{
						uniqueBytes.push_back(byte);
					}
				}

				PatchBlock current;
				for (const PatchByte& byte : uniqueBytes)
				{
					if (current.newBytes.empty())
					{
						current.rva = byte.rva;
					}
					else if (byte.rva != current.rva + current.newBytes.size())
					{
						module.blocks.push_back(std::move(current));
						current = PatchBlock{};
						current.rva = byte.rva;
					}
					current.oldBytes.push_back(byte.oldByte);
					current.newBytes.push_back(byte.newByte);
				}
				if (!current.newBytes.empty())
				{
					module.blocks.push_back(std::move(current));
				}
			}

			static bool ParsePatchFile(const PatchFileData& file, std::vector<ModulePatch>& modules)
			{
				modules.clear();
				ModulePatch* currentModule = nullptr;
				uint32_t lineNumber = 0;
				uint32_t order = 0;
				size_t start = 0;
				while (start <= file.text.size())
				{
					size_t end = file.text.find('\n', start);
					std::string line = end == std::string::npos ? file.text.substr(start) : file.text.substr(start, end - start);
					if (!line.empty() && line.back() == '\r')
					{
						line.pop_back();
					}
					++lineNumber;

					std::string trimmed = TrimString(line);
					if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed[0] == '#')
					{
						if (end == std::string::npos) break;
						start = end + 1;
						continue;
					}

					if (trimmed[0] == '>')
					{
						std::wstring moduleName = TrimWide(Utf8OrAcpToWide(TrimString(trimmed.substr(1))));
						if (moduleName.empty())
						{
							LogMessage(LogLevel::Warn, L"BinaryPatch: empty module file=%s line=%u", file.resolvedPath.c_str(), lineNumber);
							currentModule = nullptr;
						}
						else
						{
							currentModule = FindOrAddModule(modules, moduleName);
						}
						if (end == std::string::npos) break;
						start = end + 1;
						continue;
					}

					size_t colon = trimmed.find(':');
					size_t arrow = trimmed.find("->", colon == std::string::npos ? 0 : colon + 1);
					if (colon == std::string::npos || arrow == std::string::npos || !currentModule)
					{
						LogMessage(LogLevel::Warn, L"BinaryPatch: invalid line file=%s line=%u", file.resolvedPath.c_str(), lineNumber);
						if (end == std::string::npos) break;
						start = end + 1;
						continue;
					}

					uint32_t rva = 0;
					uint8_t oldByte = 0;
					uint8_t newByte = 0;
					if (!TryParseHexUInt32(trimmed.substr(0, colon), rva)
						|| !TryParseHexByte(trimmed.substr(colon + 1, arrow - colon - 1), oldByte)
						|| !TryParseHexByte(trimmed.substr(arrow + 2), newByte))
					{
						LogMessage(LogLevel::Warn, L"BinaryPatch: parse failed file=%s line=%u", file.resolvedPath.c_str(), lineNumber);
						if (end == std::string::npos) break;
						start = end + 1;
						continue;
					}

					currentModule->bytes.push_back(PatchByte{ rva, oldByte, newByte, lineNumber, order++ });
					if (end == std::string::npos) break;
					start = end + 1;
				}

				for (ModulePatch& module : modules)
				{
					BuildPatchBlocks(module, file.resolvedPath);
				}
				return !modules.empty();
			}

			static HMODULE ResolvePatchModule(const std::wstring& moduleName)
			{
				if (EndsWithNoCase(moduleName, L".exe"))
				{
					return GetModuleHandleW(nullptr);
				}
				return GetModuleHandleW(moduleName.c_str());
			}

			static bool ReadPatchBytes(uintptr_t target, size_t size, std::vector<uint8_t>& current)
			{
				current.assign(size, 0);
				return Rut::MemX::ReadMemory(reinterpret_cast<LPVOID>(target), current.data(), current.size());
			}

			static bool VerifyOldBytes(uintptr_t target, const PatchBlock& block)
			{
				std::vector<uint8_t> current;
				if (!ReadPatchBytes(target, block.oldBytes.size(), current))
				{
					return false;
				}
				return current == block.oldBytes;
			}

			static bool IsAlreadyPatched(uintptr_t target, const PatchBlock& block)
			{
				std::vector<uint8_t> current;
				if (!ReadPatchBytes(target, block.newBytes.size(), current))
				{
					return false;
				}
				return current == block.newBytes;
			}


			static void AppendHexByte(std::wstring& text, uint8_t value)
			{
				wchar_t buf[4] = {};
				swprintf_s(buf, L"%02X", static_cast<unsigned>(value));
				text += buf;
			}

			static std::wstring FormatBytePatch(const PatchBlock& block)
			{
				std::wstring text;
				for (size_t i = 0; i < block.oldBytes.size() && i < block.newBytes.size(); ++i)
				{
					if (i > 0)
					{
						text += L" ";
					}
					AppendHexByte(text, block.oldBytes[i]);
					text += L"->";
					AppendHexByte(text, block.newBytes[i]);
				}
				return text;
			}


			static bool ApplyModulePatch(const BinaryPatchSettings& settings, const PatchFileData& file, const ModulePatch& module, ApplyStats& stats)
			{
				const wchar_t* opPrefix = (InterlockedCompareExchange(&sg_preEntryBinaryPatchThreadId, 0, 0) == static_cast<LONG>(GetCurrentThreadId())) ? L"BinaryPatch(pre-entry)" : L"BinaryPatch";
				HMODULE moduleBase = ResolvePatchModule(module.moduleName);
				if (!moduleBase)
				{
					LogMessage(LogLevel::Warn, L"%s: module not loaded, skip module=%s file=%s", opPrefix, module.moduleName.c_str(), file.resolvedPath.c_str());
					stats.skipped += static_cast<uint32_t>(module.blocks.size());
					return !settings.failOnMissingModule;
				}

				bool keepGoing = true;
				uintptr_t base = reinterpret_cast<uintptr_t>(moduleBase);
				for (const PatchBlock& block : module.blocks)
				{
					uintptr_t target = base + block.rva;
					std::wstring bytePatch = FormatBytePatch(block);
					if (settings.verifyOldBytes && !VerifyOldBytes(target, block))
					{
						if (IsAlreadyPatched(target, block))
						{
							++stats.ok;
							stats.bytes += static_cast<uint32_t>(block.newBytes.size());
							LogMessage(LogLevel::Info, L"%s: already patched module=%s rva=0x%08X bytes=%s size=%u",
								opPrefix, module.moduleName.c_str(), block.rva, bytePatch.c_str(), static_cast<uint32_t>(block.newBytes.size()));
							continue;
						}
						LogMessage(LogLevel::Warn, L"%s: old bytes mismatch/read failed module=%s rva=0x%08X bytes=%s size=%u, skip block",
							opPrefix, module.moduleName.c_str(), block.rva, bytePatch.c_str(), static_cast<uint32_t>(block.newBytes.size()));
						++stats.skipped;
						continue;
					}

					if (Rut::MemX::WriteMemory(reinterpret_cast<LPVOID>(target), block.newBytes.data(), block.newBytes.size()))
					{
						++stats.ok;
						stats.bytes += static_cast<uint32_t>(block.newBytes.size());
						if (settings.enableLog)
						{
							LogMessage(LogLevel::Info, L"%s: wrote source=%s module=%s rva=0x%08X bytes=%s size=%u",
								opPrefix, file.source.c_str(), module.moduleName.c_str(), block.rva, bytePatch.c_str(), static_cast<uint32_t>(block.newBytes.size()));
						}
					}
					else
					{
						++stats.failed;
						LogMessage(LogLevel::Error, L"%s: write failed module=%s rva=0x%08X bytes=%s size=%u",
							opPrefix, module.moduleName.c_str(), block.rva, bytePatch.c_str(), static_cast<uint32_t>(block.newBytes.size()));
						if (settings.failOnWriteError)
						{
							keepGoing = false;
							break;
						}
					}
				}
				return keepGoing;
			}

			static void ClearBinaryPatchHwbp(CONTEXT* context)
			{
				if (!context)
				{
					return;
				}
				context->Dr0 = 0;
				context->Dr7 &= ~static_cast<DWORD_PTR>(0x000F0003);
				context->Dr6 &= ~static_cast<DWORD_PTR>(0x1);
				context->EFlags |= (1u << 16);
			}

			static LONG CALLBACK BinaryPatchHwbpVehHandler(PEXCEPTION_POINTERS exceptionInfo)
			{
				if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
				{
					return EXCEPTION_CONTINUE_SEARCH;
				}

				EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
				CONTEXT* context = exceptionInfo->ContextRecord;
				if (record->ExceptionCode == kBinaryPatchSetHwbpException)
				{
					if (record->NumberParameters < 1 || record->ExceptionInformation[0] == 0)
					{
						return EXCEPTION_CONTINUE_EXECUTION;
					}
					DWORD_PTR target = static_cast<DWORD_PTR>(record->ExceptionInformation[0]);
					context->Dr0 = target;
					context->Dr6 = 0;
					context->Dr7 &= ~static_cast<DWORD_PTR>(0x000F0003);
					context->Dr7 |= 0x1; // L0 execute breakpoint, 1 byte
					LogMessage(LogLevel::Info, L"BinaryPatch(HWBP): set DR0 execute breakpoint at 0x%p", reinterpret_cast<void*>(target));
					return EXCEPTION_CONTINUE_EXECUTION;
				}

				if (record->ExceptionCode != EXCEPTION_SINGLE_STEP)
				{
					return EXCEPTION_CONTINUE_SEARCH;
				}
				if (InterlockedCompareExchange(&sg_hwbpBinaryPatchInstalled, 0, 0) == 0)
				{
					return EXCEPTION_CONTINUE_SEARCH;
				}
				if ((context->Dr6 & 0x1) == 0 || static_cast<uintptr_t>(context->Dr0) != sg_hwbpBinaryPatchTarget)
				{
					return EXCEPTION_CONTINUE_SEARCH;
				}
				if (InterlockedExchange(&sg_hwbpBinaryPatchApplying, 1) != 0)
				{
					ClearBinaryPatchHwbp(context);
					return EXCEPTION_CONTINUE_EXECUTION;
				}

				ClearBinaryPatchHwbp(context);
				InterlockedExchange(&sg_hwbpBinaryPatchInstalled, 0);
				LogMessage(LogLevel::Info, L"BinaryPatch(HWBP): hit patch address 0x%p, apply .1337", reinterpret_cast<void*>(sg_hwbpBinaryPatchTarget));
				try
				{
					ApplyBinaryPatches(sg_hwbpBinaryPatchSettings);
				}
				catch (const std::exception& err)
				{
					std::wstring wideError = Utf8OrAcpToWide(err.what());
					LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): apply exception: %s", wideError.c_str());
				}
				catch (...)
				{
					LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): unknown apply exception");
				}
				InterlockedExchange(&sg_hwbpBinaryPatchApplying, 0);
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			static bool ResolveHwbpPatchTarget(const BinaryPatchSettings& settings, uintptr_t& target, std::wstring& moduleName, uint32_t& rva)
			{
				target = 0;
				moduleName.clear();
				rva = 0;
				if (!settings.enable)
				{
					return false;
				}
				if (!settings.hwbpModule.empty() && settings.hwbpRva != 0)
				{
					HMODULE moduleBase = ResolvePatchModule(settings.hwbpModule);
					if (!moduleBase)
					{
						return false;
					}
					moduleName = settings.hwbpModule;
					rva = settings.hwbpRva;
					target = reinterpret_cast<uintptr_t>(moduleBase) + rva;
					return true;
				}
				if (settings.patchFiles.empty())
				{
					return false;
				}

				std::wstring gameDir = GetGameDirectory();
				for (const std::wstring& configuredPath : settings.patchFiles)
				{
					PatchFileData file;
					if (!LoadPatchFileText(gameDir, configuredPath, settings.preferCustomPak, file))
					{
						continue;
					}
					std::vector<ModulePatch> modules;
					if (!ParsePatchFile(file, modules))
					{
						continue;
					}
					for (const ModulePatch& module : modules)
					{
						if (module.blocks.empty())
						{
							continue;
						}
						HMODULE moduleBase = ResolvePatchModule(module.moduleName);
						if (!moduleBase)
						{
							continue;
						}
						moduleName = module.moduleName;
						rva = module.blocks.front().rva;
						target = reinterpret_cast<uintptr_t>(moduleBase) + rva;
						return true;
					}
				}
				return false;
			}

		}

		void ApplyBinaryPatches(const BinaryPatchSettings& settings)
		{
			const LONG activePreEntryThreadId = InterlockedCompareExchange(&sg_preEntryBinaryPatchThreadId, 0, 0);
			const bool preEntryActive = activePreEntryThreadId == static_cast<LONG>(GetCurrentThreadId());
			if (!preEntryActive && activePreEntryThreadId != 0)
			{
				HANDLE doneEvent = sg_preEntryBinaryPatchDoneEvent;
				LogMessage(LogLevel::Info, L"BinaryPatch: waiting for active pre-entry attempt to finish");
				if (doneEvent)
				{
					DWORD waitResult = WaitForSingleObject(doneEvent, 10000);
					if (waitResult == WAIT_OBJECT_0)
					{
						if (InterlockedCompareExchange(&sg_preEntryBinaryPatchApplied, 0, 0) != 0)
						{
							LogMessage(LogLevel::Info, L"BinaryPatch: skipped runtime because pre-entry already applied after wait");
							return;
						}
						LogMessage(LogLevel::Warn, L"BinaryPatch: pre-entry finished without success, fallback to runtime");
					}
					else
					{
						LogMessage(LogLevel::Warn, L"BinaryPatch: pre-entry wait failed/timeout (result=%lu), fallback to runtime", waitResult);
					}
				}
			}
			if (!preEntryActive && InterlockedCompareExchange(&sg_preEntryBinaryPatchApplied, 0, 0) != 0)
			{
				LogMessage(LogLevel::Info, L"BinaryPatch: skipped runtime because pre-entry already applied");
				return;
			}

			const wchar_t* logPrefix = preEntryActive ? L"BinaryPatch(pre-entry)" : L"ApplyBinaryPatches";
			if (settings.enableHwbp && InterlockedCompareExchange(&sg_hwbpBinaryPatchApplying, 0, 0) == 0)
			{
				if (InterlockedCompareExchange(&sg_hwbpBinaryPatchInstalled, 0, 0) != 0)
				{
					LogMessage(LogLevel::Info, L"%s: defer .1337 until HWBP target is hit", logPrefix);
					return;
				}
			}
			if (!settings.enable)
			{
				if (preEntryActive)
				{
					LogMessage(LogLevel::Debug, L"%s: disabled", logPrefix);
				}
				return;
			}
			if (settings.patchFiles.empty())
			{
				LogMessage(LogLevel::Warn, L"%s: enabled but no patch files configured", logPrefix);
				return;
			}

			std::wstring gameDir = GetGameDirectory();
			ApplyStats stats = {};
			LogMessage(LogLevel::Info, L"%s: files=%u verifyOldBytes=%d preferCustomPak=%d",
				logPrefix, static_cast<uint32_t>(settings.patchFiles.size()), settings.verifyOldBytes ? 1 : 0, settings.preferCustomPak ? 1 : 0);

			bool keepGoing = true;
			for (const std::wstring& configuredPath : settings.patchFiles)
			{
				PatchFileData file;
				if (!LoadPatchFileText(gameDir, configuredPath, settings.preferCustomPak, file))
				{
					LogMessage(LogLevel::Error, L"%s: file missing or unreadable: %s", logPrefix, ToGameAbsolutePath(gameDir, configuredPath).c_str());
					++stats.failed;
					continue;
				}
				++stats.files;
				if (settings.enableLog)
				{
					LogMessage(LogLevel::Info, L"%s: loaded file=%s source=%s size=%u",
						logPrefix, file.resolvedPath.c_str(), file.source.c_str(), static_cast<uint32_t>(file.text.size()));
				}

				std::vector<ModulePatch> modules;
				if (!ParsePatchFile(file, modules))
				{
					LogMessage(LogLevel::Warn, L"%s: no valid patches in file=%s", logPrefix, file.resolvedPath.c_str());
					++stats.skipped;
					continue;
				}

				stats.modules += static_cast<uint32_t>(modules.size());
				for (const ModulePatch& module : modules)
				{
					stats.blocks += static_cast<uint32_t>(module.blocks.size());
					keepGoing = ApplyModulePatch(settings, file, module, stats);
					if (!keepGoing)
					{
						break;
					}
				}
				if (!keepGoing)
				{
					break;
				}
			}

			LogMessage(stats.failed > 0 ? LogLevel::Warn : LogLevel::Info,
				L"%s: files=%u modules=%u blocks=%u bytes=%u ok=%u failed=%u skipped=%u",
				logPrefix, stats.files, stats.modules, stats.blocks, stats.bytes, stats.ok, stats.failed, stats.skipped);
			if (preEntryActive && stats.ok > 0)
			{
				InterlockedExchange(&sg_preEntryBinaryPatchOk, 1);
			}
		}

		void TryApplyBinaryPatchesBeforeEntry(const BinaryPatchSettings& settings)
		{
			if (InterlockedCompareExchange(&sg_preEntryBinaryPatchApplied, 0, 0) != 0)
			{
				LogMessage(LogLevel::Info, L"BinaryPatch(pre-entry): already applied, skip duplicate attempt");
				return;
			}

			LogMessage(LogLevel::Info, L"BinaryPatch(pre-entry): begin");
			if (!sg_preEntryBinaryPatchDoneEvent)
			{
				sg_preEntryBinaryPatchDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			}
			if (sg_preEntryBinaryPatchDoneEvent)
			{
				ResetEvent(sg_preEntryBinaryPatchDoneEvent);
			}
			InterlockedExchange(&sg_preEntryBinaryPatchOk, 0);
			InterlockedExchange(&sg_preEntryBinaryPatchActive, 1);
			InterlockedExchange(&sg_preEntryBinaryPatchThreadId, static_cast<LONG>(GetCurrentThreadId()));
			try
			{
				ApplyBinaryPatches(settings);
			}
			catch (const std::exception& err)
			{
				InterlockedExchange(&sg_preEntryBinaryPatchThreadId, 0);
				InterlockedExchange(&sg_preEntryBinaryPatchActive, 0);
				if (sg_preEntryBinaryPatchDoneEvent)
				{
					SetEvent(sg_preEntryBinaryPatchDoneEvent);
				}
				std::wstring wideError = Utf8OrAcpToWide(err.what());
				LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): exception, fallback to runtime: %s", wideError.c_str());
				return;
			}
			catch (...)
			{
				InterlockedExchange(&sg_preEntryBinaryPatchThreadId, 0);
				InterlockedExchange(&sg_preEntryBinaryPatchActive, 0);
				if (sg_preEntryBinaryPatchDoneEvent)
				{
					SetEvent(sg_preEntryBinaryPatchDoneEvent);
				}
				LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): unknown exception, fallback to runtime");
				return;
			}
			InterlockedExchange(&sg_preEntryBinaryPatchThreadId, 0);
			InterlockedExchange(&sg_preEntryBinaryPatchActive, 0);
			if (sg_preEntryBinaryPatchDoneEvent)
			{
				SetEvent(sg_preEntryBinaryPatchDoneEvent);
			}

			if (InterlockedCompareExchange(&sg_preEntryBinaryPatchOk, 0, 0) != 0)
			{
				InterlockedExchange(&sg_preEntryBinaryPatchApplied, 1);
				LogMessage(LogLevel::Info, L"BinaryPatch(pre-entry): applied before entry, runtime duplicate will be skipped");
			}
			else
			{
				LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): no block applied, fallback to runtime");
			}
			}

		bool IsBinaryPatchPreEntryApplied()
		{
			return InterlockedCompareExchange(&sg_preEntryBinaryPatchApplied, 0, 0) != 0;
		}

		void TryRequestBinaryPatchOnFirstPatchHit(const BinaryPatchSettings& settings)
		{
			if (InterlockedCompareExchange(&sg_preEntryBinaryPatchApplied, 0, 0) != 0)
			{
				LogMessage(LogLevel::Info, L"BinaryPatch(HWBP): skip install because pre-entry already applied");
				return;
			}
			if (InterlockedCompareExchange(&sg_hwbpBinaryPatchInstalled, 0, 0) != 0)
			{
				return;
			}

			uintptr_t target = 0;
			std::wstring moduleName;
			uint32_t rva = 0;
			if (!ResolveHwbpPatchTarget(settings, target, moduleName, rva))
			{
				LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): cannot resolve patch target, skip HWBP");
				return;
			}

			sg_hwbpBinaryPatchSettings = settings;
			sg_hwbpBinaryPatchTarget = target;
			if (!sg_hwbpBinaryPatchVeh)
			{
				sg_hwbpBinaryPatchVeh = AddVectoredExceptionHandler(1, BinaryPatchHwbpVehHandler);
				if (!sg_hwbpBinaryPatchVeh)
				{
					LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): AddVectoredExceptionHandler failed, skip HWBP");
					return;
				}
			}

			ULONG_PTR args[1] = { static_cast<ULONG_PTR>(target) };
			InterlockedExchange(&sg_hwbpBinaryPatchInstalled, 1);
			LogMessage(LogLevel::Info, L"BinaryPatch(HWBP): request patch execute breakpoint module=%s rva=0x%08X addr=0x%p",
				moduleName.c_str(), rva, reinterpret_cast<void*>(target));
			RaiseException(kBinaryPatchSetHwbpException, 0, 1, args);
		}
	}
}
