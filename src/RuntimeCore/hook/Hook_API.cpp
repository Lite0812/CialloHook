#include "Hook_API.h"
#include "Hook.h"
#include "Hook_API_DEF.h"
#include "CustomPakVFS.h"

#include <Windows.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <intrin.h>
#include <share.h>
#include <shlobj.h>

namespace Rut
{
	namespace HookX
	{
		static bool sg_loggerInitialized = false;
		static bool sg_loggerEnableDebug = false;
		static bool sg_loggerEnableFile = false;
		static bool sg_loggerEnableConsole = false;
		static std::wstring sg_loggerModuleName = L"HookFont";
		static std::wstring sg_loggerFilePath;
		static FILE* sg_loggerFile = nullptr;
		static HANDLE sg_loggerExternalConsoleProcess = nullptr;
		static CRITICAL_SECTION sg_loggerLock;
		static INIT_ONCE sg_loggerLockInitOnce = INIT_ONCE_STATIC_INIT;
		static volatile LONG sg_hookRuntimeShuttingDown = 0;

		static BOOL CALLBACK InitLoggerLockOnce(PINIT_ONCE initOnce, PVOID parameter, PVOID* context)
		{
			UNREFERENCED_PARAMETER(initOnce);
			UNREFERENCED_PARAMETER(parameter);
			UNREFERENCED_PARAMETER(context);
			InitializeCriticalSection(&sg_loggerLock);
			return TRUE;
		}

		static void EnsureLoggerLock()
		{
			InitOnceExecuteOnce(&sg_loggerLockInitOnce, InitLoggerLockOnce, nullptr, nullptr);
		}

		static std::wstring AnsiToWide(const char* text)
		{
			if (!text)
			{
				return L"";
			}

			int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
			if (len > 0)
			{
				std::wstring result(len, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], len);
				if (!result.empty() && result.back() == L'\0')
				{
					result.pop_back();
				}
				return result;
			}

			len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
			if (len <= 0)
			{
				return L"";
			}

