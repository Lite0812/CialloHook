#include "INI.h"
#include "Str.h"

#include <Windows.h>

#include <limits>
#include <stdexcept>

namespace Rcf
{
	namespace INI
	{
		using namespace Rut::StrX;

		namespace
		{
			std::string ReadAllBytes(const std::wstring& path)
			{
				HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE)
				{
					throw std::runtime_error("OpenFileUTF8Stream: Open File Error!");
				}

				LARGE_INTEGER fileSize = {};
				if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart > (std::numeric_limits<DWORD>::max)())
				{
					CloseHandle(file);
					throw std::runtime_error("OpenFileUTF8Stream: Open File Error!");
				}

				std::string bytes(static_cast<size_t>(fileSize.QuadPart), '\0');
				DWORD bytesRead = 0;
				BOOL ok = bytes.empty() || ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
				CloseHandle(file);
				if (!ok || bytesRead != bytes.size())
				{
					throw std::runtime_error("OpenFileUTF8Stream: Open File Error!");
				}
				return bytes;
			}

			void WriteAllBytes(const std::wstring& path, const std::string& bytes)
			{
				if (bytes.size() > (std::numeric_limits<DWORD>::max)())
				{
					throw std::runtime_error("CreateFileUTF8Stream: Create File Error!");
				}

				HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE)
				{
					throw std::runtime_error("CreateFileUTF8Stream: Create File Error!");
				}

				DWORD bytesWritten = 0;
				BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr);
				CloseHandle(file);
				if (!ok || bytesWritten != bytes.size())
				{
					throw std::runtime_error("CreateFileUTF8Stream: Create File Error!");
				}
			}

			std::wstring Utf8ToWide(const std::string& bytes)
			{
				const char* data = bytes.data();
				size_t size = bytes.size();
				if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
				{
					data += 3;
					size -= 3;
				}
				if (size == 0)
				{
					return L"";
				}
				if (size > static_cast<size_t>((std::numeric_limits<int>::max)()))
				{
					throw std::runtime_error("INI_File::Parse: File Too Large!");
				}

				int length = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), nullptr, 0);
				if (length <= 0)
				{
					throw std::runtime_error("INI_File::Parse: UTF8 Decode Error!");
				}
				std::wstring result(length, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), &result[0], length);
				return result;
			}

			std::string WideToUtf8(const std::wstring& text)
			{
				std::string result("\xEF\xBB\xBF", 3);
				if (text.empty())
				{
					return result;
				}
				if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
				{
					throw std::runtime_error("INI_File::Save: File Too Large!");
				}

				int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
				if (length <= 0)
				{
					throw std::runtime_error("INI_File::Save: UTF8 Encode Error!");
				}
				size_t offset = result.size();
				result.resize(offset + length);
				WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[offset], length, nullptr, nullptr);
				return result;
			}
		}

		INI_File::INI_File()
		{
		}

		INI_File::INI_File(const std::wstring& wsINI)
		{
			Parse(wsINI);
		}

		void INI_File::Parse(const std::wstring& wsINI)
		{
			std::wstring content = Utf8ToWide(ReadAllBytes(wsINI));
			std::size_t pos = std::wstring::npos;
			std::wstring node_name;
			std::size_t start = 0;

			while (start < content.size())
			{
				std::size_t end = content.find(L'\n', start);
				std::wstring line = end == std::wstring::npos
					? content.substr(start)
					: content.substr(start, end - start);
				if (!line.empty() && line.back() == L'\r')
				{
					line.pop_back();
				}
				start = end == std::wstring::npos ? content.size() : end + 1;

				if (line.empty()) { continue; }

				switch (line[0])
				{
				case L'#':case L';':case L'/':
					break;

				case L'[':
				{
					pos = line.find_first_of(L']');
					if (pos == std::wstring::npos) { throw std::runtime_error("INI_File:Parse: Get Node Error!"); }
					node_name = Trim(line.substr(1, pos - 1));
				}
				break;

				default:
				{
					pos = line.find_first_of(L'=');
					if ((pos == std::wstring::npos) || (pos == 0)) { throw std::runtime_error("INI_File::Parse: Get Key Error!"); }
					m_mpNodes[node_name][Trim(line.substr(0, pos))] = Trim(line.substr(pos + 1));
				}
				break;
				}
			}
		}

		void INI_File::Save(const std::wstring& wsFile)
		{
			WriteAllBytes(wsFile, WideToUtf8(Dump()));
		}

		std::wstring INI_File::Dump()
		{
			std::wstring result;
			for (auto& node : m_mpNodes)
			{
				result += L"[";
				result += node.first;
				result += L"]\n";
				for (auto& key : node.second)
				{
					result += key.first;
					result += L"=";
					result += std::wstring(key.second);
					result += L"\n";
				}
				result += L"\n";
			}
			return result;
		}

		NodesMap::iterator INI_File::At(const std::wstring& wsNode)
		{
			return m_mpNodes.find(wsNode);
		}

		NodesMap::iterator INI_File::End()
		{
			return m_mpNodes.end();
		}

		KeysMap& INI_File::Get(const std::wstring& wsNode)
		{
			const auto& ite_node = At(wsNode);
			if (ite_node == End()) { throw std::runtime_error("INI_File::Get: INI File No Find Node"); }
			return ite_node->second;
		}

		Value& INI_File::Get(const std::wstring& wsNode, const std::wstring& wsName)
		{
			auto& keys = Get(wsNode);
			const auto& ite_keys = keys.find(wsName);
			if (ite_keys == keys.end()) { throw std::runtime_error("INI_File::Get: INI File No Find Key"); }
			return ite_keys->second;
		}

		KeysMap& INI_File::operator[] (const std::wstring& wsNode)
		{
			return Get(wsNode);
		}

		void INI_File::Add(const std::wstring& wsNode, const std::wstring& wsName, const Value& vValue)
		{
			m_mpNodes[wsNode][wsName] = vValue;
		}

		bool INI_File::Has(const std::wstring& wsNode)
		{
			return At(wsNode) != End() ? true : false;
		}

		bool INI_File::Has(const std::wstring& wsNode, const std::wstring& wsName)
		{
			auto ite_node = At(wsNode);
			if (ite_node != End())
			{
				auto& keys = ite_node->second;
				auto ite_keys = keys.find(wsName);
				return ite_keys != keys.end() ? true : false;
			}
			return false;
		}
	}
}
