#pragma once

#include <string>
#include "settings.h"

namespace CialloHook
{
	class ConfigManager
	{
	public:
			static bool Load(const std::wstring& iniPath, AppSettings& settings, std::string& errorMessage, std::string* warningMessage = nullptr);
			static std::wstring DescribeSource(const std::wstring& iniPath);
	};
}
