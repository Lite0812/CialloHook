#include "INI_Value.h"
#include "Str.h"

#include <limits>
#include <stdexcept>

namespace Rcf
{
	namespace INI
	{
		namespace
		{
			static std::wstring GetTrimmedValue(const std::wstring& value)
			{
				return Rut::StrX::Trim(value);
			}

			static size_t GetParsedLength(const std::wstring& value, const size_t parsedLength)
			{
				return parsedLength == std::wstring::npos ? value.size() : parsedLength;
			}
		}

		Value::Value()
		{

		}

		Value::Value(const int iValue)
		{
			m_wsValue = std::to_wstring(iValue);
		}

		Value::Value(const uint32_t uiValue)
		{
			m_wsValue = std::to_wstring(uiValue);
		}

		Value::Value(const float fValue) 
		{ 
			m_wsValue = std::to_wstring(fValue);
		}

		Value::Value(const double dValue) 
		{ 
			m_wsValue = std::to_wstring(dValue);
		}

		Value::Value(const bool bValue) 
		{ 
			m_wsValue = bValue ? L"true" : L"false";
		}

		Value::Value(const wchar_t* wValue)
		{ 
			m_wsValue = wValue;
		}

		Value::Value(const std::wstring& wsValue)
		{
			m_wsValue = wsValue; 
		}

		Value::operator const int() 
		{
			std::wstring value = GetTrimmedValue(m_wsValue);
			size_t parsedLength = 0;
			int parsedValue = std::stoi(value, &parsedLength, 0);
			if (GetParsedLength(value, parsedLength) != value.size())
			{
				throw std::invalid_argument("invalid integer value");
			}
			return parsedValue;
		}

		Value::operator const uint32_t()
		{
			std::wstring value = GetTrimmedValue(m_wsValue);
			if (value.empty() || value[0] == L'-')
			{
				throw std::invalid_argument("invalid uint32 value");
			}
			size_t parsedLength = 0;
			unsigned long parsedValue = std::stoul(value, &parsedLength, 0);
			if (GetParsedLength(value, parsedLength) != value.size() || parsedValue > (std::numeric_limits<uint32_t>::max)())
			{
				throw std::invalid_argument("invalid uint32 value");
			}
			return static_cast<uint32_t>(parsedValue);
		}

		Value::operator const float() 
		{
			std::wstring value = GetTrimmedValue(m_wsValue);
			size_t parsedLength = 0;
			float parsedValue = std::stof(value, &parsedLength);
			if (GetParsedLength(value, parsedLength) != value.size())
			{
				throw std::invalid_argument("invalid float value");
			}
			return parsedValue;
		}

		Value::operator const double() 
		{
			std::wstring value = GetTrimmedValue(m_wsValue);
			size_t parsedLength = 0;
			double parsedValue = std::stod(value, &parsedLength);
			if (GetParsedLength(value, parsedLength) != value.size())
			{
				throw std::invalid_argument("invalid double value");
			}
			return parsedValue;
		}

		Value::operator const bool() 
		{
			std::wstring value = GetTrimmedValue(m_wsValue);
			if (value == L"1")
			{
				return true;
			}
			if (value == L"0")
			{
				return false;
			}
			if (_wcsicmp(value.c_str(), L"true") == 0 || _wcsicmp(value.c_str(), L"yes") == 0 || _wcsicmp(value.c_str(), L"on") == 0)
			{
				return true;
			}
			if (_wcsicmp(value.c_str(), L"false") == 0 || _wcsicmp(value.c_str(), L"no") == 0 || _wcsicmp(value.c_str(), L"off") == 0)
			{
				return false;
			}
			throw std::invalid_argument("invalid bool value");
		}

		Value::operator const wchar_t*() 
		{
			return m_wsValue.c_str();
		}

		Value::operator const std::string() const
		{
			return Rut::StrX::WStrToStr(m_wsValue, 0);
		}

		Value::operator const std::wstring() const
		{
			return m_wsValue;
		}

		Value::operator std::wstring& () 
		{
			return m_wsValue;
		}

		bool Value::Empty() 
		{
			return m_wsValue.empty();
		}
	}
}
