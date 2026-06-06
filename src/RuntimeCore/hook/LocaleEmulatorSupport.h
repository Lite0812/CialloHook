#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Rut
{
	namespace HookX
	{
		struct LocaleEmulatorLoaderOptions
		{
			std::wstring tempDir;
			std::wstring patchBaseDir;
			const std::vector<std::wstring>* patchFolders = nullptr;
			const wchar_t* logPrefix = L"LocaleEmulator";
			bool preferLocalLoader = false;
		};

		struct LocaleEmulatorFileStageOptions
		{
			const wchar_t* logPrefix = L"LocaleEmulator";
			bool logSkipIfTargetExists = false;
		};

		void AppendUniquePath(std::vector<std::wstring>& paths, const std::wstring& path);
		void CleanupPreparedRuntimeFiles(const std::vector<std::wstring>& paths);
		std::wstring GetEnvironmentVariableString(const wchar_t* name, bool& exists);
		std::wstring BuildDelimitedEnvironmentValue(const std::vector<std::wstring>& paths);
		void StageLocaleEmulatorFilesNextToExe(
			const std::vector<std::wstring>& sourcePaths,
			const std::wstring& exeDir,
			const LocaleEmulatorFileStageOptions& options,
			std::vector<std::wstring>& stagedPaths);
		std::wstring BuildPrependedPathValue(
			const std::vector<std::wstring>& directories,
			const std::wstring& originalPath);
		bool BuildEnvironmentBlockWithPath(const std::wstring& pathValue, std::vector<wchar_t>& environmentBlock);
		bool PrependDirectoriesToPath(
			const std::vector<std::wstring>& directories,
			std::wstring& originalPath,
			bool& originalPathExists);
		void RestorePathEnvironment(const std::wstring& originalPath, bool originalPathExists);
		bool CopyFileToRuntimeTemp(
			const std::wstring& sourcePath,
			const std::wstring& tempDir,
			const wchar_t* logPrefix,
			std::wstring& copiedPath);
		bool ExtractCustomPakFileToTemp(
			const std::wstring& filePath,
			const std::wstring& tempDir,
			const wchar_t* logPrefix,
			std::wstring& extractedPath,
			bool& foundCandidate);
		bool PrepareLocaleEmulatorDependency(
			const LocaleEmulatorLoaderOptions& options,
			const wchar_t* fileName,
			std::wstring& preparedPath,
			bool& fromCustomPak,
			bool& foundCandidate);
		bool PopulateLocaleEmulatorTimeZone(
			const std::wstring& timezone,
			wchar_t* standardName,
			size_t standardNameCount,
			wchar_t* daylightName,
			size_t daylightNameCount,
			int32_t& bias,
			int32_t& standardBias,
			int32_t& daylightBias);
		bool TryResolveLocalLocaleEmulatorDependency(
			const std::wstring& baseDir,
			const wchar_t* fileName,
			std::wstring& resolvedPath);
		const wchar_t* GetLepLoaderFileName();
		const wchar_t* GetLepCoreFileName();
		const wchar_t* GetLeLoaderFileName();
		const wchar_t* GetLeCoreFileName();

		HMODULE LoadLocaleEmulatorLoaderModule(
			const LocaleEmulatorLoaderOptions& options,
			bool& usedConfiguredOverride,
			bool& usedCustomPak,
			bool& foundConfiguredCandidate,
			std::vector<std::wstring>& preparedPaths,
			std::vector<std::wstring>* runtimeSearchDirs = nullptr,
			bool* foundLocaleCandidate = nullptr);
		inline HMODULE LoadLocaleEmulatorLoaderModule(
			const LocaleEmulatorLoaderOptions& options,
			bool& usedCustomPak,
			bool& foundConfiguredCandidate,
			std::vector<std::wstring>& preparedPaths,
			std::vector<std::wstring>* runtimeSearchDirs = nullptr,
			bool* foundLocaleCandidate = nullptr)
		{
			bool usedConfiguredOverride = false;
			return LoadLocaleEmulatorLoaderModule(options, usedConfiguredOverride, usedCustomPak, foundConfiguredCandidate, preparedPaths, runtimeSearchDirs, foundLocaleCandidate);
		}
	}
}
