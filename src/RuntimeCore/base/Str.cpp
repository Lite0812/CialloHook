#include "Str.h"

#include <codecvt>
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

		std::locale& GetCVT_UTF8()
		{
			static std::locale cvtUTF8 = std::locale(
				std::locale(),
				new std::codecvt_utf8<wchar_t, 0x10ffff, std::codecvt_mode(std::consume_header | std::generate_header | std::little_endian)>
			);
			return cvtUTF8;
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
			int charCount = MultiByteToWideChar
			(
				codePage, NULL, msString.c_str(), static_cast<int>(msString.size()), NULL, NULL
			);

			if (charCount == 0) { wsString = L""; return 0; }

			wsString.resize(charCount);

			MultiByteToWideChar
			(
				codePage, NULL, msString.c_str(), static_cast<int>(msString.size()), const_cast<wchar_t*>(wsString.data()), charCount
			);

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
			int wcharCount = WideCharToMultiByte
			(
				codePage, NULL, wsString.c_str(), static_cast<int>(wsString.size()), NULL, NULL, NULL, NULL
			);

			if (wcharCount == 0) { msString = ""; return 0; }

			msString.resize(wcharCount);

			WideCharToMultiByte
			(
				codePage, NULL, wsString.c_str(), static_cast<int>(wsString.size()), const_cast<char*>(msString.data()), wcharCount, NULL, NULL
			);

			return wcharCount;
		}

		std::wstring StrToWStr_CVT(const std::string& msString, size_t uCodePage)
		{
			std::wstring wsString;
			StrToWStr_CVT(msString, wsString, uCodePage);
			return wsString;
		}

		std::string WStrToStr_CVT(const std::wstring& wsString, size_t uCodePage)
		{
			std::string msString;
			WStrToStr_CVT(wsString, msString, uCodePage);
			return msString;
		}

		//Essentially it is still call MultiByteToWideChar / WideCharToMultiByte
		void StrToWStr_CVT(const std::string& msString, std::wstring& wsString, size_t uCodePage)
		{
			std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvtString
			(
				new std::codecvt_byname<wchar_t, char, mbstate_t>('.' + std::to_string(uCodePage))
			);
			wsString = cvtString.from_bytes(msString);
		}

		void WStrToStr_CVT(const std::wstring& wsString, std::string& msString, size_t uCodePage)
		{
			std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvtString
			(
				new std::codecvt_byname<wchar_t, char, mbstate_t>('.' + std::to_string(uCodePage))
			);
			msString = cvtString.to_bytes(wsString);
		}
	}
}
