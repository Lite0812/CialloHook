#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace CialloLauncher
{
	#pragma pack(push, 1)
	struct TIME_FIELDS
	{
		uint16_t Year;
		uint16_t Month;
		uint16_t Day;
		uint16_t Hour;
		uint16_t Minute;
		uint16_t Second;
		uint16_t Milliseconds;
		uint16_t Weekday;
	};

	struct RTL_TIME_ZONE_INFORMATION
	{
		int32_t Bias;
		uint8_t StandardName[64];
		TIME_FIELDS StandardDate;
		int32_t StandardBias;
		uint8_t DaylightName[64];
		TIME_FIELDS DaylightDate;
		int32_t DaylightBias;
	};

	struct LEB
	{
		uint32_t AnsiCodePage;
		uint32_t OemCodePage;
		uint32_t LocaleID;
		uint32_t DefaultCharset;
		uint32_t HookUILanguageAPI;
		uint8_t DefaultFaceName[64];
		RTL_TIME_ZONE_INFORMATION Timezone;
	};

	struct ML_PROCESS_INFORMATION : PROCESS_INFORMATION
	{
		void* FirstCallLdrLoadDll;
	};
	#pragma pack(pop)

	using PFN_LeCreateProcess = uint32_t(WINAPI*)(
		const void* leb,
		const wchar_t* applicationName,
		const wchar_t* commandLine,
		const wchar_t* currentDirectory,
		uint32_t creationFlags,
		STARTUPINFOW* startupInfo,
		ML_PROCESS_INFORMATION* processInfo,
		void* processAttributes,
		void* threadAttributes,
		void* environment,
		void* token);

	struct LauncherStartupMessage
	{
		bool enable = false;
		std::wstring title = L"CialloHook";
		std::wstring body;
	};

	struct LauncherConfig
	{
		std::wstring iniPath;
		std::wstring launcherSection;
		std::wstring targetExe;
		std::wstring warningMessage;
		uint32_t targetDllCount = 0;
		bool debugMode = false;
		bool logToFile = false;
		bool logToConsole = false;
		bool customPakEnable = false;
		bool enableLocaleEmulator = false;
		LEB localeEmulatorBlock = {};
		LauncherStartupMessage startupMessage;
		std::vector<std::wstring> customPakFiles;
		std::vector<std::wstring> patchFolders;
		std::vector<std::wstring> targetDllNames;
	};
}
