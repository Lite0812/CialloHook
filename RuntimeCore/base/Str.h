#pragma once
#include <string>

//Ria's Utility Library X
namespace Rut
{
	namespace StrX
	{
		std::wstring Trim(std::wstring wsLine, const wchar_t* wFilterChar = L" \r\n\t");
		std::locale& GetCVT_UTF8();

		std::wstring StrToWStr(const std::string& msString, size_t uCodePage);
		std::string  WStrToStr(const std::wstring& wsString, size_t uCodePage);
		size_t       StrToWStr(const std::string& msString, std::wstring& wsString, size_t uCodePage);
		size_t       WStrToStr(const std::wstring& wsString, std::string& msString, size_t uCodePage);

		std::wstring StrToWStr_CVT(const std::string& msString, size_t uCodePage);
		std::string  WStrToStr_CVT(const std::wstring& wsString, size_t uCodePage);
		void         StrToWStr_CVT(const std::string& msString, std::wstring& wsString, size_t uCodePage);
		void         WStrToStr_CVT(const std::wstring& wsString, std::string& msString, size_t uCodePage);
	}
}