			std::wstring result(len, L'\0');
			MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], len);
			if (!result.empty() && result.back() == L'\0')
			{
				result.pop_back();
			}
			return result;
		}

		static std::string WideToUtf8(const std::wstring& text)
		{
			if (text.empty())
			{
				return "";
			}

			int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (len <= 0)
			{
				return "";
			}

			std::string result(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], len, nullptr, nullptr);
			if (!result.empty() && result.back() == '\0')
			{
				result.pop_back();
			}
			return result;
		}

		static std::wstring EscapePowerShellSingleQuoted(const std::wstring& text)
		{
			std::wstring escaped;
			escaped.reserve(text.size());
			for (wchar_t c : text)
			{
				if (c == L'\'')
				{
					escaped += L"''";
				}
				else
				{
					escaped.push_back(c);
				}
			}
			return escaped;
		}

		static std::wstring NormalizeSlashes(std::wstring value)
		{
			for (wchar_t& c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			return value;
		}

		static std::wstring TrimTrailingSlashes(std::wstring value)
		{
			while (value.size() > 3 && !value.empty() && (value.back() == L'\\' || value.back() == L'/'))
			{
				value.pop_back();
			}
			return value;
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
			wchar_t last = left.back();
			if (last == L'\\' || last == L'/')
			{
				return left + right;
			}
			return left + L"\\" + right;
		}

		static std::wstring GetParentPathNoSlash(const std::wstring& value)
		{
			std::wstring normalized = TrimTrailingSlashes(NormalizeSlashes(value));
			size_t pos = normalized.find_last_of(L"\\/");
			if (pos == std::wstring::npos)
			{
				return L"";
			}
			if (pos == 2 && normalized.size() >= 2 && normalized[1] == L':')
			{
				return normalized.substr(0, pos + 1);
			}
			return normalized.substr(0, pos);
		}

		static bool EnsureDirectoryChain(const std::wstring& path)
		{
			if (path.empty())
			{
				return false;
			}
			std::wstring normalized = TrimTrailingSlashes(NormalizeSlashes(path));
			if (normalized.empty())
			{
				return false;
			}
			DWORD attr = GetFileAttributesW(normalized.c_str());
			if (attr != INVALID_FILE_ATTRIBUTES)
			{
				return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
			}
			std::wstring parent = GetParentPathNoSlash(normalized);
			if (!parent.empty() && parent != normalized && !EnsureDirectoryChain(parent))
			{
				return false;
			}
			if (CreateDirectoryW(normalized.c_str(), nullptr))
			{
				return true;
			}
			if (GetLastError() != ERROR_ALREADY_EXISTS)
			{
				return false;
			}
			attr = GetFileAttributesW(normalized.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		std::wstring GetRuntimeCacheRoot(const wchar_t* fallbackBaseDir)
		{
			wchar_t tempDir[MAX_PATH] = {};
			if (GetTempPathW(MAX_PATH, tempDir) != 0 && tempDir[0] != L'\0')
			{
				std::wstring tempRoot = TrimTrailingSlashes(NormalizeSlashes(tempDir));
				std::wstring tempCacheRoot = JoinPath(JoinPath(tempRoot, L"CialloHook"), L"RuntimeCache");
				if (EnsureDirectoryChain(tempCacheRoot))
				{
					return tempCacheRoot;
				}
			}

			std::wstring fallbackRoot = fallbackBaseDir ? TrimTrailingSlashes(NormalizeSlashes(fallbackBaseDir)) : L"";
			if (fallbackRoot.empty())
			{
				return L"";
			}

			std::wstring patchCacheRoot = JoinPath(JoinPath(fallbackRoot, L"patch"), L"_ciallohook_runtime_cache");
			if (EnsureDirectoryChain(patchCacheRoot))
			{
				return patchCacheRoot;
			}

			std::wstring directCacheRoot = JoinPath(fallbackRoot, L"_ciallohook_runtime_cache");
			if (EnsureDirectoryChain(directCacheRoot))
			{
				return directCacheRoot;
			}

			return L"";
		}

		std::wstring GetRuntimeCacheDir(const wchar_t* fallbackBaseDir, const wchar_t* relativePath)
		{
			std::wstring root = GetRuntimeCacheRoot(fallbackBaseDir);
			if (root.empty())
			{
				return L"";
			}
			std::wstring relative = relativePath ? NormalizeSlashes(relativePath) : L"";
			while (!relative.empty() && (relative[0] == L'\\' || relative[0] == L'/' || relative[0] == L'.'))
			{
				relative.erase(relative.begin());
			}
			std::wstring fullPath = relative.empty() ? root : JoinPath(root, relative);
			if (!EnsureDirectoryChain(fullPath))
			{
				return L"";
			}
			return fullPath;
		}

		bool IsManagedRuntimeCachePath(const wchar_t* path)
		{
			if (!path || path[0] == L'\0')
			{
				return false;
			}
			std::wstring lower = NormalizeSlashes(path);
			std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
			return lower.find(L"\\ciallohook\\runtimecache") != std::wstring::npos
				|| lower.find(L"\\_ciallohook_runtime_cache") != std::wstring::npos
				|| lower.find(L"\\ciallohook_vfs_cache") != std::wstring::npos
				|| lower.find(L"\\_ciallohook_vfs_cache") != std::wstring::npos;
		}

		bool RemoveDirectoryTreeIfEmpty(const wchar_t* path, uint32_t maxLevels)
		{
			if (!path || path[0] == L'\0' || maxLevels == 0)
			{
				return false;
			}
			std::wstring current = TrimTrailingSlashes(NormalizeSlashes(path));
			bool removedAny = false;
			for (uint32_t level = 0; level < maxLevels; ++level)
			{
				if (current.empty() || !IsManagedRuntimeCachePath(current.c_str()))
				{
					break;
				}
				DWORD attr = GetFileAttributesW(current.c_str());
				if (attr == INVALID_FILE_ATTRIBUTES)
				{
					current = GetParentPathNoSlash(current);
					continue;
				}
				if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					break;
				}
				SetFileAttributesW(current.c_str(), FILE_ATTRIBUTE_NORMAL);
				if (!RemoveDirectoryW(current.c_str()))
				{
					break;
				}
				removedAny = true;
				current = GetParentPathNoSlash(current);
			}
			return removedAny;
		}

		static void LaunchExternalLogConsole(const std::wstring& logPath, const std::wstring& moduleName)
		{
			if (logPath.empty())
			{
				return;
			}

			if (sg_loggerExternalConsoleProcess)
			{
				DWORD waitResult = WaitForSingleObject(sg_loggerExternalConsoleProcess, 0);
				if (waitResult == WAIT_TIMEOUT)
				{
					return;
				}
				CloseHandle(sg_loggerExternalConsoleProcess);
				sg_loggerExternalConsoleProcess = nullptr;
			}

			std::wstring psPath = EscapePowerShellSingleQuoted(logPath);
			std::wstring psTitle = EscapePowerShellSingleQuoted(moduleName + L" Console Log");
			wchar_t pidTagBuf[64] = {};
			swprintf_s(pidTagBuf, L"[P%lu:", GetCurrentProcessId());
			std::wstring psPidTag = EscapePowerShellSingleQuoted(pidTagBuf);
			wchar_t targetPidBuf[32] = {};
			swprintf_s(targetPidBuf, L"%lu", GetCurrentProcessId());
			
			wchar_t sysDir[MAX_PATH] = {};
			GetSystemDirectoryW(sysDir, MAX_PATH);
			std::wstring psExe = std::wstring(sysDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
			std::wstring command = L"\"";
			command += psExe;
			command += L"\" -NoProfile -NoLogo -ExecutionPolicy Bypass -Command \"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; $Host.UI.RawUI.WindowTitle = '";
			command += psTitle;
			command += L"'; if (-not (Test-Path -LiteralPath '";
			command += psPath;
			command += L"')) { New-Item -ItemType File -Force -Path '";
			command += psPath;
			command += L"' | Out-Null }; $pidTag = '";
			command += psPidTag;
			command += L"'; $targetPid = ";
			command += targetPidBuf;
			command += L"; $fs = [System.IO.File]::Open('";
			command += psPath;
			command += L"',[System.IO.FileMode]::OpenOrCreate,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite); try { $sr = New-Object System.IO.StreamReader($fs,[System.Text.Encoding]::UTF8,$true); Write-Host 'Replaying current process log history, then following new lines...' -ForegroundColor DarkGray; while ((Get-Process -Id $targetPid -ErrorAction SilentlyContinue) -ne $null) { while (-not $sr.EndOfStream) { $line = $sr.ReadLine(); if ($line.IndexOf($pidTag) -lt 0) { continue }; if ($line -match '\\[ERROR\\]') { Write-Host $line -ForegroundColor Red } elseif ($line -match '\\[WARN \\]') { Write-Host $line -ForegroundColor Yellow } elseif ($line -match '\\[DEBUG\\]') { Write-Host $line -ForegroundColor Green } elseif ($line -match '\\[INFO \\]') { Write-Host $line -ForegroundColor Gray } else { Write-Host $line } }; Start-Sleep -Milliseconds 200 }; while (-not $sr.EndOfStream) { $line = $sr.ReadLine(); if ($line.IndexOf($pidTag) -lt 0) { continue }; if ($line -match '\\[ERROR\\]') { Write-Host $line -ForegroundColor Red } elseif ($line -match '\\[WARN \\]') { Write-Host $line -ForegroundColor Yellow } elseif ($line -match '\\[DEBUG\\]') { Write-Host $line -ForegroundColor Green } elseif ($line -match '\\[INFO \\]') { Write-Host $line -ForegroundColor Gray } else { Write-Host $line } } } finally { if ($sr) { $sr.Dispose() }; $fs.Dispose() }\"";

			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi = {};
			std::vector<wchar_t> cmdBuf(command.begin(), command.end());
			cmdBuf.push_back(L'\0');
			if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
			{
				CloseHandle(pi.hThread);
				sg_loggerExternalConsoleProcess = pi.hProcess;
			}
		}

		int ShowExternalStartupConsentDialog(const wchar_t* title, const wchar_t* body)
		{
			std::wstring safeTitle = title ? title : L"CialloHook";
			std::wstring safeBody = body ? body : L"";
			if (safeBody.empty())
			{
				return IDYES;
			}

			LogMessage(LogLevel::Info, L"ShowExternalStartupConsentDialog: using direct MessageBoxW");
			return MessageBoxW(nullptr, safeBody.c_str(), safeTitle.c_str(),
				MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
		}

		static const wchar_t* GetLogLevelName(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Debug:
				return L"DEBUG";
			case LogLevel::Warn:
				return L"WARN ";
			case LogLevel::Error:
				return L"ERROR";
			default:
				return L"INFO ";
			}
		}

		static WORD GetLogColor(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Warn:
				return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			case LogLevel::Error:
				return FOREGROUND_RED | FOREGROUND_INTENSITY;
			case LogLevel::Debug:
				return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			default:
				return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			}
		}

		static bool ShouldEmitLog(LogLevel level)
		{
			if (!sg_loggerInitialized)
			{
				return false;
			}
			if (level == LogLevel::Debug && !sg_loggerEnableDebug)
			{
				return false;
			}
			return true;
		}

		static void EmitLogLine(LogLevel level, const wchar_t* message)
		{
			if (!ShouldEmitLog(level))
			{
				return;
			}

			SYSTEMTIME st;
			GetLocalTime(&st);

			wchar_t prefix[128] = {};
			swprintf_s(prefix, L"[%02d:%02d:%02d.%03d][P%lu:T%lu][%s][%s] ",
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
				GetCurrentProcessId(), GetCurrentThreadId(),
				sg_loggerModuleName.c_str(), GetLogLevelName(level));

			std::wstring line = prefix;
			line += message ? message : L"";
			line += L"\r\n";

			OutputDebugStringW(line.c_str());

			if (sg_loggerEnableFile && sg_loggerFile)
			{
				std::string utf8 = WideToUtf8(line);
				if (!utf8.empty())
				{
					fwrite(utf8.data(), 1, utf8.size(), sg_loggerFile);
					fflush(sg_loggerFile);
				}
			}

			if (sg_loggerEnableConsole)
			{
				HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
				if (hConsole && hConsole != INVALID_HANDLE_VALUE)
				{
					CONSOLE_SCREEN_BUFFER_INFO info = {};
					WORD oldAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
					if (GetConsoleScreenBufferInfo(hConsole, &info))
					{
						oldAttr = info.wAttributes;
					}
					SetConsoleTextAttribute(hConsole, GetLogColor(level));
					DWORD written = 0;
					WriteConsoleW(hConsole, line.c_str(), (DWORD)line.size(), &written, nullptr);
					SetConsoleTextAttribute(hConsole, oldAttr);
				}
			}
		}

		void InitLogger(const wchar_t* moduleName, bool enableDebug, bool enableFile, bool enableConsole)
		{
			EnsureLoggerLock();
			EnterCriticalSection(&sg_loggerLock);

			if (sg_loggerFile)
			{
				fclose(sg_loggerFile);
				sg_loggerFile = nullptr;
			}

			sg_loggerEnableDebug = enableDebug;
			sg_loggerEnableFile = enableFile;
			sg_loggerEnableConsole = enableConsole;
			if (!sg_loggerEnableDebug)
			{
				sg_loggerEnableFile = false;
				sg_loggerEnableConsole = false;
			}
			sg_loggerModuleName = (moduleName && moduleName[0] != L'\0') ? moduleName : L"HookFont";
			sg_loggerInitialized = true;

			bool needExternalConsole = false;
			if (sg_loggerEnableConsole)
			{
				needExternalConsole = true;
				sg_loggerEnableConsole = false;
				sg_loggerEnableFile = true;
			}

			if (sg_loggerEnableFile)
			{
				wchar_t exePath[MAX_PATH] = {};
				GetModuleFileNameW(nullptr, exePath, MAX_PATH);
				std::wstring logDir = exePath;
				size_t pos = logDir.find_last_of(L"\\/");
				if (pos != std::wstring::npos)
				{
					logDir = logDir.substr(0, pos + 1);
				}
				else
				{
					logDir.clear();
				}

				sg_loggerFilePath = logDir + sg_loggerModuleName + L".log";
				sg_loggerFile = _wfsopen(sg_loggerFilePath.c_str(), L"a+b", _SH_DENYNO);
				if (sg_loggerFile)
				{
					fseek(sg_loggerFile, 0, SEEK_END);
					long fileSize = ftell(sg_loggerFile);
					if (fileSize <= 0)
					{
						unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
						fwrite(bom, 1, sizeof(bom), sg_loggerFile);
					}
					fflush(sg_loggerFile);
				}
			}

			if (needExternalConsole && sg_loggerEnableFile)
			{
				LaunchExternalLogConsole(sg_loggerFilePath, sg_loggerModuleName);
			}

			LeaveCriticalSection(&sg_loggerLock);

			EmitLogLine(LogLevel::Info, L"Logger initialized");
			if (!sg_loggerFilePath.empty())
			{
				LogMessage(LogLevel::Info, L"Log file: %s", sg_loggerFilePath.c_str());
			}
		}

		void ShutdownLogger()
		{
			EnsureLoggerLock();
			EnterCriticalSection(&sg_loggerLock);
			sg_loggerInitialized = false;
			sg_loggerEnableDebug = false;
			sg_loggerEnableFile = false;
			sg_loggerEnableConsole = false;
			if (sg_loggerFile)
			{
				fclose(sg_loggerFile);
				sg_loggerFile = nullptr;
			}
			if (sg_loggerExternalConsoleProcess)
			{
				CloseHandle(sg_loggerExternalConsoleProcess);
				sg_loggerExternalConsoleProcess = nullptr;
			}
			LeaveCriticalSection(&sg_loggerLock);
		}

		void LogMessage(LogLevel level, const wchar_t* format, ...)
		{
			if (!ShouldEmitLog(level))
			{
				return;
			}

			wchar_t buffer[4096] = {};
			va_list args;
			va_start(args, format);
			_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
			va_end(args);

			EnsureLoggerLock();
			EnterCriticalSection(&sg_loggerLock);
			EmitLogLine(level, buffer);
			LeaveCriticalSection(&sg_loggerLock);
		}

		void LogMessageA(LogLevel level, const char* format, ...)
		{
			if (!ShouldEmitLog(level))
			{
				return;
			}

			char buffer[4096] = {};
			va_list args;
			va_start(args, format);
			vsnprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
			va_end(args);

			std::wstring wide = AnsiToWide(buffer);

			EnsureLoggerLock();
			EnterCriticalSection(&sg_loggerLock);
			EmitLogLine(level, wide.c_str());
			LeaveCriticalSection(&sg_loggerLock);
		}

		void SetHookRuntimeShuttingDown(bool shuttingDown)
		{
			InterlockedExchange(&sg_hookRuntimeShuttingDown, shuttingDown ? 1 : 0);
		}

		bool IsHookRuntimeShuttingDown()
		{
			return InterlockedCompareExchange(&sg_hookRuntimeShuttingDown, 0, 0) != 0;
		}


#include "hook_api/font_hooks.inl"
#include "hook_api/text_hooks.inl"
#include "hook_api/window_hooks.inl"
#include "hook_api/file_hooks.inl"
#include "hook_api/siglus_hooks.inl"
#include "hook_api/registry_hooks.inl"
#include "hook_api/codepage_hooks.inl"

    }
}
