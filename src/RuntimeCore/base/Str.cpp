#include "Str.h"

#include <limits>
#include <Windows.h>

namespace Rut
{
	namespace StrX
	{
		std::wstring Trim(std::wstring wsLine, const wchar_t* wFilterChar)
		{
			size_t begin = wsLine.find_first_not_of(wFilterChar);
			if (begin == std::wstring::npos)
			{
				return L"";
			}
			size_t end = wsLine.find_last_not_of(wFilterChar);
			return wsLine.substr(begin, end - begin + 1);
		}

		std::wstring StrToWStr(const std::string& msString, size_t uCodePage)
		{
			std::wstring wsString;
			if (StrToWStr(msString, wsString, uCodePage) == 0)
			{
				wsString.clear();
			}
			return wsString;
		}

		std::string WStrToStr(const std::wstring& wsString, size_t uCodePage)
		{
			std::string msString;
			if (WStrToStr(wsString, msString, uCodePage) == 0)
			{
				msString.clear();
			}
			return msString;
		}

		size_t StrToWStr(const std::string& msString, std::wstring& wsString, size_t uCodePage)
		{
			if (uCodePage > static_cast<size_t>((std::numeric_limits<UINT>::max)()))
			{
				wsString = L"";
				return 0;
			}
			UINT codePage = static_cast<UINT>(uCodePage);
			int charCount = MultiByteToWideChar(codePage, 0, msString.c_str(), static_cast<int>(msString.size()), nullptr, 0);
			if (charCount == 0)
			{
				wsString = L"";
				return 0;
			}

			wsString.resize(charCount);
			MultiByteToWideChar(codePage, 0, msString.c_str(), static_cast<int>(msString.size()), &wsString[0], charCount);
			return charCount;
		}

		size_t WStrToStr(const std::wstring& wsString, std::string& msString, size_t uCodePage)
		{
			if (uCodePage > static_cast<size_t>((std::numeric_limits<UINT>::max)()))
			{
				msString = "";
				return 0;
			}
			UINT codePage = static_cast<UINT>(uCodePage);
			int charCount = WideCharToMultiByte(codePage, 0, wsString.c_str(), static_cast<int>(wsString.size()), nullptr, 0, nullptr, nullptr);
			if (charCount == 0)
			{
				msString = "";
				return 0;
			}

			msString.resize(charCount);
			WideCharToMultiByte(codePage, 0, wsString.c_str(), static_cast<int>(wsString.size()), &msString[0], charCount, nullptr, nullptr);
			return charCount;
		}
	}
}
