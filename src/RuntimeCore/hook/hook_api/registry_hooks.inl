		static pRegOpenKeyExW rawRegOpenKeyExW = RegOpenKeyExW;
		static pRegOpenKeyExA rawRegOpenKeyExA = RegOpenKeyExA;
		static pRegOpenKeyW rawRegOpenKeyW = RegOpenKeyW;
		static pRegOpenKeyA rawRegOpenKeyA = RegOpenKeyA;
		static pRegCreateKeyExW rawRegCreateKeyExW = RegCreateKeyExW;
		static pRegCreateKeyExA rawRegCreateKeyExA = RegCreateKeyExA;
		static pRegCreateKeyW rawRegCreateKeyW = RegCreateKeyW;
		static pRegCreateKeyA rawRegCreateKeyA = RegCreateKeyA;
		static pRegCloseKey rawRegCloseKey = RegCloseKey;
		static pRegQueryValueExW rawRegQueryValueExW = RegQueryValueExW;
		static pRegQueryValueExA rawRegQueryValueExA = RegQueryValueExA;
		static pRegGetValueW rawRegGetValueW = RegGetValueW;
		static pRegGetValueA rawRegGetValueA = RegGetValueA;
		static pRegSetValueExW rawRegSetValueExW = RegSetValueExW;
		static pRegSetValueExA rawRegSetValueExA = RegSetValueExA;
		static pRegEnumKeyExW rawRegEnumKeyExW = RegEnumKeyExW;
		static pRegEnumKeyExA rawRegEnumKeyExA = RegEnumKeyExA;
		static pRegEnumValueW rawRegEnumValueW = RegEnumValueW;
		static pRegEnumValueA rawRegEnumValueA = RegEnumValueA;
		static pRegQueryInfoKeyW rawRegQueryInfoKeyW = RegQueryInfoKeyW;
		static pRegQueryInfoKeyA rawRegQueryInfoKeyA = RegQueryInfoKeyA;

		struct VirtualRegistryValue
		{
			DWORD type = REG_NONE;
			std::wstring text;
			std::vector<BYTE> raw;
		};

		struct VirtualRegistryKey
		{
			std::wstring displayPath;
			std::wstring normalizedPath;
			std::unordered_map<std::wstring, std::shared_ptr<VirtualRegistryKey>> subKeys;
			std::vector<std::wstring> subKeyOrder;
			std::unordered_map<std::wstring, VirtualRegistryValue> values;
			std::vector<std::wstring> valueOrder;
		};

		static bool sg_virtualRegistryEnableLog = false;
		static bool sg_virtualRegistryLoaded = false;
		static std::wstring sg_virtualRegistrySourcePath;
		static std::unordered_map<std::wstring, std::shared_ptr<VirtualRegistryKey>> sg_virtualRegistryPathMap;
		static std::unordered_map<ULONG_PTR, std::shared_ptr<VirtualRegistryKey>> sg_virtualRegistryHandleMap;
		static ULONG_PTR sg_nextVirtualRegistryHandle = 1;
		static CRITICAL_SECTION sg_virtualRegistryLock;
		static bool sg_virtualRegistryLockInitialized = false;

		struct RegistryRootAlias
		{
			const wchar_t* name;
			HKEY handle;
		};

		static const RegistryRootAlias kRegistryRootAliases[] =
		{
			{ L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT },
			{ L"HKCR", HKEY_CLASSES_ROOT },
			{ L"HKEY_CURRENT_USER", HKEY_CURRENT_USER },
			{ L"HKCU", HKEY_CURRENT_USER },
			{ L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },
			{ L"HKLM", HKEY_LOCAL_MACHINE },
			{ L"HKEY_USERS", HKEY_USERS },
			{ L"HKU", HKEY_USERS },
			{ L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
			{ L"HKCC", HKEY_CURRENT_CONFIG }
		};

		struct ScopedVirtualRegistryLock
		{
			ScopedVirtualRegistryLock()
			{
				if (!sg_virtualRegistryLockInitialized)
				{
					InitializeCriticalSection(&sg_virtualRegistryLock);
					sg_virtualRegistryLockInitialized = true;
				}
				EnterCriticalSection(&sg_virtualRegistryLock);
			}

			~ScopedVirtualRegistryLock()
			{
				LeaveCriticalSection(&sg_virtualRegistryLock);
			}
		};

		static std::wstring RegistryWideToAnsiRoundTrip(const std::wstring& text)
		{
			return text;
		}

		static std::string WideToAnsiCodePage(const std::wstring& text, UINT codePage = CP_ACP)
		{
			if (text.empty())
			{
				return std::string();
			}

			int len = WideCharToMultiByte(codePage, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (len <= 0)
			{
				return std::string();
			}

			std::string result(static_cast<size_t>(len) - 1, '\0');
			WideCharToMultiByte(codePage, 0, text.c_str(), -1, &result[0], len, nullptr, nullptr);
			return result;
		}

		static std::wstring AnsiToWideCodePage(const char* text, int charCount, UINT codePage = CP_ACP)
		{
			if (!text)
			{
				return L"";
			}

			int sourceLength = charCount >= 0 ? charCount : static_cast<int>(strlen(text));
			if (sourceLength <= 0)
			{
				return L"";
			}

			int len = MultiByteToWideChar(codePage, 0, text, sourceLength, nullptr, 0);
			if (len <= 0)
			{
				return L"";
			}

			std::wstring result(static_cast<size_t>(len), L'\0');
			MultiByteToWideChar(codePage, 0, text, sourceLength, &result[0], len);
			return result;
		}

		static std::wstring TrimWhitespace(const std::wstring& text)
		{
			size_t begin = 0;
			while (begin < text.size() && iswspace(static_cast<wint_t>(text[begin])) != 0)
			{
				++begin;
			}
			size_t end = text.size();
			while (end > begin && iswspace(static_cast<wint_t>(text[end - 1])) != 0)
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}

		static std::wstring ToLowerWide(std::wstring text)
		{
			for (wchar_t& c : text)
			{
				c = static_cast<wchar_t>(towlower(c));
			}
			return text;
		}

		static std::wstring NormalizeRegistrySubPath(const std::wstring& value)
		{
			std::wstring normalized;
			normalized.reserve(value.size());
			bool previousSlash = false;
			for (wchar_t c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
				if (c == L'\\')
				{
					if (previousSlash)
					{
						continue;
					}
					previousSlash = true;
				}
				else
				{
					previousSlash = false;
				}
				normalized.push_back(c);
			}

			while (!normalized.empty() && normalized.front() == L'\\')
			{
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == L'\\')
			{
				normalized.pop_back();
			}
			return normalized;
		}

		static std::vector<std::wstring> SplitRegistryPath(const std::wstring& path)
		{
			std::vector<std::wstring> parts;
			size_t start = 0;
			while (start <= path.size())
			{
				size_t slash = path.find(L'\\', start);
				std::wstring part = slash == std::wstring::npos
					? path.substr(start)
					: path.substr(start, slash - start);
				if (!part.empty())
				{
					parts.push_back(part);
				}
				if (slash == std::wstring::npos)
				{
					break;
				}
				start = slash + 1;
			}
			return parts;
		}

		static std::wstring JoinRegistryPath(const std::wstring& left, const std::wstring& right)
		{
			if (left.empty())
			{
				return right;
			}
			if (right.empty())
			{
				return left;
			}
			return left + L"\\" + right;
		}

		static bool TryGetRegistryRootInfo(const std::wstring& rootName, std::wstring& canonicalName, HKEY& rootHandle)
		{
			std::wstring normalized = ToLowerWide(TrimWhitespace(rootName));
			for (const RegistryRootAlias& alias : kRegistryRootAliases)
			{
				if (normalized == ToLowerWide(alias.name))
				{
					canonicalName = alias.name[1] == L'K'
						? std::wstring(alias.name)
						: canonicalName;
					rootHandle = alias.handle;
					break;
				}
			}

			if (!canonicalName.empty())
			{
				return true;
			}

			for (const RegistryRootAlias& alias : kRegistryRootAliases)
			{
				if (_wcsicmp(rootName.c_str(), alias.name) == 0)
				{
					rootHandle = alias.handle;
					switch (reinterpret_cast<ULONG_PTR>(rootHandle))
					{
					case reinterpret_cast<ULONG_PTR>(HKEY_CLASSES_ROOT):
						canonicalName = L"HKEY_CLASSES_ROOT";
						return true;
					case reinterpret_cast<ULONG_PTR>(HKEY_CURRENT_USER):
						canonicalName = L"HKEY_CURRENT_USER";
						return true;
					case reinterpret_cast<ULONG_PTR>(HKEY_LOCAL_MACHINE):
						canonicalName = L"HKEY_LOCAL_MACHINE";
						return true;
					case reinterpret_cast<ULONG_PTR>(HKEY_USERS):
						canonicalName = L"HKEY_USERS";
						return true;
					case reinterpret_cast<ULONG_PTR>(HKEY_CURRENT_CONFIG):
						canonicalName = L"HKEY_CURRENT_CONFIG";
						return true;
					default:
						break;
					}
				}
			}
			return false;
		}

		static bool TryGetRegistryRootNameFromHandle(HKEY hKey, std::wstring& rootName)
		{
			if (hKey == HKEY_CLASSES_ROOT)
			{
				rootName = L"HKEY_CLASSES_ROOT";
				return true;
			}
			if (hKey == HKEY_CURRENT_USER)
			{
				rootName = L"HKEY_CURRENT_USER";
				return true;
			}
			if (hKey == HKEY_LOCAL_MACHINE)
			{
				rootName = L"HKEY_LOCAL_MACHINE";
				return true;
			}
			if (hKey == HKEY_USERS)
			{
				rootName = L"HKEY_USERS";
				return true;
			}
			if (hKey == HKEY_CURRENT_CONFIG)
			{
				rootName = L"HKEY_CURRENT_CONFIG";
				return true;
			}
			return false;
		}

		static std::wstring MakeNormalizedRegistryPath(const std::wstring& fullPath)
		{
			return ToLowerWide(NormalizeRegistrySubPath(fullPath));
		}

		static std::shared_ptr<VirtualRegistryKey> GetVirtualRegistryKeyByHandleLocked(HKEY hKey)
		{
			auto it = sg_virtualRegistryHandleMap.find(reinterpret_cast<ULONG_PTR>(hKey));
			return it == sg_virtualRegistryHandleMap.end() ? nullptr : it->second;
		}

		static std::shared_ptr<VirtualRegistryKey> FindVirtualRegistryKeyLocked(const std::wstring& normalizedPath)
		{
			auto it = sg_virtualRegistryPathMap.find(normalizedPath);
			return it == sg_virtualRegistryPathMap.end() ? nullptr : it->second;
		}

		static std::shared_ptr<VirtualRegistryKey> EnsureVirtualRegistryKeyLocked(const std::wstring& canonicalPath)
		{
			std::wstring normalizedPath = MakeNormalizedRegistryPath(canonicalPath);
			auto existing = FindVirtualRegistryKeyLocked(normalizedPath);
			if (existing)
			{
				return existing;
			}

			std::shared_ptr<VirtualRegistryKey> key = std::make_shared<VirtualRegistryKey>();
			key->displayPath = canonicalPath;
			key->normalizedPath = normalizedPath;
			sg_virtualRegistryPathMap[normalizedPath] = key;

			size_t slash = canonicalPath.find_last_of(L'\\');
			if (slash != std::wstring::npos)
			{
				std::wstring parentPath = canonicalPath.substr(0, slash);
				std::wstring childName = canonicalPath.substr(slash + 1);
				std::shared_ptr<VirtualRegistryKey> parent = EnsureVirtualRegistryKeyLocked(parentPath);
				std::wstring childNameLower = ToLowerWide(childName);
				if (parent->subKeys.find(childNameLower) == parent->subKeys.end())
				{
					parent->subKeys[childNameLower] = key;
					parent->subKeyOrder.push_back(childName);
				}
			}

			return key;
		}

		static void ClearVirtualRegistryLocked()
		{
			sg_virtualRegistryLoaded = false;
			sg_virtualRegistrySourcePath.clear();
			sg_virtualRegistryPathMap.clear();
			sg_virtualRegistryHandleMap.clear();
			sg_nextVirtualRegistryHandle = 1;
		}

		static HKEY CreateVirtualRegistryHandleLocked(const std::shared_ptr<VirtualRegistryKey>& key)
		{
			if (!key)
			{
				return nullptr;
			}

			ULONG_PTR handleValue = 0xC1A11000ull + (sg_nextVirtualRegistryHandle++ << 4);
			HKEY hKey = reinterpret_cast<HKEY>(handleValue);
			sg_virtualRegistryHandleMap[handleValue] = key;
			return hKey;
		}

		static bool TryResolveVirtualRegistryPathLocked(HKEY hKey, const std::wstring& subKey, std::shared_ptr<VirtualRegistryKey>& keyOut)
		{
			keyOut = nullptr;

			if (std::shared_ptr<VirtualRegistryKey> existingKey = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				std::wstring fullPath = existingKey->displayPath;
				std::wstring normalizedSubKey = NormalizeRegistrySubPath(subKey);
				if (!normalizedSubKey.empty())
				{
					fullPath = JoinRegistryPath(fullPath, normalizedSubKey);
				}
				keyOut = FindVirtualRegistryKeyLocked(MakeNormalizedRegistryPath(fullPath));
				return keyOut != nullptr;
			}

			std::wstring rootName;
			if (!TryGetRegistryRootNameFromHandle(hKey, rootName))
			{
				return false;
			}

			std::wstring fullPath = rootName;
			std::wstring normalizedSubKey = NormalizeRegistrySubPath(subKey);
			if (!normalizedSubKey.empty())
			{
				fullPath = JoinRegistryPath(fullPath, normalizedSubKey);
			}
			keyOut = FindVirtualRegistryKeyLocked(MakeNormalizedRegistryPath(fullPath));
			return keyOut != nullptr;
		}

		static bool TryCreateVirtualRegistryPathLocked(HKEY hKey, const std::wstring& subKey, std::shared_ptr<VirtualRegistryKey>& keyOut, bool& created)
		{
			created = false;
			keyOut = nullptr;

			std::wstring normalizedSubKey = NormalizeRegistrySubPath(subKey);
			if (normalizedSubKey.empty())
			{
				return TryResolveVirtualRegistryPathLocked(hKey, L"", keyOut);
			}

			if (std::shared_ptr<VirtualRegistryKey> virtualBase = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				std::wstring fullPath = JoinRegistryPath(virtualBase->displayPath, normalizedSubKey);
				keyOut = EnsureVirtualRegistryKeyLocked(fullPath);
				created = true;
				return keyOut != nullptr;
			}

			std::wstring rootName;
			if (!TryGetRegistryRootNameFromHandle(hKey, rootName))
			{
				return false;
			}

			std::vector<std::wstring> segments = SplitRegistryPath(normalizedSubKey);
			if (segments.empty())
			{
				return false;
			}

			std::wstring candidate = rootName;
			std::shared_ptr<VirtualRegistryKey> deepestExisting;
			for (size_t i = 0; i < segments.size(); ++i)
			{
				candidate = JoinRegistryPath(candidate, segments[i]);
				std::shared_ptr<VirtualRegistryKey> existing = FindVirtualRegistryKeyLocked(MakeNormalizedRegistryPath(candidate));
				if (existing)
				{
					deepestExisting = existing;
					continue;
				}
				if (!deepestExisting)
				{
					return false;
				}
				keyOut = EnsureVirtualRegistryKeyLocked(candidate);
				deepestExisting = keyOut;
				created = true;
			}

			if (!deepestExisting)
			{
				return false;
			}

			keyOut = deepestExisting;
			return true;
		}

		static bool TryReadWholeFile(const wchar_t* filePath, std::vector<BYTE>& bytes)
		{
			bytes.clear();
			if (!filePath || !filePath[0])
			{
				return false;
			}

			HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				return false;
			}

			LARGE_INTEGER size = {};
			if (!GetFileSizeEx(hFile, &size) || size.QuadPart < 0)
			{
				CloseHandle(hFile);
				return false;
			}

			bytes.resize(static_cast<size_t>(size.QuadPart));
			DWORD totalRead = 0;
			while (totalRead < bytes.size())
			{
				DWORD chunk = 0;
				if (!ReadFile(hFile, bytes.data() + totalRead, static_cast<DWORD>(bytes.size() - totalRead), &chunk, nullptr))
				{
					CloseHandle(hFile);
					bytes.clear();
					return false;
				}
				if (chunk == 0)
				{
					break;
				}
				totalRead += chunk;
			}

			CloseHandle(hFile);
			bytes.resize(totalRead);
			return true;
		}

		static bool DecodeRegistryFileUtf16Le(const std::vector<BYTE>& bytes, std::wstring& textOut)
		{
			textOut.clear();
			if (bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xFE)
			{
				return false;
			}

			size_t contentSize = bytes.size() - 2;
			if ((contentSize % sizeof(wchar_t)) != 0)
			{
				return false;
			}

			textOut.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), contentSize / sizeof(wchar_t));
			if (!textOut.empty() && textOut.back() == L'\0')
			{
				textOut.pop_back();
			}
			return true;
		}

		static std::vector<std::wstring> SplitRegistryFileLines(const std::wstring& text)
		{
			std::vector<std::wstring> lines;
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find(L'\n', start);
				std::wstring line = end == std::wstring::npos
					? text.substr(start)
					: text.substr(start, end - start);
				if (!line.empty() && line.back() == L'\r')
				{
					line.pop_back();
				}
				lines.push_back(line);
				if (end == std::wstring::npos)
				{
					break;
				}
				start = end + 1;
			}
			return lines;
		}

		static int HexDigitValue(wchar_t c)
		{
			if (c >= L'0' && c <= L'9')
			{
				return c - L'0';
			}
			if (c >= L'a' && c <= L'f')
			{
				return 10 + (c - L'a');
			}
			if (c >= L'A' && c <= L'F')
			{
				return 10 + (c - L'A');
			}
			return -1;
		}

		static bool ParseQuotedRegistryString(const std::wstring& text, size_t startIndex, std::wstring& parsedValue, size_t& nextIndex)
		{
			parsedValue.clear();
			nextIndex = startIndex;
			if (startIndex >= text.size() || text[startIndex] != L'"')
			{
				return false;
			}

			for (size_t i = startIndex + 1; i < text.size(); ++i)
			{
				wchar_t c = text[i];
				if (c == L'\\' && i + 1 < text.size())
				{
					wchar_t escaped = text[i + 1];
					if (escaped == L'\\' || escaped == L'"')
					{
						parsedValue.push_back(escaped);
						++i;
						continue;
					}
				}
				if (c == L'"')
				{
					nextIndex = i + 1;
					return true;
				}
				parsedValue.push_back(c);
			}

			return false;
		}

		static bool ParseRegistryHexBytes(const std::wstring& text, size_t startIndex, std::vector<BYTE>& bytes)
		{
			bytes.clear();
			size_t i = startIndex;
			while (i < text.size())
			{
				while (i < text.size() && (text[i] == L' ' || text[i] == L'\t' || text[i] == L','))
				{
					++i;
				}
				if (i >= text.size())
				{
					break;
				}
				if (i + 1 >= text.size())
				{
					return false;
				}
				int hi = HexDigitValue(text[i]);
				int lo = HexDigitValue(text[i + 1]);
				if (hi < 0 || lo < 0)
				{
					return false;
				}
				bytes.push_back(static_cast<BYTE>((hi << 4) | lo));
				i += 2;
				while (i < text.size() && (text[i] == L' ' || text[i] == L'\t'))
				{
					++i;
				}
				if (i < text.size())
				{
					if (text[i] == L',')
					{
						++i;
						continue;
					}
					return false;
				}
			}
			return true;
		}

		static bool ParseRegistryDword(const std::wstring& text, DWORD& valueOut)
		{
			if (text.size() != 8)
			{
				return false;
			}
			DWORD value = 0;
			for (wchar_t c : text)
			{
				int digit = HexDigitValue(c);
				if (digit < 0)
				{
					return false;
				}
				value = (value << 4) | static_cast<DWORD>(digit);
			}
			valueOut = value;
			return true;
		}

		static std::wstring DecodeWideRegistryString(const std::vector<BYTE>& bytes)
		{
			if (bytes.empty() || (bytes.size() % sizeof(wchar_t)) != 0)
			{
				return L"";
			}

			const wchar_t* wide = reinterpret_cast<const wchar_t*>(bytes.data());
			size_t count = bytes.size() / sizeof(wchar_t);
			while (count > 0 && wide[count - 1] == L'\0')
			{
				--count;
			}
			return std::wstring(wide, count);
		}

		static VirtualRegistryValue MakeRegistryStringValue(DWORD type, const std::wstring& text)
		{
			VirtualRegistryValue value;
			value.type = type;
			value.text = text;
			return value;
		}

		static VirtualRegistryValue MakeRegistryRawValue(DWORD type, const BYTE* data, size_t dataSize)
		{
			VirtualRegistryValue value;
			value.type = type;
			if (data && dataSize > 0)
			{
				value.raw.assign(data, data + dataSize);
			}
			if ((type == REG_SZ || type == REG_EXPAND_SZ) && !value.raw.empty())
			{
				value.text = DecodeWideRegistryString(value.raw);
			}
			return value;
		}

		static bool ParseRegistryValueData(const std::wstring& dataText, VirtualRegistryValue& valueOut)
		{
			std::wstring trimmed = TrimWhitespace(dataText);
			if (trimmed.empty())
			{
				return false;
			}

			if (trimmed[0] == L'"')
			{
				std::wstring parsedText;
				size_t nextIndex = 0;
				if (!ParseQuotedRegistryString(trimmed, 0, parsedText, nextIndex))
				{
					return false;
				}
				valueOut = MakeRegistryStringValue(REG_SZ, parsedText);
				return true;
			}

			if (_wcsnicmp(trimmed.c_str(), L"dword:", 6) == 0)
			{
				DWORD dwordValue = 0;
				if (!ParseRegistryDword(TrimWhitespace(trimmed.substr(6)), dwordValue))
				{
					return false;
				}
				valueOut = MakeRegistryRawValue(REG_DWORD, reinterpret_cast<const BYTE*>(&dwordValue), sizeof(DWORD));
				return true;
			}

			if (_wcsnicmp(trimmed.c_str(), L"hex", 3) == 0)
			{
				DWORD valueType = REG_BINARY;
				size_t payloadIndex = 3;
				if (payloadIndex < trimmed.size() && trimmed[payloadIndex] == L'(')
				{
					size_t close = trimmed.find(L')', payloadIndex + 1);
					if (close == std::wstring::npos || close + 1 >= trimmed.size() || trimmed[close + 1] != L':')
					{
						return false;
					}
					std::wstring typeText = trimmed.substr(payloadIndex + 1, close - payloadIndex - 1);
					unsigned long long parsedType = 0;
					try
					{
						parsedType = std::stoull(typeText, nullptr, 16);
					}
					catch (...)
					{
						return false;
					}
					valueType = static_cast<DWORD>(parsedType);
					payloadIndex = close + 2;
				}
				else
				{
					if (payloadIndex >= trimmed.size() || trimmed[payloadIndex] != L':')
					{
						return false;
					}
					++payloadIndex;
				}

				std::vector<BYTE> bytes;
				if (!ParseRegistryHexBytes(trimmed, payloadIndex, bytes))
				{
					return false;
				}
				valueOut = MakeRegistryRawValue(valueType, bytes.data(), bytes.size());
				return true;
			}

			return false;
		}

		static bool ParseRegistryLineIntoValue(const std::wstring& line, std::wstring& valueNameOut, VirtualRegistryValue& valueOut)
		{
			valueNameOut.clear();
			if (line.empty())
			{
				return false;
			}

			size_t equals = line.find(L'=');
			if (equals == std::wstring::npos)
			{
				return false;
			}

			std::wstring left = TrimWhitespace(line.substr(0, equals));
			std::wstring right = line.substr(equals + 1);
			if (left == L"@")
			{
				valueNameOut.clear();
				return ParseRegistryValueData(right, valueOut);
			}

			size_t nextIndex = 0;
			if (!ParseQuotedRegistryString(left, 0, valueNameOut, nextIndex))
			{
				return false;
			}
			return ParseRegistryValueData(right, valueOut);
		}

		static bool ParseRegistryExportPath(const std::wstring& line, std::wstring& canonicalPathOut)
		{
			canonicalPathOut.clear();
			if (line.size() < 3 || line.front() != L'[' || line.back() != L']')
			{
				return false;
			}

			std::wstring body = line.substr(1, line.size() - 2);
			if (!body.empty() && body[0] == L'-')
			{
				return false;
			}

			size_t slash = body.find(L'\\');
			std::wstring rootText = slash == std::wstring::npos ? body : body.substr(0, slash);
			std::wstring subPath = slash == std::wstring::npos ? L"" : body.substr(slash + 1);

			std::wstring canonicalRoot;
			HKEY rootHandle = nullptr;
			if (!TryGetRegistryRootInfo(rootText, canonicalRoot, rootHandle))
			{
				return false;
			}

			canonicalPathOut = canonicalRoot;
			subPath = NormalizeRegistrySubPath(subPath);
			if (!subPath.empty())
			{
				canonicalPathOut = JoinRegistryPath(canonicalPathOut, subPath);
			}
			return true;
		}

		static bool LoadVirtualRegistryFileLockedInternal(const wchar_t* regFilePath)
		{
			std::vector<BYTE> bytes;
			if (!TryReadWholeFile(regFilePath, bytes))
			{
				return false;
			}

			std::wstring text;
			if (!DecodeRegistryFileUtf16Le(bytes, text))
			{
				return false;
			}

			std::vector<std::wstring> lines = SplitRegistryFileLines(text);
			bool seenHeader = false;
			std::shared_ptr<VirtualRegistryKey> currentKey;
			for (const std::wstring& rawLine : lines)
			{
				std::wstring line = TrimWhitespace(rawLine);
				if (line.empty() || line[0] == L';' || line[0] == L'#')
				{
					continue;
				}

				if (!seenHeader)
				{
					if (_wcsicmp(line.c_str(), L"Windows Registry Editor Version 5.00") != 0)
					{
						return false;
					}
					seenHeader = true;
					continue;
				}

				if (line.front() == L'[' && line.back() == L']')
				{
					std::wstring path;
					if (!ParseRegistryExportPath(line, path))
					{
						currentKey.reset();
						continue;
					}
					currentKey = EnsureVirtualRegistryKeyLocked(path);
					continue;
				}

				if (!currentKey)
				{
					continue;
				}

				std::wstring valueName;
				VirtualRegistryValue value;
				if (!ParseRegistryLineIntoValue(line, valueName, value))
				{
					continue;
				}

				std::wstring normalizedValueName = ToLowerWide(valueName);
				if (currentKey->values.find(normalizedValueName) == currentKey->values.end())
				{
					currentKey->valueOrder.push_back(valueName);
				}
				currentKey->values[normalizedValueName] = value;
			}

			return seenHeader && !sg_virtualRegistryPathMap.empty();
		}

		static std::vector<BYTE> BuildVirtualRegistryValueBytesW(const VirtualRegistryValue& value)
		{
			if (value.type == REG_SZ || value.type == REG_EXPAND_SZ)
			{
				std::vector<BYTE> bytes((value.text.size() + 1) * sizeof(wchar_t));
				memcpy(bytes.data(), value.text.c_str(), bytes.size());
				return bytes;
			}
			return value.raw;
		}

		static std::vector<BYTE> BuildVirtualRegistryValueBytesA(const VirtualRegistryValue& value)
		{
			if (value.type == REG_SZ || value.type == REG_EXPAND_SZ)
			{
				std::string ansi = WideToAnsiCodePage(value.text);
				std::vector<BYTE> bytes(ansi.size() + 1);
				if (!ansi.empty())
				{
					memcpy(bytes.data(), ansi.data(), ansi.size());
				}
				bytes[ansi.size()] = '\0';
				return bytes;
			}
			return value.raw;
		}

		static LSTATUS CopyVirtualRegistryValueResult(const VirtualRegistryValue& value, bool asAnsi, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
		{
			if (lpType)
			{
				*lpType = value.type;
			}

			std::vector<BYTE> bytes = asAnsi ? BuildVirtualRegistryValueBytesA(value) : BuildVirtualRegistryValueBytesW(value);
			DWORD required = static_cast<DWORD>(bytes.size());
			if (!lpcbData)
			{
				return lpData ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
			}

			DWORD provided = *lpcbData;
			*lpcbData = required;
			if (!lpData)
			{
				return ERROR_SUCCESS;
			}
			if (provided < required)
			{
				if (provided > 0 && !bytes.empty())
				{
					memcpy(lpData, bytes.data(), provided);
				}
				return ERROR_MORE_DATA;
			}

			if (!bytes.empty())
			{
				memcpy(lpData, bytes.data(), required);
			}
			return ERROR_SUCCESS;
		}

		static bool DoesRegistryTypeMatchFlags(DWORD valueType, DWORD flags)
		{
			DWORD mask = flags & RRF_RT_ANY;
			if (mask == 0 || mask == RRF_RT_ANY)
			{
				return true;
			}

			switch (valueType)
			{
			case REG_NONE:
				return (mask & RRF_RT_REG_NONE) != 0;
			case REG_SZ:
				return (mask & RRF_RT_REG_SZ) != 0;
			case REG_EXPAND_SZ:
				return (mask & RRF_RT_REG_EXPAND_SZ) != 0;
			case REG_BINARY:
				return (mask & RRF_RT_REG_BINARY) != 0;
			case REG_DWORD:
				return (mask & RRF_RT_REG_DWORD) != 0;
			case REG_MULTI_SZ:
				return (mask & RRF_RT_REG_MULTI_SZ) != 0;
			case REG_QWORD:
				return (mask & RRF_RT_REG_QWORD) != 0;
			default:
				return true;
			}
		}

		static LSTATUS QueryVirtualRegistryValueLocked(
			const std::shared_ptr<VirtualRegistryKey>& key,
			const std::wstring& valueName,
			bool asAnsi,
			DWORD flags,
			LPDWORD lpType,
			LPBYTE lpData,
			LPDWORD lpcbData)
		{
			if (!key)
			{
				return ERROR_INVALID_HANDLE;
			}

			auto valueIt = key->values.find(ToLowerWide(valueName));
			if (valueIt == key->values.end())
			{
				return ERROR_FILE_NOT_FOUND;
			}

			if (!DoesRegistryTypeMatchFlags(valueIt->second.type, flags))
			{
				return ERROR_UNSUPPORTED_TYPE;
			}

			return CopyVirtualRegistryValueResult(valueIt->second, asAnsi, lpType, lpData, lpcbData);
		}

		static void SetVirtualRegistryValueLocked(
			const std::shared_ptr<VirtualRegistryKey>& key,
			const std::wstring& valueName,
			DWORD valueType,
			const BYTE* data,
			DWORD dataSize,
			bool fromAnsi)
		{
			if (!key)
			{
				return;
			}

			VirtualRegistryValue value;
			value.type = valueType;
			if ((valueType == REG_SZ || valueType == REG_EXPAND_SZ) && data)
			{
				if (fromAnsi)
				{
					value.text = AnsiToWideCodePage(reinterpret_cast<const char*>(data), static_cast<int>(dataSize > 0 ? dataSize - 1 : 0));
				}
				else if ((dataSize % sizeof(wchar_t)) == 0)
				{
					size_t count = dataSize / sizeof(wchar_t);
					const wchar_t* wide = reinterpret_cast<const wchar_t*>(data);
					while (count > 0 && wide[count - 1] == L'\0')
					{
						--count;
					}
					value.text.assign(wide, count);
				}
			}
			else if (data && dataSize > 0)
			{
				value.raw.assign(data, data + dataSize);
			}

			std::wstring normalizedValueName = ToLowerWide(valueName);
			if (key->values.find(normalizedValueName) == key->values.end())
			{
				key->valueOrder.push_back(valueName);
			}
			key->values[normalizedValueName] = value;
		}

		static DWORD GetMaxVirtualRegistrySubKeyLength(const std::shared_ptr<VirtualRegistryKey>& key)
		{
			DWORD maxLength = 0;
			if (!key)
			{
				return maxLength;
			}
			for (const std::wstring& name : key->subKeyOrder)
			{
				maxLength = maxLength > static_cast<DWORD>(name.size()) ? maxLength : static_cast<DWORD>(name.size());
			}
			return maxLength;
		}

		static DWORD GetMaxVirtualRegistryValueNameLength(const std::shared_ptr<VirtualRegistryKey>& key)
		{
			DWORD maxLength = 0;
			if (!key)
			{
				return maxLength;
			}
			for (const std::wstring& name : key->valueOrder)
			{
				maxLength = maxLength > static_cast<DWORD>(name.size()) ? maxLength : static_cast<DWORD>(name.size());
			}
			return maxLength;
		}

		static DWORD GetMaxVirtualRegistryValueDataLength(const std::shared_ptr<VirtualRegistryKey>& key, bool asAnsi)
		{
			DWORD maxLength = 0;
			if (!key)
			{
				return maxLength;
			}
			for (const auto& pair : key->values)
			{
				std::vector<BYTE> bytes = asAnsi ? BuildVirtualRegistryValueBytesA(pair.second) : BuildVirtualRegistryValueBytesW(pair.second);
				maxLength = maxLength > static_cast<DWORD>(bytes.size()) ? maxLength : static_cast<DWORD>(bytes.size());
			}
			return maxLength;
		}

		bool LoadVirtualRegistryFile(const wchar_t* regFilePath, bool enableLog)
		{
			ScopedVirtualRegistryLock lock;
			ClearVirtualRegistryLocked();
			sg_virtualRegistryEnableLog = enableLog;

			if (!regFilePath || !regFilePath[0])
			{
				if (enableLog)
				{
					LogMessage(LogLevel::Warn, L"VirtualRegistry: registry file path is empty");
				}
				return false;
			}

			if (!LoadVirtualRegistryFileLockedInternal(regFilePath))
			{
				if (enableLog)
				{
					LogMessage(LogLevel::Error, L"VirtualRegistry: failed to load %s (require UTF-16LE Windows .reg export)", regFilePath);
				}
				return false;
			}

			sg_virtualRegistryLoaded = true;
			sg_virtualRegistrySourcePath = regFilePath;
			if (enableLog)
			{
				LogMessage(LogLevel::Info, L"VirtualRegistry: loaded %u keys from %s",
					static_cast<uint32_t>(sg_virtualRegistryPathMap.size()),
					sg_virtualRegistrySourcePath.c_str());
			}
			return true;
		}

		bool LoadVirtualRegistryFiles(const wchar_t* const* regFilePaths, size_t count, bool enableLog)
		{
			ScopedVirtualRegistryLock lock;
			ClearVirtualRegistryLocked();
			sg_virtualRegistryEnableLog = enableLog;

			if (!regFilePaths || count == 0)
			{
				if (enableLog)
				{
					LogMessage(LogLevel::Warn, L"VirtualRegistry: registry file list is empty");
				}
				return false;
			}

			size_t loadedCount = 0;
			for (size_t i = 0; i < count; ++i)
			{
				const wchar_t* regFilePath = regFilePaths[i];
				if (!regFilePath || !regFilePath[0])
				{
					if (enableLog)
					{
						LogMessage(LogLevel::Warn, L"VirtualRegistry: skip empty registry file path at index %u", static_cast<uint32_t>(i));
					}
					continue;
				}

				if (!LoadVirtualRegistryFileLockedInternal(regFilePath))
				{
					if (enableLog)
					{
						LogMessage(LogLevel::Error, L"VirtualRegistry: failed to load %s (require UTF-16LE Windows .reg export)", regFilePath);
					}
					return false;
				}

				if (!sg_virtualRegistrySourcePath.empty())
				{
					sg_virtualRegistrySourcePath.append(L"; ");
				}
				sg_virtualRegistrySourcePath.append(regFilePath);
				++loadedCount;
			}

			if (loadedCount == 0)
			{
				if (enableLog)
				{
					LogMessage(LogLevel::Warn, L"VirtualRegistry: no valid registry files loaded");
				}
				return false;
			}

			sg_virtualRegistryLoaded = true;
			if (enableLog)
			{
				LogMessage(LogLevel::Info, L"VirtualRegistry: loaded %u keys from %u file(s)",
					static_cast<uint32_t>(sg_virtualRegistryPathMap.size()),
					static_cast<uint32_t>(loadedCount));
			}
			return true;
		}

		static LSTATUS WINAPI newRegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
		{
			if (!phkResult)
			{
				return ERROR_INVALID_PARAMETER;
			}

			ScopedVirtualRegistryLock lock;
			std::shared_ptr<VirtualRegistryKey> key;
			if (sg_virtualRegistryLoaded && TryResolveVirtualRegistryPathLocked(hKey, lpSubKey ? lpSubKey : L"", key))
			{
				*phkResult = CreateVirtualRegistryHandleLocked(key);
				if (sg_virtualRegistryEnableLog)
				{
					LogMessage(LogLevel::Debug, L"VirtualRegistry: RegOpenKeyExW -> %s", key->displayPath.c_str());
				}
				return ERROR_SUCCESS;
			}
			return rawRegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
		}

		static LSTATUS WINAPI newRegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
		{
			std::wstring subKey = lpSubKey ? AnsiToWideCodePage(lpSubKey, -1) : L"";
			return newRegOpenKeyExW(hKey, subKey.c_str(), ulOptions, samDesired, phkResult);
		}

		static LSTATUS WINAPI newRegOpenKeyW(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult)
		{
			return newRegOpenKeyExW(hKey, lpSubKey, 0, KEY_READ, phkResult);
		}

		static LSTATUS WINAPI newRegOpenKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult)
		{
			return newRegOpenKeyExA(hKey, lpSubKey, 0, KEY_READ, phkResult);
		}

		static LSTATUS WINAPI newRegCreateKeyExW(
			HKEY hKey,
			LPCWSTR lpSubKey,
			DWORD Reserved,
			LPWSTR lpClass,
			DWORD dwOptions,
			REGSAM samDesired,
			CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			PHKEY phkResult,
			LPDWORD lpdwDisposition)
		{
			ScopedVirtualRegistryLock lock;
			std::shared_ptr<VirtualRegistryKey> key;
			bool created = false;
			if (sg_virtualRegistryLoaded && TryCreateVirtualRegistryPathLocked(hKey, lpSubKey ? lpSubKey : L"", key, created))
			{
				if (phkResult)
				{
					*phkResult = CreateVirtualRegistryHandleLocked(key);
				}
				if (lpdwDisposition)
				{
					*lpdwDisposition = created ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
				}
				if (sg_virtualRegistryEnableLog)
				{
					LogMessage(LogLevel::Debug, L"VirtualRegistry: RegCreateKeyExW -> %s", key->displayPath.c_str());
				}
				return ERROR_SUCCESS;
			}
			return rawRegCreateKeyExW(hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
		}

		static LSTATUS WINAPI newRegCreateKeyExA(
			HKEY hKey,
			LPCSTR lpSubKey,
			DWORD Reserved,
			LPSTR lpClass,
			DWORD dwOptions,
			REGSAM samDesired,
			CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			PHKEY phkResult,
			LPDWORD lpdwDisposition)
		{
			std::wstring subKey = lpSubKey ? AnsiToWideCodePage(lpSubKey, -1) : L"";
			return newRegCreateKeyExW(hKey, subKey.c_str(), Reserved, nullptr, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
		}

		static LSTATUS WINAPI newRegCreateKeyW(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult)
		{
			return newRegCreateKeyExW(hKey, lpSubKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, phkResult, nullptr);
		}

		static LSTATUS WINAPI newRegCreateKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult)
		{
			return newRegCreateKeyExA(hKey, lpSubKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, phkResult, nullptr);
		}

		static LSTATUS WINAPI newRegCloseKey(HKEY hKey)
		{
			ScopedVirtualRegistryLock lock;
			auto it = sg_virtualRegistryHandleMap.find(reinterpret_cast<ULONG_PTR>(hKey));
			if (it != sg_virtualRegistryHandleMap.end())
			{
				sg_virtualRegistryHandleMap.erase(it);
				return ERROR_SUCCESS;
			}
			return rawRegCloseKey(hKey);
		}

		static LSTATUS WINAPI newRegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				return QueryVirtualRegistryValueLocked(key, lpValueName ? lpValueName : L"", false, 0, lpType, lpData, lpcbData);
			}
			return rawRegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
		}

		static LSTATUS WINAPI newRegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				std::wstring valueName = lpValueName ? AnsiToWideCodePage(lpValueName, -1) : L"";
				return QueryVirtualRegistryValueLocked(key, valueName, true, 0, lpType, lpData, lpcbData);
			}
			return rawRegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
		}

		static LSTATUS WINAPI newRegGetValueW(HKEY hKey, LPCWSTR lpSubKey, LPCWSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData)
		{
			ScopedVirtualRegistryLock lock;
			std::shared_ptr<VirtualRegistryKey> key;
			if (sg_virtualRegistryLoaded && TryResolveVirtualRegistryPathLocked(hKey, lpSubKey ? lpSubKey : L"", key))
			{
				return QueryVirtualRegistryValueLocked(key, lpValue ? lpValue : L"", false, dwFlags, pdwType, reinterpret_cast<LPBYTE>(pvData), pcbData);
			}
			return rawRegGetValueW(hKey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
		}

		static LSTATUS WINAPI newRegGetValueA(HKEY hKey, LPCSTR lpSubKey, LPCSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData)
		{
			std::wstring subKey = lpSubKey ? AnsiToWideCodePage(lpSubKey, -1) : L"";
			std::wstring valueName = lpValue ? AnsiToWideCodePage(lpValue, -1) : L"";

			ScopedVirtualRegistryLock lock;
			std::shared_ptr<VirtualRegistryKey> key;
			if (sg_virtualRegistryLoaded && TryResolveVirtualRegistryPathLocked(hKey, subKey, key))
			{
				return QueryVirtualRegistryValueLocked(key, valueName, true, dwFlags, pdwType, reinterpret_cast<LPBYTE>(pvData), pcbData);
			}
			return rawRegGetValueA(hKey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
		}

		static LSTATUS WINAPI newRegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, CONST BYTE* lpData, DWORD cbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				SetVirtualRegistryValueLocked(key, lpValueName ? lpValueName : L"", dwType, lpData, cbData, false);
				if (sg_virtualRegistryEnableLog)
				{
					LogMessage(LogLevel::Debug, L"VirtualRegistry: RegSetValueExW %s [%s]", key->displayPath.c_str(), lpValueName ? lpValueName : L"@");
				}
				return ERROR_SUCCESS;
			}
			return rawRegSetValueExW(hKey, lpValueName, Reserved, dwType, lpData, cbData);
		}

		static LSTATUS WINAPI newRegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, CONST BYTE* lpData, DWORD cbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				std::wstring valueName = lpValueName ? AnsiToWideCodePage(lpValueName, -1) : L"";
				SetVirtualRegistryValueLocked(key, valueName, dwType, lpData, cbData, true);
				if (sg_virtualRegistryEnableLog)
				{
					LogMessage(LogLevel::Debug, L"VirtualRegistry: RegSetValueExA %s [%s]", key->displayPath.c_str(), valueName.empty() ? L"@" : valueName.c_str());
				}
				return ERROR_SUCCESS;
			}
			return rawRegSetValueExA(hKey, lpValueName, Reserved, dwType, lpData, cbData);
		}

		static LSTATUS WINAPI newRegEnumKeyExW(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcName, LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcClass, PFILETIME lpftLastWriteTime)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (!lpcName)
				{
					return ERROR_INVALID_PARAMETER;
				}
				if (dwIndex >= key->subKeyOrder.size())
				{
					return ERROR_NO_MORE_ITEMS;
				}

				const std::wstring& name = key->subKeyOrder[dwIndex];
				DWORD required = static_cast<DWORD>(name.size());
				DWORD provided = *lpcName;
				*lpcName = required;
				if (!lpName)
				{
					return ERROR_SUCCESS;
				}
				if (provided <= required)
				{
					if (provided > 0)
					{
						wcsncpy_s(lpName, provided, name.c_str(), _TRUNCATE);
					}
					return ERROR_MORE_DATA;
				}
				wcsncpy_s(lpName, provided, name.c_str(), _TRUNCATE);
				if (lpClass && lpcClass && *lpcClass > 0)
				{
					lpClass[0] = L'\0';
					*lpcClass = 0;
				}
				if (lpftLastWriteTime)
				{
					memset(lpftLastWriteTime, 0, sizeof(FILETIME));
				}
				return ERROR_SUCCESS;
			}
			return rawRegEnumKeyExW(hKey, dwIndex, lpName, lpcName, lpReserved, lpClass, lpcClass, lpftLastWriteTime);
		}

		static LSTATUS WINAPI newRegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcName, LPDWORD lpReserved, LPSTR lpClass, LPDWORD lpcClass, PFILETIME lpftLastWriteTime)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (!lpcName)
				{
					return ERROR_INVALID_PARAMETER;
				}
				if (dwIndex >= key->subKeyOrder.size())
				{
					return ERROR_NO_MORE_ITEMS;
				}

				std::string name = WideToAnsiCodePage(key->subKeyOrder[dwIndex]);
				DWORD required = static_cast<DWORD>(name.size());
				DWORD provided = *lpcName;
				*lpcName = required;
				if (!lpName)
				{
					return ERROR_SUCCESS;
				}
				if (provided <= required)
				{
					if (provided > 0)
					{
						strncpy_s(lpName, provided, name.c_str(), _TRUNCATE);
					}
					return ERROR_MORE_DATA;
				}
				strncpy_s(lpName, provided, name.c_str(), _TRUNCATE);
				if (lpClass && lpcClass && *lpcClass > 0)
				{
					lpClass[0] = '\0';
					*lpcClass = 0;
				}
				if (lpftLastWriteTime)
				{
					memset(lpftLastWriteTime, 0, sizeof(FILETIME));
				}
				return ERROR_SUCCESS;
			}
			return rawRegEnumKeyExA(hKey, dwIndex, lpName, lpcName, lpReserved, lpClass, lpcClass, lpftLastWriteTime);
		}

		static LSTATUS WINAPI newRegEnumValueW(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (!lpcchValueName)
				{
					return ERROR_INVALID_PARAMETER;
				}
				if (dwIndex >= key->valueOrder.size())
				{
					return ERROR_NO_MORE_ITEMS;
				}

				const std::wstring& valueName = key->valueOrder[dwIndex];
				DWORD required = static_cast<DWORD>(valueName.size());
				DWORD provided = *lpcchValueName;
				*lpcchValueName = required;
				if (lpValueName)
				{
					if (provided <= required)
					{
						if (provided > 0)
						{
							wcsncpy_s(lpValueName, provided, valueName.c_str(), _TRUNCATE);
						}
						return ERROR_MORE_DATA;
					}
					wcsncpy_s(lpValueName, provided, valueName.c_str(), _TRUNCATE);
				}

				return QueryVirtualRegistryValueLocked(key, valueName, false, 0, lpType, lpData, lpcbData);
			}
			return rawRegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
		}

		static LSTATUS WINAPI newRegEnumValueA(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (!lpcchValueName)
				{
					return ERROR_INVALID_PARAMETER;
				}
				if (dwIndex >= key->valueOrder.size())
				{
					return ERROR_NO_MORE_ITEMS;
				}

				std::string valueNameAnsi = WideToAnsiCodePage(key->valueOrder[dwIndex]);
				DWORD required = static_cast<DWORD>(valueNameAnsi.size());
				DWORD provided = *lpcchValueName;
				*lpcchValueName = required;
				if (lpValueName)
				{
					if (provided <= required)
					{
						if (provided > 0)
						{
							strncpy_s(lpValueName, provided, valueNameAnsi.c_str(), _TRUNCATE);
						}
						return ERROR_MORE_DATA;
					}
					strncpy_s(lpValueName, provided, valueNameAnsi.c_str(), _TRUNCATE);
				}

				return QueryVirtualRegistryValueLocked(key, key->valueOrder[dwIndex], true, 0, lpType, lpData, lpcbData);
			}
			return rawRegEnumValueA(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
		}

		static LSTATUS WINAPI newRegQueryInfoKeyW(
			HKEY hKey,
			LPWSTR lpClass,
			LPDWORD lpcchClass,
			LPDWORD lpReserved,
			LPDWORD lpcSubKeys,
			LPDWORD lpcbMaxSubKeyLen,
			LPDWORD lpcbMaxClassLen,
			LPDWORD lpcValues,
			LPDWORD lpcbMaxValueNameLen,
			LPDWORD lpcbMaxValueLen,
			LPDWORD lpcbSecurityDescriptor,
			PFILETIME lpftLastWriteTime)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (lpClass && lpcchClass && *lpcchClass > 0)
				{
					lpClass[0] = L'\0';
					*lpcchClass = 0;
				}
				if (lpcSubKeys) *lpcSubKeys = static_cast<DWORD>(key->subKeyOrder.size());
				if (lpcbMaxSubKeyLen) *lpcbMaxSubKeyLen = GetMaxVirtualRegistrySubKeyLength(key);
				if (lpcbMaxClassLen) *lpcbMaxClassLen = 0;
				if (lpcValues) *lpcValues = static_cast<DWORD>(key->valueOrder.size());
				if (lpcbMaxValueNameLen) *lpcbMaxValueNameLen = GetMaxVirtualRegistryValueNameLength(key);
				if (lpcbMaxValueLen) *lpcbMaxValueLen = GetMaxVirtualRegistryValueDataLength(key, false);
				if (lpcbSecurityDescriptor) *lpcbSecurityDescriptor = 0;
				if (lpftLastWriteTime)
				{
					memset(lpftLastWriteTime, 0, sizeof(FILETIME));
				}
				return ERROR_SUCCESS;
			}
			return rawRegQueryInfoKeyW(hKey, lpClass, lpcchClass, lpReserved, lpcSubKeys, lpcbMaxSubKeyLen, lpcbMaxClassLen, lpcValues, lpcbMaxValueNameLen, lpcbMaxValueLen, lpcbSecurityDescriptor, lpftLastWriteTime);
		}

		static LSTATUS WINAPI newRegQueryInfoKeyA(
			HKEY hKey,
			LPSTR lpClass,
			LPDWORD lpcchClass,
			LPDWORD lpReserved,
			LPDWORD lpcSubKeys,
			LPDWORD lpcbMaxSubKeyLen,
			LPDWORD lpcbMaxClassLen,
			LPDWORD lpcValues,
			LPDWORD lpcbMaxValueNameLen,
			LPDWORD lpcbMaxValueLen,
			LPDWORD lpcbSecurityDescriptor,
			PFILETIME lpftLastWriteTime)
		{
			ScopedVirtualRegistryLock lock;
			if (std::shared_ptr<VirtualRegistryKey> key = GetVirtualRegistryKeyByHandleLocked(hKey))
			{
				if (lpClass && lpcchClass && *lpcchClass > 0)
				{
					lpClass[0] = '\0';
					*lpcchClass = 0;
				}
				if (lpcSubKeys) *lpcSubKeys = static_cast<DWORD>(key->subKeyOrder.size());
				if (lpcbMaxSubKeyLen) *lpcbMaxSubKeyLen = GetMaxVirtualRegistrySubKeyLength(key);
				if (lpcbMaxClassLen) *lpcbMaxClassLen = 0;
				if (lpcValues) *lpcValues = static_cast<DWORD>(key->valueOrder.size());
				if (lpcbMaxValueNameLen) *lpcbMaxValueNameLen = GetMaxVirtualRegistryValueNameLength(key);
				if (lpcbMaxValueLen) *lpcbMaxValueLen = GetMaxVirtualRegistryValueDataLength(key, true);
				if (lpcbSecurityDescriptor) *lpcbSecurityDescriptor = 0;
				if (lpftLastWriteTime)
				{
					memset(lpftLastWriteTime, 0, sizeof(FILETIME));
				}
				return ERROR_SUCCESS;
			}
			return rawRegQueryInfoKeyA(hKey, lpClass, lpcchClass, lpReserved, lpcSubKeys, lpcbMaxSubKeyLen, lpcbMaxClassLen, lpcValues, lpcbMaxValueNameLen, lpcbMaxValueLen, lpcbSecurityDescriptor, lpftLastWriteTime);
		}

		bool HookRegistryAPIs()
		{
			bool hasFailed = false;
			hasFailed |= DetourAttachFunc(&rawRegOpenKeyExW, newRegOpenKeyExW);
			hasFailed |= DetourAttachFunc(&rawRegOpenKeyExA, newRegOpenKeyExA);
			hasFailed |= DetourAttachFunc(&rawRegOpenKeyW, newRegOpenKeyW);
			hasFailed |= DetourAttachFunc(&rawRegOpenKeyA, newRegOpenKeyA);
			hasFailed |= DetourAttachFunc(&rawRegCreateKeyExW, newRegCreateKeyExW);
			hasFailed |= DetourAttachFunc(&rawRegCreateKeyExA, newRegCreateKeyExA);
			hasFailed |= DetourAttachFunc(&rawRegCreateKeyW, newRegCreateKeyW);
			hasFailed |= DetourAttachFunc(&rawRegCreateKeyA, newRegCreateKeyA);
			hasFailed |= DetourAttachFunc(&rawRegCloseKey, newRegCloseKey);
			hasFailed |= DetourAttachFunc(&rawRegQueryValueExW, newRegQueryValueExW);
			hasFailed |= DetourAttachFunc(&rawRegQueryValueExA, newRegQueryValueExA);
			hasFailed |= DetourAttachFunc(&rawRegGetValueW, newRegGetValueW);
			hasFailed |= DetourAttachFunc(&rawRegGetValueA, newRegGetValueA);
			hasFailed |= DetourAttachFunc(&rawRegSetValueExW, newRegSetValueExW);
			hasFailed |= DetourAttachFunc(&rawRegSetValueExA, newRegSetValueExA);
			hasFailed |= DetourAttachFunc(&rawRegEnumKeyExW, newRegEnumKeyExW);
			hasFailed |= DetourAttachFunc(&rawRegEnumKeyExA, newRegEnumKeyExA);
			hasFailed |= DetourAttachFunc(&rawRegEnumValueW, newRegEnumValueW);
			hasFailed |= DetourAttachFunc(&rawRegEnumValueA, newRegEnumValueA);
			hasFailed |= DetourAttachFunc(&rawRegQueryInfoKeyW, newRegQueryInfoKeyW);
			hasFailed |= DetourAttachFunc(&rawRegQueryInfoKeyA, newRegQueryInfoKeyA);
			return !hasFailed;
		}
