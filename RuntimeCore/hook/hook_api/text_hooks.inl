		//*********Start Text Replace*********
		// 通配符匹配函数（ANSI版本）
		static bool WildcardMatchA(const char* pattern, const char* text)
		{
			const char* p = pattern;
			const char* t = text;
			const char* star = nullptr;
			const char* starText = nullptr;

			while (*t)
			{
				if (*p == '*')
				{
					star = p++;
					starText = t;
				}
				else if (*p == '?' || *p == *t)
				{
					p++;
					t++;
				}
				else if (star)
				{
					p = star + 1;
					t = ++starText;
				}
				else
				{
					return false;
				}
			}

			while (*p == '*') p++;
			return *p == '\0';
		}

		// 通配符匹配函数（Unicode版本）
		static bool WildcardMatchW(const wchar_t* pattern, const wchar_t* text)
		{
			const wchar_t* p = pattern;
			const wchar_t* t = text;
			const wchar_t* star = nullptr;
			const wchar_t* starText = nullptr;

			while (*t)
			{
				if (*p == L'*')
				{
					star = p++;
					starText = t;
				}
				else if (*p == L'?' || *p == *t)
				{
					p++;
					t++;
				}
				else if (star)
				{
					p = star + 1;
					t = ++starText;
				}
				else
				{
					return false;
				}
			}

			while (*p == L'*') p++;
			return *p == L'\0';
		}

		// 存储文字替换规则
		struct TextReplaceRule
		{
			std::string original;
			std::string replacement;
		};

		struct TextReplaceRuleW
		{
			std::wstring original;
			std::wstring replacement;
		};

		static std::vector<TextReplaceRule> sg_vecTextReplaceRulesA;
		static std::vector<TextReplaceRuleW> sg_vecTextReplaceRulesW;
		static UINT sg_textReadCodePage = CP_ACP;
		static UINT sg_textWriteCodePage = CP_ACP;
		static bool sg_enableTextReplaceVerboseLog = false;
		static bool sg_skipTextReplacement = false;
		static UINT sg_cnJpMapReadCodePage = CP_ACP;
		static bool sg_enableWaffleGetTextCrashPatch = false;
		static std::map<std::string, bool> sg_mapTextReplaceHitLogA;
		static std::map<std::wstring, bool> sg_mapTextReplaceHitLogW;
		static std::map<std::wstring, bool> sg_mapTextReplaceApiHitLog;
		static std::map<std::wstring, bool> sg_mapTextReplaceVerboseLog;
		static std::map<std::string, bool> sg_mapTextReplaceSkipSingleCharLogA;
		static std::map<std::wstring, bool> sg_mapTextReplaceSkipSingleCharLogW;
		static bool sg_enableCnJpMap = false;
		static bool sg_enableCnJpMapVerboseLog = false;
		static std::wstring sg_cnJpMapSourcePath;
		static std::unordered_map<wchar_t, wchar_t> sg_mapCnJpGlyphCharReverse;
		static std::unordered_map<wchar_t, wchar_t> sg_mapCnJpGlyphCharForward;
		static std::map<std::wstring, bool> sg_mapCnJpVerboseLog;
		static pExtTextOutW rawExtTextOutW = ExtTextOutW;
		static pGetGlyphIndicesW rawGetGlyphIndicesW = GetGlyphIndicesW;
		static int ApplyGlyphOffsetX(int x);
		static int ApplyGlyphOffsetY(int y);

		static std::wstring MultiByteToWideWithCodePage(const std::string& text, UINT codePage)
		{
			if (text.empty())
			{
				return L"";
			}

			UINT cp = codePage == 0 ? CP_ACP : codePage;
			int len = MultiByteToWideChar(cp, 0, text.data(), (int)text.size(), nullptr, 0);
			if (len <= 0)
			{
				return L"";
			}

			std::wstring result(len, L'\0');
			MultiByteToWideChar(cp, 0, text.data(), (int)text.size(), &result[0], len);
			return result;
		}

		static bool TryWideToMultiByteWithCodePage(const std::wstring& text, UINT codePage, std::string& result, bool rejectDefaultChar)
		{
			result.clear();
			if (text.empty())
			{
				return true;
			}

			UINT cp = codePage == 0 ? CP_ACP : codePage;
			int len = WideCharToMultiByte(cp, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
			if (len <= 0)
			{
				return false;
			}

			result.resize(len);
			if (cp == CP_UTF8)
			{
				return WideCharToMultiByte(cp, 0, text.data(), (int)text.size(), &result[0], len, nullptr, nullptr) > 0;
			}

			BOOL usedDefaultChar = FALSE;
			if (WideCharToMultiByte(cp, 0, text.data(), (int)text.size(), &result[0], len, nullptr, &usedDefaultChar) <= 0)
			{
				result.clear();
				return false;
			}

			if (rejectDefaultChar && usedDefaultChar)
			{
				result.clear();
				return false;
			}
			return true;
		}

		static void ClearCnJpMap()
		{
			sg_cnJpMapSourcePath.clear();
			sg_mapCnJpGlyphCharReverse.clear();
			sg_mapCnJpGlyphCharForward.clear();
		}

		static bool TryReadWholeFileForTextMap(const wchar_t* filePath, std::vector<BYTE>& bytes)
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

		static bool TryDecodeTextBytesToWide(const std::vector<BYTE>& bytes, std::wstring& text)
		{
			text.clear();
			if (bytes.empty())
			{
				return true;
			}

			if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
			{
				size_t charCount = (bytes.size() - 2) / sizeof(wchar_t);
				text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), charCount);
				return true;
			}

			size_t utf8Offset = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
			const char* utf8Data = reinterpret_cast<const char*>(bytes.data() + utf8Offset);
			int utf8Size = static_cast<int>(bytes.size() - utf8Offset);
			int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Data, utf8Size, nullptr, 0);
			if (len <= 0)
			{
				return false;
			}

			text.resize(len);
			return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Data, utf8Size, &text[0], len) > 0;
		}

		static void SkipJsonWhitespace(const std::wstring& text, size_t& pos)
		{
			while (pos < text.size() && iswspace(text[pos]))
			{
				++pos;
			}
		}

		static int HexDigitToInt(wchar_t ch)
		{
			if (ch >= L'0' && ch <= L'9')
			{
				return ch - L'0';
			}
			if (ch >= L'a' && ch <= L'f')
			{
				return 10 + (ch - L'a');
			}
			if (ch >= L'A' && ch <= L'F')
			{
				return 10 + (ch - L'A');
			}
			return -1;
		}

		static bool TryParseJsonString(const std::wstring& text, size_t& pos, std::wstring& out)
		{
			out.clear();
			if (pos >= text.size() || text[pos] != L'"')
			{
				return false;
			}

			++pos;
			while (pos < text.size())
			{
				wchar_t ch = text[pos++];
				if (ch == L'"')
				{
					return true;
				}
				if (ch != L'\\')
				{
					out.push_back(ch);
					continue;
				}

				if (pos >= text.size())
				{
					return false;
				}

				wchar_t escaped = text[pos++];
				switch (escaped)
				{
				case L'"': out.push_back(L'"'); break;
				case L'\\': out.push_back(L'\\'); break;
				case L'/': out.push_back(L'/'); break;
				case L'b': out.push_back(L'\b'); break;
				case L'f': out.push_back(L'\f'); break;
				case L'n': out.push_back(L'\n'); break;
				case L'r': out.push_back(L'\r'); break;
				case L't': out.push_back(L'\t'); break;
				case L'u':
				{
					if (pos + 4 > text.size())
					{
						return false;
					}

					int codePoint = 0;
					for (int i = 0; i < 4; ++i)
					{
						int value = HexDigitToInt(text[pos + i]);
						if (value < 0)
						{
							return false;
						}
						codePoint = (codePoint << 4) | value;
					}
					pos += 4;
					out.push_back(static_cast<wchar_t>(codePoint));
					break;
				}
				default:
					return false;
				}
			}

			return false;
		}

		static bool TryParseCnJpMapJson(const std::wstring& text)
		{
			ClearCnJpMap();

			size_t pos = 0;
			SkipJsonWhitespace(text, pos);
			if (pos >= text.size() || text[pos] != L'{')
			{
				return false;
			}

			++pos;
			while (true)
			{
				SkipJsonWhitespace(text, pos);
				if (pos >= text.size())
				{
					return false;
				}
				if (text[pos] == L'}')
				{
					++pos;
					break;
				}

				std::wstring leftValue;
				std::wstring rightValue;
				if (!TryParseJsonString(text, pos, leftValue))
				{
					return false;
				}

				SkipJsonWhitespace(text, pos);
				if (pos >= text.size() || text[pos] != L':')
				{
					return false;
				}
				++pos;
				SkipJsonWhitespace(text, pos);

				if (!TryParseJsonString(text, pos, rightValue))
				{
					return false;
				}

				if (leftValue.size() == 1 && rightValue.size() == 1)
				{
					sg_mapCnJpGlyphCharReverse[rightValue[0]] = leftValue[0];
					sg_mapCnJpGlyphCharForward[leftValue[0]] = rightValue[0];
				}

				SkipJsonWhitespace(text, pos);
				if (pos >= text.size())
				{
					return false;
				}
				if (text[pos] == L',')
				{
					++pos;
					continue;
				}
				if (text[pos] == L'}')
				{
					++pos;
					break;
				}
				return false;
			}

			return true;
		}

		void EnableCnJpMap(bool enable)
		{
			sg_enableCnJpMap = enable;
			LogMessage(LogLevel::Info, L"EnableCnJpMap: %s", enable ? L"true" : L"false");
		}

		void EnableCnJpMapVerboseLog(bool enable)
		{
			sg_enableCnJpMapVerboseLog = enable;
			if (!enable)
			{
				sg_mapCnJpVerboseLog.clear();
			}
			LogMessage(LogLevel::Info, L"EnableCnJpMapVerboseLog: %s", enable ? L"true" : L"false");
		}

		bool IsCnJpMapEnabled()
		{
			return sg_enableCnJpMap && !sg_mapCnJpGlyphCharReverse.empty();
		}

		static std::wstring BuildCnJpLogPreview(const wchar_t* text, int length)
		{
			if (!text)
			{
				return L"(null)";
			}
			if (length < 0)
			{
				length = (int)wcslen(text);
			}
			if (length <= 0)
			{
				return L"(empty)";
			}

			const int previewLimit = 24;
			int copyLength = length > previewLimit ? previewLimit : length;
			std::wstring preview(text, copyLength);
			if (copyLength < length)
			{
				preview += L"...";
			}
			return preview;
		}

		static std::wstring BuildAnsiByteHexPreview(const std::string& text)
		{
			if (text.empty())
			{
				return L"(empty)";
			}

			static const wchar_t* kHex = L"0123456789ABCDEF";
			const size_t previewLimit = 24;
			size_t count = text.size() > previewLimit ? previewLimit : text.size();
			std::wstring hex;
			hex.reserve(count * 3 + 8);
			for (size_t i = 0; i < count; ++i)
			{
				if (i > 0)
				{
					hex += L' ';
				}
				unsigned char value = static_cast<unsigned char>(text[i]);
				hex += kHex[(value >> 4) & 0x0F];
				hex += kHex[value & 0x0F];
			}
			if (count < text.size())
			{
				hex += L" ...";
			}
			return hex;
		}

		static std::wstring EscapeTextReplaceLogText(const std::wstring& text)
		{
			std::wstring escaped;
			escaped.reserve(text.size());
			for (wchar_t ch : text)
			{
				switch (ch)
				{
				case L'\r':
					escaped += L"\\r";
					break;
				case L'\n':
					escaped += L"\\n";
					break;
				case L'\t':
					escaped += L"\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}
			return escaped;
		}

		static bool ShouldLogTextReplaceVerboseKey(const std::wstring& key)
		{
			if (!sg_enableTextReplaceVerboseLog)
			{
				return false;
			}
			if (sg_mapTextReplaceVerboseLog.find(key) != sg_mapTextReplaceVerboseLog.end())
			{
				return false;
			}
			sg_mapTextReplaceVerboseLog[key] = true;
			return true;
		}

		static void LogTextReplaceVerboseObserveW(const std::wstring& text)
		{
			if (!ShouldLogTextReplaceVerboseKey(std::wstring(L"W|") + text))
			{
				return;
			}
			std::wstring escaped = EscapeTextReplaceLogText(text);
			LogMessage(LogLevel::Info, L"TextReplace verbose: observe kind=W len=%d text=\"%s\"",
				(int)text.size(), escaped.c_str());
		}

		static void LogTextReplaceVerboseObserveA(const std::string& text)
		{
			std::wstring decoded = MultiByteToWideWithCodePage(text, sg_textReadCodePage);
			if (decoded.empty() && !text.empty())
			{
				std::wstring bytesPreview = BuildAnsiByteHexPreview(text);
				std::wstring key = std::wstring(L"A|decode_failed|") + bytesPreview;
				if (!ShouldLogTextReplaceVerboseKey(key))
				{
					return;
				}
				LogMessage(LogLevel::Info, L"TextReplace verbose: observe kind=A readCp=%u decode=failed bytes=%s",
					sg_textReadCodePage, bytesPreview.c_str());
				return;
			}

			std::wstring key = std::wstring(L"A|") + decoded;
			if (!ShouldLogTextReplaceVerboseKey(key))
			{
				return;
			}
			std::wstring escaped = EscapeTextReplaceLogText(decoded);
			LogMessage(LogLevel::Info, L"TextReplace verbose: observe kind=A readCp=%u len=%d text=\"%s\"",
				sg_textReadCodePage, (int)decoded.size(), escaped.c_str());
		}

		static bool ShouldLogCnJpVerboseKey(const std::wstring& key)
		{
			if (!sg_enableCnJpMapVerboseLog)
			{
				return false;
			}
			if (sg_mapCnJpVerboseLog.find(key) != sg_mapCnJpVerboseLog.end())
			{
				return false;
			}
			sg_mapCnJpVerboseLog[key] = true;
			return true;
		}

		static void LogCnJpVerboseObserveA(const wchar_t* stage, const std::string& text, UINT codePage)
		{
			if (!sg_enableCnJpMapVerboseLog)
			{
				return;
			}

			std::wstring decoded = MultiByteToWideWithCodePage(text, codePage);
			std::wstring textPreview = decoded.empty() ? L"(decode_failed)" : BuildCnJpLogPreview(decoded.c_str(), (int)decoded.size());
			std::wstring bytesPreview = BuildAnsiByteHexPreview(text);
			std::wstring key = std::wstring(L"A|")
				+ (stage ? stage : L"(unknown)")
				+ L"\x1f" + std::to_wstring(codePage)
				+ L"\x1f" + textPreview
				+ L"\x1f" + bytesPreview;
			if (!ShouldLogCnJpVerboseKey(key))
			{
				return;
			}

			LogMessage(LogLevel::Info,
				L"CnJpMap verbose: stage=%s kind=A cp=%u bytes=%s text=\"%s\"",
				stage ? stage : L"(unknown)",
				codePage,
				bytesPreview.c_str(),
				textPreview.c_str());
		}

		static void LogCnJpVerboseObserveW(const wchar_t* stage, const std::wstring& text)
		{
			if (!sg_enableCnJpMapVerboseLog)
			{
				return;
			}

			std::wstring preview = BuildCnJpLogPreview(text.c_str(), (int)text.size());
			std::wstring key = std::wstring(L"W|")
				+ (stage ? stage : L"(unknown)")
				+ L"\x1f" + preview;
			if (!ShouldLogCnJpVerboseKey(key))
			{
				return;
			}

			LogMessage(LogLevel::Info,
				L"CnJpMap verbose: stage=%s kind=W len=%d text=\"%s\"",
				stage ? stage : L"(unknown)",
				(int)text.size(),
				preview.c_str());
		}

		static void LogCnJpVerboseDecision(const wchar_t* stage, const std::wstring& source, const std::wstring& mapped)
		{
			if (!sg_enableCnJpMapVerboseLog)
			{
				return;
			}

			bool changed = source != mapped;
			std::wstring srcPreview = BuildCnJpLogPreview(source.c_str(), (int)source.size());
			std::wstring dstPreview = BuildCnJpLogPreview(mapped.c_str(), (int)mapped.size());
			std::wstring key = std::wstring(L"D|")
				+ (stage ? stage : L"(unknown)")
				+ L"\x1f" + (changed ? L"1" : L"0")
				+ L"\x1f" + srcPreview
				+ L"\x1f" + dstPreview;
			if (!ShouldLogCnJpVerboseKey(key))
			{
				return;
			}

			LogMessage(LogLevel::Info,
				L"CnJpMap verbose: stage=%s changed=%d src=\"%s\" dst=\"%s\"",
				stage ? stage : L"(unknown)",
				changed ? 1 : 0,
				srcPreview.c_str(),
				dstPreview.c_str());
		}

		static void LogCnJpVerboseReason(const wchar_t* stage, const wchar_t* reason, const std::wstring& detail)
		{
			if (!sg_enableCnJpMapVerboseLog)
			{
				return;
			}

			std::wstring preview = BuildCnJpLogPreview(detail.c_str(), (int)detail.size());
			std::wstring key = std::wstring(L"R|")
				+ (stage ? stage : L"(unknown)")
				+ L"\x1f" + (reason ? reason : L"(unknown)")
				+ L"\x1f" + preview;
			if (!ShouldLogCnJpVerboseKey(key))
			{
				return;
			}

			LogMessage(LogLevel::Info,
				L"CnJpMap verbose: stage=%s reason=%s detail=\"%s\"",
				stage ? stage : L"(unknown)",
				reason ? reason : L"(unknown)",
				preview.c_str());
		}

		static void LogCnJpMapSamples()
		{
			if (sg_mapCnJpGlyphCharReverse.empty())
			{
				LogMessage(LogLevel::Warn, L"CnJpMap: parsed successfully but no single-char mappings were found");
				return;
			}

			uint32_t sampleIndex = 0;
			for (const auto& entry : sg_mapCnJpGlyphCharReverse)
			{
				if (sampleIndex >= 8)
				{
					break;
				}
				LogMessage(LogLevel::Debug, L"CnJpMap: sample[%u] U+%04X -> U+%04X",
					sampleIndex,
					static_cast<uint32_t>(entry.first),
					static_cast<uint32_t>(entry.second));
				++sampleIndex;
			}
		}

		bool LoadCnJpMapFile(const wchar_t* jsonFilePath)
		{
			ClearCnJpMap();
			if (!jsonFilePath || !jsonFilePath[0])
			{
				LogMessage(LogLevel::Error, L"CnJpMap: json path is empty");
				return false;
			}

			std::vector<BYTE> bytes;
			if (!TryReadWholeFileForTextMap(jsonFilePath, bytes))
			{
				LogMessage(LogLevel::Error, L"CnJpMap: failed to read %s", jsonFilePath);
				return false;
			}

			std::wstring jsonText;
			if (!TryDecodeTextBytesToWide(bytes, jsonText))
			{
				LogMessage(LogLevel::Error, L"CnJpMap: failed to decode %s (expect UTF-8 or UTF-16LE BOM)", jsonFilePath);
				return false;
			}

			if (!TryParseCnJpMapJson(jsonText))
			{
				LogMessage(LogLevel::Error, L"CnJpMap: failed to parse json object from %s", jsonFilePath);
				return false;
			}

			sg_cnJpMapSourcePath = jsonFilePath;
			LogMessage(LogLevel::Info, L"CnJpMap: loaded glyph-char mappings=%u from %s",
				static_cast<uint32_t>(sg_mapCnJpGlyphCharReverse.size()),
				sg_cnJpMapSourcePath.c_str());
			LogCnJpMapSamples();
			return true;
		}

		static std::string ProcessTextReplaceA(const char* text, int length);
		static std::wstring ProcessTextReplaceW(const wchar_t* text, int length);
		static std::wstring ProcessTextReplaceW(const wchar_t* text, int length, bool* matchedRule);

		static bool TryMapCnJpGlyphChar(wchar_t source, wchar_t& mapped)
		{
			mapped = source;
			if (!sg_enableCnJpMap)
			{
				return false;
			}

			auto it = sg_mapCnJpGlyphCharReverse.find(source);
			if (it == sg_mapCnJpGlyphCharReverse.end())
			{
				return false;
			}
			mapped = it->second;
			return mapped != source;
		}

		static bool TryMapCnJpGlyphSourceChar(wchar_t source, wchar_t& mapped)
		{
			mapped = source;
			if (!sg_enableCnJpMap)
			{
				return false;
			}

			auto it = sg_mapCnJpGlyphCharForward.find(source);
			if (it == sg_mapCnJpGlyphCharForward.end())
			{
				return false;
			}
			mapped = it->second;
			return mapped != source;
		}

		static std::wstring ProcessCnJpGlyphMapW(const wchar_t* text, int length)
		{
			if (!text) return L"";
			if (length < 0) length = (int)wcslen(text);
			if (length <= 0 || !sg_enableCnJpMap) return std::wstring(text, length);

			std::wstring mapped(text, length);
			bool changed = false;
			uint32_t changedCount = 0;
			wchar_t firstSource = 0;
			wchar_t firstMapped = 0;
			for (wchar_t& ch : mapped)
			{
				wchar_t newChar = ch;
				if (TryMapCnJpGlyphChar(ch, newChar))
				{
					if (!changed)
					{
						firstSource = ch;
						firstMapped = newChar;
					}
					ch = newChar;
					changed = true;
					++changedCount;
				}
			}
			static volatile LONG s_changedLogCount = 0;
			static volatile LONG s_unchangedLogCount = 0;
			if (changed)
			{
				LONG logIndex = InterlockedIncrement(&s_changedLogCount);
				if (logIndex <= 20)
				{
					LogMessage(LogLevel::Debug,
						L"CnJpMap: glyph remap hit #%ld changed=%u first=U+%04X->U+%04X src=\"%s\" dst=\"%s\"",
						logIndex,
						changedCount,
						static_cast<uint32_t>(firstSource),
						static_cast<uint32_t>(firstMapped),
						BuildCnJpLogPreview(text, length).c_str(),
						BuildCnJpLogPreview(mapped.c_str(), (int)mapped.size()).c_str());
				}
			}
			else
			{
				LONG logIndex = InterlockedIncrement(&s_unchangedLogCount);
				if (logIndex <= 10)
				{
					LogMessage(LogLevel::Debug,
						L"CnJpMap: glyph remap miss #%ld len=%d preview=\"%s\"",
						logIndex,
						length,
						BuildCnJpLogPreview(text, length).c_str());
				}
			}
			LogCnJpVerboseDecision(L"ProcessCnJpGlyphMapW", std::wstring(text, length), changed ? mapped : std::wstring(text, length));
			return changed ? mapped : std::wstring(text, length);
		}

		static std::wstring ProcessCnJpGlyphSourceMapW(const wchar_t* text, int length)
		{
			if (!text) return L"";
			if (length < 0) length = (int)wcslen(text);
			if (length <= 0 || !sg_enableCnJpMap) return std::wstring(text, length);

			std::wstring mapped(text, length);
			bool changed = false;
			for (wchar_t& ch : mapped)
			{
				wchar_t newChar = ch;
				if (TryMapCnJpGlyphSourceChar(ch, newChar))
				{
					ch = newChar;
					changed = true;
				}
			}
			LogCnJpVerboseDecision(L"ProcessCnJpGlyphSourceMapW", std::wstring(text, length), changed ? mapped : std::wstring(text, length));
			return changed ? mapped : std::wstring(text, length);
		}

		static std::string ProcessCnJpGlyphMapA(const char* text, int length)
		{
			if (!text) return "";
			if (length < 0) length = (int)strlen(text);
			if (length <= 0 || !sg_enableCnJpMap) return std::string(text, length);

			std::string original(text, length);
			LogCnJpVerboseObserveA(L"ProcessCnJpGlyphMapA", original, sg_cnJpMapReadCodePage);
			std::wstring wide = MultiByteToWideWithCodePage(original, sg_cnJpMapReadCodePage);
			if (wide.empty())
			{
				LogCnJpVerboseReason(L"ProcessCnJpGlyphMapA", L"decode_failed", BuildAnsiByteHexPreview(original));
				return original;
			}

			std::wstring mappedWide = ProcessCnJpGlyphMapW(wide.c_str(), (int)wide.size());
			if (mappedWide == wide)
			{
				return original;
			}

			std::string mappedA;
			if (!TryWideToMultiByteWithCodePage(mappedWide, sg_cnJpMapReadCodePage, mappedA, true))
			{
				LogCnJpVerboseReason(L"ProcessCnJpGlyphMapA", L"encode_failed", mappedWide);
				return original;
			}

			return mappedA;
		}

		static std::wstring ProcessGlyphStageW(const wchar_t* text, int length);

		static std::string ProcessGlyphStageA(const char* text, int length)
		{
			if (!text) return "";
			if (length < 0) length = (int)strlen(text);
			if (length <= 0) return "";

			std::string source(text, length);
			LogCnJpVerboseObserveA(L"ProcessGlyphStageA", source, sg_cnJpMapReadCodePage);
			std::wstring wide = MultiByteToWideWithCodePage(source, sg_cnJpMapReadCodePage);
			if (wide.empty())
			{
				LogCnJpVerboseReason(L"ProcessGlyphStageA", L"decode_failed_fallback_to_replace", BuildAnsiByteHexPreview(source));
				return ProcessTextReplaceA(text, length);
			}

			std::wstring mappedWide = ProcessGlyphStageW(wide.c_str(), (int)wide.size());
			if (mappedWide == wide)
			{
				return source;
			}

			std::string mappedA;
			if (!TryWideToMultiByteWithCodePage(mappedWide, sg_cnJpMapReadCodePage, mappedA, true))
			{
				LogCnJpVerboseReason(L"ProcessGlyphStageA", L"encode_failed_return_original", mappedWide);
				return source;
			}

			return mappedA;
		}

		static std::wstring ProcessGlyphStageW(const wchar_t* text, int length)
		{
			std::wstring source(text ? text : L"", (text == nullptr) ? 0 : (length < 0 ? (int)wcslen(text) : length));
			if (!source.empty())
			{
				LogCnJpVerboseObserveW(L"ProcessGlyphStageW.source", source);
			}
			bool matchedTextReplaceRule = false;
			std::wstring replaced = ProcessTextReplaceW(text, length, &matchedTextReplaceRule);
			if (matchedTextReplaceRule)
			{
				LogCnJpVerboseReason(L"ProcessGlyphStageW", L"text_replace_hit_skip_cn_jp_map", replaced);
				LogCnJpVerboseDecision(L"ProcessGlyphStageW.final", source, replaced);
				return replaced;
			}
			std::wstring mapped = ProcessCnJpGlyphMapW(replaced.c_str(), (int)replaced.size());
			LogCnJpVerboseDecision(L"ProcessGlyphStageW.final", source, mapped);
			return mapped;
		}

		static std::wstring ProcessGlyphQueryStageW(const wchar_t* text, int length)
		{
			std::wstring display = ProcessGlyphStageW(text, length);
			LogCnJpVerboseDecision(L"ProcessGlyphQueryStageW", display, display);
			return display;
		}

		static bool TryBuildGlyphIndicesForTextW(HDC hdc, const std::wstring& text, std::vector<WORD>& glyphIndices)
		{
			glyphIndices.clear();
			if (!hdc || text.empty())
			{
				return false;
			}

			glyphIndices.resize(text.size());
			DWORD count = rawGetGlyphIndicesW(hdc,
				text.c_str(),
				(int)text.size(),
				glyphIndices.data(),
				GGI_MARK_NONEXISTING_GLYPHS);
			if (count == GDI_ERROR || count != text.size())
			{
				glyphIndices.clear();
				return false;
			}

			for (WORD glyphIndex : glyphIndices)
			{
				if (glyphIndex == 0xFFFF)
				{
					glyphIndices.clear();
					return false;
				}
			}
			return true;
		}

		static BOOL TryDrawGlyphIndexFallbackW(HDC hdc,
			int x,
			int y,
			UINT options,
			const RECT* lprect,
			const wchar_t* displayText,
			int length,
			const INT* lpDx,
			BOOL& handled)
		{
			handled = FALSE;
			if (!displayText || length <= 0 || (options & ETO_GLYPH_INDEX) != 0)
			{
				return FALSE;
			}
			return FALSE;
		}

		std::wstring ProcessGlyphStageTextW(const wchar_t* text, int length)
		{
			return ProcessGlyphStageW(text, length);
		}

		static bool IsGlobalWildcardPatternA(const std::string& pattern)
		{
			return pattern.size() == 1 && pattern[0] == '*';
		}

		static bool IsGlobalWildcardPatternW(const std::wstring& pattern)
		{
			return pattern.size() == 1 && pattern[0] == L'*';
		}

		static void LogTextReplaceApiHitA(const wchar_t* apiName, const std::string& source, const std::string& replaced)
		{
			std::wstring srcW = MultiByteToWideWithCodePage(source, sg_textReadCodePage);
			std::wstring dstW = MultiByteToWideWithCodePage(replaced, sg_textWriteCodePage);
			if (srcW.empty())
			{
				srcW = L"(decode_failed)";
			}
			if (dstW.empty())
			{
				dstW = L"(decode_failed)";
			}

			std::wstring key = std::wstring(apiName ? apiName : L"(unknown)")
				+ L"\x1f" + srcW + L"\x1f" + dstW;
			if (sg_mapTextReplaceApiHitLog.find(key) != sg_mapTextReplaceApiHitLog.end())
			{
				return;
			}
			sg_mapTextReplaceApiHitLog[key] = true;
			LogMessage(LogLevel::Info, L"TextReplace API hit: api=%s readCp=%u writeCp=%u from=\"%s\" to=\"%s\"",
				apiName ? apiName : L"(unknown)", sg_textReadCodePage, sg_textWriteCodePage, srcW.c_str(), dstW.c_str());
		}

		static void LogTextReplaceApiHitW(const wchar_t* apiName, const std::wstring& source, const std::wstring& replaced)
		{
			std::wstring key = std::wstring(apiName ? apiName : L"(unknown)")
				+ L"\x1f" + source + L"\x1f" + replaced;
			if (sg_mapTextReplaceApiHitLog.find(key) != sg_mapTextReplaceApiHitLog.end())
			{
				return;
			}
			sg_mapTextReplaceApiHitLog[key] = true;
			LogMessage(LogLevel::Info, L"TextReplace API hit: api=%s from=\"%s\" to=\"%s\"",
				apiName ? apiName : L"(unknown)", source.c_str(), replaced.c_str());
		}

		void SetTextReplaceEncoding(uint32_t codePage)
		{
			SetTextReplaceEncodings(codePage, codePage);
		}

		void SetTextReplaceEncodings(uint32_t readCodePage, uint32_t writeCodePage)
		{
			sg_textReadCodePage = readCodePage == 0 ? CP_ACP : (UINT)readCodePage;
			sg_textWriteCodePage = writeCodePage == 0 ? sg_textReadCodePage : (UINT)writeCodePage;
			LogMessage(LogLevel::Info, L"SetTextReplaceEncodings: read=%u write=%u", sg_textReadCodePage, sg_textWriteCodePage);
		}

		void EnableTextReplaceVerboseLog(bool enable)
		{
			sg_enableTextReplaceVerboseLog = enable;
			if (!enable)
			{
				sg_mapTextReplaceVerboseLog.clear();
			}
			LogMessage(LogLevel::Info, L"EnableTextReplaceVerboseLog: %s", enable ? L"true" : L"false");
		}

		void SetTextReplaceBypass(bool bypass)
		{
			sg_skipTextReplacement = bypass;
			LogMessage(LogLevel::Debug, L"SetTextReplaceBypass: %s", bypass ? L"true" : L"false");
		}

		void SetWaffleGetTextCrashPatchEnabled(bool enable)
		{
			sg_enableWaffleGetTextCrashPatch = enable;
			LogMessage(LogLevel::Info, L"SetWaffleGetTextCrashPatchEnabled: %d", enable ? 1 : 0);
		}

		void SetCnJpMapEncoding(uint32_t codePage)
		{
			sg_cnJpMapReadCodePage = codePage == 0 ? CP_ACP : (UINT)codePage;
			LogMessage(LogLevel::Info, L"SetCnJpMapEncoding: cp=%u", sg_cnJpMapReadCodePage);
		}

		void AddTextReplaceRule(const char* original, const char* replacement)
		{
			if (original && replacement)
			{
				TextReplaceRule rule;
				rule.original = original;
				rule.replacement = replacement;
				sg_vecTextReplaceRulesA.push_back(rule);
			}
		}

		void AddTextReplaceRuleW(const wchar_t* original, const wchar_t* replacement)
		{
			if (original && replacement)
			{
				TextReplaceRuleW rule;
				rule.original = original;
				rule.replacement = replacement;
				sg_vecTextReplaceRulesW.push_back(rule);
			}
		}

		// 执行文字替换（ANSI版本）
		static std::string ProcessTextReplaceA(const char* text, int length)
		{
			if (!text) return "";
			if (length < 0) length = (int)strlen(text);
			if (length <= 0) return "";
			
			std::string str(text, length);
			if (sg_skipTextReplacement)
			{
				return str;
			}
			LogTextReplaceVerboseObserveA(str);
			LogCnJpVerboseObserveA(L"ProcessTextReplaceA", str, sg_textReadCodePage);
			
			for (const auto& rule : sg_vecTextReplaceRulesA)
			{
				if (WildcardMatchA(rule.original.c_str(), str.c_str()))
				{
					if (length <= 1 && IsGlobalWildcardPatternA(rule.original))
					{
						if (sg_mapTextReplaceSkipSingleCharLogA.find(str) == sg_mapTextReplaceSkipSingleCharLogA.end())
						{
							sg_mapTextReplaceSkipSingleCharLogA[str] = true;
							std::wstring srcW = MultiByteToWideWithCodePage(str, sg_textReadCodePage);
							if (srcW.empty())
							{
								srcW = L"(decode_failed)";
							}
							LogMessage(LogLevel::Warn, L"TextReplaceA skip: reason=global_wildcard_single_char readCp=%u text=\"%s\"",
								sg_textReadCodePage, srcW.c_str());
						}
						continue;
					}
					std::string hitKey = rule.original + std::string("\x1f") + rule.replacement;
					if (sg_mapTextReplaceHitLogA.find(hitKey) == sg_mapTextReplaceHitLogA.end())
					{
						sg_mapTextReplaceHitLogA[hitKey] = true;
						std::wstring srcW = MultiByteToWideWithCodePage(str, sg_textReadCodePage);
						std::wstring dstW = MultiByteToWideWithCodePage(rule.replacement, sg_textWriteCodePage);
						if (srcW.empty())
						{
							srcW = L"(decode_failed)";
						}
						if (dstW.empty())
						{
							dstW = L"(decode_failed)";
						}
						LogMessage(LogLevel::Info, L"TextReplaceA hit: readCp=%u writeCp=%u from=\"%s\" to=\"%s\"",
							sg_textReadCodePage, sg_textWriteCodePage, srcW.c_str(), dstW.c_str());
					}
					str = rule.replacement;
					break;
				}
			}

			return str;
		}

		// 执行文字替换（Unicode版本）
		static std::wstring ProcessTextReplaceW(const wchar_t* text, int length)
		{
			return ProcessTextReplaceW(text, length, nullptr);
		}

		static std::wstring ProcessTextReplaceW(const wchar_t* text, int length, bool* matchedRule)
		{
			if (matchedRule)
			{
				*matchedRule = false;
			}
			if (!text) return L"";
			if (length < 0) length = (int)wcslen(text);
			if (length <= 0) return L"";
			
			std::wstring str(text, length);
			if (sg_skipTextReplacement)
			{
				return str;
			}
			LogTextReplaceVerboseObserveW(str);
			LogCnJpVerboseObserveW(L"ProcessTextReplaceW", str);
			
			for (const auto& rule : sg_vecTextReplaceRulesW)
			{
				if (WildcardMatchW(rule.original.c_str(), str.c_str()))
				{
					if (length <= 1 && IsGlobalWildcardPatternW(rule.original))
					{
						if (sg_mapTextReplaceSkipSingleCharLogW.find(str) == sg_mapTextReplaceSkipSingleCharLogW.end())
						{
							sg_mapTextReplaceSkipSingleCharLogW[str] = true;
							LogMessage(LogLevel::Warn, L"TextReplaceW skip: reason=global_wildcard_single_char text=\"%s\"",
								str.c_str());
						}
						continue;
					}
					if (matchedRule)
					{
						*matchedRule = true;
					}
					std::wstring hitKey = rule.original + std::wstring(L"\x1f") + rule.replacement;
					if (sg_mapTextReplaceHitLogW.find(hitKey) == sg_mapTextReplaceHitLogW.end())
					{
						sg_mapTextReplaceHitLogW[hitKey] = true;
						LogMessage(LogLevel::Info, L"TextReplaceW hit: from=\"%s\" to=\"%s\"",
							str.c_str(), rule.replacement.c_str());
					}
					str = rule.replacement;
					break;
				}
			}

			return str;
		}

		//*********Hook TextOutA*********
		static pTextOutA rawTextOutA = TextOutA;
		static std::map<std::string, std::string> sg_mapTextCacheA;
		static const int kSpacingExtraNotApplied = 0x7fffffff;

		static int ComputeSpacingExtra(HDC hdc)
		{
			if (hdc == nullptr || sg_fFontSpacingScale == 1.0f)
			{
				return 0;
			}
			TEXTMETRICW tm = {};
			if (!GetTextMetricsW(hdc, &tm))
			{
				return 0;
			}
			int baseWidth = tm.tmAveCharWidth;
			if (baseWidth <= 0)
			{
				int baseHeight = tm.tmHeight < 0 ? -tm.tmHeight : tm.tmHeight;
				baseWidth = baseHeight > 0 ? baseHeight / 2 : 0;
			}
			if (baseWidth <= 0)
			{
				return 0;
			}
			float delta = (sg_fFontSpacingScale - 1.0f) * (float)baseWidth;
			return delta >= 0.0f ? (int)(delta + 0.5f) : (int)(delta - 0.5f);
		}

		static int ApplySpacingExtra(HDC hdc)
		{
			int extra = ComputeSpacingExtra(hdc);
			if (extra == 0)
			{
				return kSpacingExtraNotApplied;
			}
			int previousExtra = SetTextCharacterExtra(hdc, extra);
			if (previousExtra == 0x80000000)
			{
				return kSpacingExtraNotApplied;
			}
			return previousExtra;
		}

		static void RestoreSpacingExtra(HDC hdc, int previousExtra)
		{
			if (previousExtra == kSpacingExtraNotApplied)
			{
				return;
			}
			SetTextCharacterExtra(hdc, previousExtra);
		}

		static const INT* BuildAdjustedDx(const INT* lpDx, UINT c, int extra, std::vector<INT>& buffer)
		{
			if (lpDx == nullptr || c == 0 || extra == 0)
			{
				return lpDx;
			}
			buffer.assign(lpDx, lpDx + c);
			for (UINT i = 0; i < c; ++i)
			{
				buffer[i] += extra;
			}
			return buffer.data();
		}

		static int ApplyGlyphOffsetX(int x)
		{
			return x;
		}

		static int ApplyGlyphOffsetY(int y)
		{
			return y;
		}

		struct AnsiGlyphStageResult
		{
			std::string sourceAnsi;
			std::wstring sourceWide;
			std::wstring mappedWide;
			std::string mappedAnsi;
			bool changedWide = false;
			bool changedAnsi = false;
			bool needUnicodeFallback = false;
		};

		static bool BuildAnsiGlyphStageResult(const char* text, int length, AnsiGlyphStageResult& result)
		{
			result = AnsiGlyphStageResult{};
			if (!text)
			{
				return false;
			}
			if (length < 0)
			{
				length = (int)strlen(text);
			}

			result.sourceAnsi.assign(text, length);
			if (length <= 0)
			{
				return true;
			}
			LogCnJpVerboseObserveA(L"BuildAnsiGlyphStageResult.sourceAnsi", result.sourceAnsi, sg_cnJpMapReadCodePage);

			result.sourceWide = MultiByteToWideWithCodePage(result.sourceAnsi, sg_cnJpMapReadCodePage);
			if (result.sourceWide.empty())
			{
				LogCnJpVerboseReason(L"BuildAnsiGlyphStageResult", L"decode_failed", BuildAnsiByteHexPreview(result.sourceAnsi));
				return false;
			}

			result.mappedWide = ProcessGlyphStageW(result.sourceWide.c_str(), (int)result.sourceWide.size());
			result.changedWide = (result.mappedWide != result.sourceWide);
			LogCnJpVerboseDecision(L"BuildAnsiGlyphStageResult.wide", result.sourceWide, result.mappedWide);
			if (!result.changedWide)
			{
				return true;
			}

			if (TryWideToMultiByteWithCodePage(result.mappedWide, sg_cnJpMapReadCodePage, result.mappedAnsi, true))
			{
				result.changedAnsi = (result.mappedAnsi != result.sourceAnsi);
				if (result.changedAnsi)
				{
					LogCnJpVerboseObserveA(L"BuildAnsiGlyphStageResult.mappedAnsi", result.mappedAnsi, sg_cnJpMapReadCodePage);
				}
			}
			else
			{
				result.needUnicodeFallback = true;
				LogCnJpVerboseReason(L"BuildAnsiGlyphStageResult", L"encode_failed_need_unicode_fallback", result.mappedWide);
			}
			return true;
		}

		static void LogAnsiUnicodeFallback(const wchar_t* apiName, const AnsiGlyphStageResult& result)
		{
			static volatile LONG s_unicodeFallbackLogCount = 0;
			LONG logIndex = InterlockedIncrement(&s_unicodeFallbackLogCount);
			if (logIndex > 20)
			{
				return;
			}

			LogMessage(LogLevel::Debug,
				L"CnJpMap: unicode fallback api=%s cp=%u src=\"%s\" dst=\"%s\"",
				apiName ? apiName : L"(unknown)",
				sg_cnJpMapReadCodePage,
				BuildCnJpLogPreview(result.sourceWide.c_str(), (int)result.sourceWide.size()).c_str(),
				BuildCnJpLogPreview(result.mappedWide.c_str(), (int)result.mappedWide.size()).c_str());
		}

		static BOOL TryTextOutAUnicodeFallback(HDC hdc, int x, int y, LPCSTR lpString, int length, BOOL& handled);
		static BOOL TryExtTextOutAUnicodeFallback(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx, BOOL& handled);
		static int TryDrawTextAUnicodeFallback(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, int& handled);
		static int TryDrawTextExAUnicodeFallback(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp, int& handled);
		static BOOL TryGetTextExtentPoint32AUnicodeFallback(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl, BOOL& handled);
		static BOOL TryGetTextExtentExPointAUnicodeFallback(HDC hdc, LPCSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize, BOOL& handled);
		static BOOL TryGetTextExtentPointAUnicodeFallback(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz, BOOL& handled);
		static DWORD TryGetGlyphOutlineAUnicodeFallback(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2, bool& handled);

		static RECT BuildOffsetRect(const RECT* rect)
		{
			RECT adjusted = *rect;
			return adjusted;
		}

		static void CommitAdjustedRect(LPRECT originalRect, const RECT& adjustedRect)
		{
			if (originalRect)
			{
				*originalRect = adjustedRect;
			}
		}

		struct ScopedDrawHdcFontOverride
		{
			HDC hdc = nullptr;
			HFONT oldFont = nullptr;
			HFONT newFont = nullptr;

			explicit ScopedDrawHdcFontOverride(HDC targetHdc)
				: hdc(targetHdc)
			{
				newFont = ReplaceHdcFont(hdc, &oldFont);
			}

			~ScopedDrawHdcFontOverride()
			{
				RestoreHdcFont(hdc, oldFont, newFont);
			}
		};

		BOOL WINAPI newTextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			int length = c < 0 ? (lpString ? (int)strlen(lpString) : 0) : c;
			BOOL handled = FALSE;
			BOOL unicodeRet = TryTextOutAUnicodeFallback(hdc, x, y, lpString, length, handled);
			if (handled)
			{
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			std::string replaced = ProcessGlyphStageA(lpString, length);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::string(lpString, length))
			{
				LogTextReplaceApiHitA(L"TextOutA", std::string(lpString, length), replaced);
				ret = rawTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), replaced.c_str(), (int)replaced.length());
			}
			else
			{
				ret = rawTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), lpString, c);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookTextOutA()
		{
			return DetourAttachFunc(&rawTextOutA, newTextOutA);
		}
		//*********END Hook TextOutA*********

		//*********Hook TextOutW*********
		static pTextOutW rawTextOutW = TextOutW;
		static std::map<std::wstring, std::wstring> sg_mapTextCacheW;

		static BOOL TryTextOutAUnicodeFallback(HDC hdc, int x, int y, LPCSTR lpString, int length, BOOL& handled)
		{
			handled = FALSE;
			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpString, length, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return FALSE;
			}

			LogAnsiUnicodeFallback(L"TextOutA", glyphStage);
			handled = TRUE;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL glyphHandled = FALSE;
			BOOL glyphRet = TryDrawGlyphIndexFallbackW(hdc, x, y, 0, nullptr, glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length(), nullptr, glyphHandled);
			if (glyphHandled)
			{
				return glyphRet;
			}
			return rawTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length());
		}

		BOOL WINAPI newTextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = c < 0 ? (lpString ? (int)wcslen(lpString) : 0) : c;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"TextOutW", std::wstring(lpString, length), replaced);
				BOOL glyphHandled = FALSE;
				ret = TryDrawGlyphIndexFallbackW(hdc, x, y, 0, nullptr, replaced.c_str(), (int)replaced.length(), nullptr, glyphHandled);
				if (!glyphHandled)
				{
					ret = rawTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), replaced.c_str(), (int)replaced.length());
				}
			}
			else
			{
				BOOL glyphHandled = FALSE;
				ret = TryDrawGlyphIndexFallbackW(hdc, x, y, 0, nullptr, lpString, length, nullptr, glyphHandled);
				if (!glyphHandled)
				{
					ret = rawTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), lpString, c);
				}
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookTextOutW()
		{
			return DetourAttachFunc(&rawTextOutW, newTextOutW);
		}
		//*********END Hook TextOutW*********

		//*********Hook ExtTextOutA*********
		static pExtTextOutA rawExtTextOutA = ExtTextOutA;

		BOOL WINAPI newExtTextOutA(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx)
		{
			int previousExtra = lpDx == nullptr ? ApplySpacingExtra(hdc) : kSpacingExtraNotApplied;
			int dxExtra = lpDx == nullptr ? 0 : ComputeSpacingExtra(hdc);
			std::vector<INT> adjustedDx;
			const INT* effectiveDx = BuildAdjustedDx(lpDx, c, dxExtra, adjustedDx);
			const RECT* effectiveRect = lprect;
			RECT adjustedRect = {};
			if (lprect)
			{
				adjustedRect = BuildOffsetRect(lprect);
				effectiveRect = &adjustedRect;
			}
			if ((options & ETO_GLYPH_INDEX) != 0)
			{
				ScopedDrawHdcFontOverride fontOverride(hdc);
				BOOL ret = rawExtTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
				RestoreSpacingExtra(hdc, previousExtra);
				return ret;
			}
			BOOL handled = FALSE;
			BOOL unicodeRet = TryExtTextOutAUnicodeFallback(hdc, x, y, options, effectiveRect, lpString, c, lpDx, handled);
			if (handled)
			{
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			std::string replaced = ProcessGlyphStageA(lpString, c);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (!replaced.empty() && replaced != std::string(lpString, c))
			{
				if (lpDx != nullptr && replaced.length() != c)
				{
					LogMessage(LogLevel::Warn, L"TextReplace API skip: api=ExtTextOutA reason=lpDx_length_mismatch srcLen=%u dstLen=%u",
						c, (UINT)replaced.length());
					ret = rawExtTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
					RestoreSpacingExtra(hdc, previousExtra);
					return ret;
				}
				LogTextReplaceApiHitA(L"ExtTextOutA", std::string(lpString, c), replaced);
				ret = rawExtTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, replaced.c_str(), (UINT)replaced.length(), effectiveDx);
			}
			else
			{
				ret = rawExtTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookExtTextOutA()
		{
			return DetourAttachFunc(&rawExtTextOutA, newExtTextOutA);
		}
		//*********END Hook ExtTextOutA*********

		//*********Hook ExtTextOutW*********
		static BOOL TryExtTextOutAUnicodeFallback(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx, BOOL& handled)
		{
			handled = FALSE;
			if ((options & ETO_GLYPH_INDEX) != 0 || lpDx != nullptr)
			{
				return FALSE;
			}

			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpString, (int)c, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return FALSE;
			}

			LogAnsiUnicodeFallback(L"ExtTextOutA", glyphStage);
			handled = TRUE;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL glyphHandled = FALSE;
			BOOL glyphRet = TryDrawGlyphIndexFallbackW(hdc, x, y, options, lprect, glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length(), lpDx, glyphHandled);
			if (glyphHandled)
			{
				return glyphRet;
			}
			return rawExtTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, lprect, glyphStage.mappedWide.c_str(), (UINT)glyphStage.mappedWide.length(), nullptr);
		}

		BOOL WINAPI newExtTextOutW(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCWSTR lpString, UINT c, CONST INT* lpDx)
		{
			int previousExtra = lpDx == nullptr ? ApplySpacingExtra(hdc) : kSpacingExtraNotApplied;
			int dxExtra = lpDx == nullptr ? 0 : ComputeSpacingExtra(hdc);
			std::vector<INT> adjustedDx;
			const INT* effectiveDx = BuildAdjustedDx(lpDx, c, dxExtra, adjustedDx);
			const RECT* effectiveRect = lprect;
			RECT adjustedRect = {};
			if (lprect)
			{
				adjustedRect = BuildOffsetRect(lprect);
				effectiveRect = &adjustedRect;
			}
			if ((options & ETO_GLYPH_INDEX) != 0)
			{
				ScopedDrawHdcFontOverride fontOverride(hdc);
				BOOL ret = rawExtTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
				RestoreSpacingExtra(hdc, previousExtra);
				return ret;
			}
			std::wstring replaced = ProcessGlyphStageW(lpString, c);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (!replaced.empty() && replaced != std::wstring(lpString, c))
			{
				if (lpDx != nullptr && replaced.length() != c)
				{
					LogMessage(LogLevel::Warn, L"TextReplace API skip: api=ExtTextOutW reason=lpDx_length_mismatch srcLen=%u dstLen=%u",
						c, (UINT)replaced.length());
					ret = rawExtTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
					RestoreSpacingExtra(hdc, previousExtra);
					return ret;
				}
				LogTextReplaceApiHitW(L"ExtTextOutW", std::wstring(lpString, c), replaced);
				BOOL glyphHandled = FALSE;
				ret = TryDrawGlyphIndexFallbackW(hdc, x, y, options, effectiveRect, replaced.c_str(), (int)replaced.length(), effectiveDx, glyphHandled);
				if (!glyphHandled)
				{
					ret = rawExtTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, replaced.c_str(), (UINT)replaced.length(), effectiveDx);
				}
			}
			else
			{
				BOOL glyphHandled = FALSE;
				ret = TryDrawGlyphIndexFallbackW(hdc, x, y, options, effectiveRect, lpString, (int)c, effectiveDx, glyphHandled);
				if (!glyphHandled)
				{
					ret = rawExtTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), options, effectiveRect, lpString, c, effectiveDx);
				}
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookExtTextOutW()
		{
			return DetourAttachFunc(&rawExtTextOutW, newExtTextOutW);
		}
		//*********END Hook ExtTextOutW*********

		//*********Hook DrawTextA*********
		static pDrawTextA rawDrawTextA = DrawTextA;

		int WINAPI newDrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchText == -1 ? (int)strlen(lpchText) : cchText;
			std::string replaced = ProcessGlyphStageA(lpchText, length);
			const RECT* effectiveRect = lprc;
			RECT adjustedRect = {};
			if (lprc)
			{
				adjustedRect = BuildOffsetRect(lprc);
				effectiveRect = &adjustedRect;
			}
			int handled = 0;
			int unicodeRet = TryDrawTextAUnicodeFallback(hdc, lpchText, length, const_cast<LPRECT>(effectiveRect), format, handled);
			if (handled)
			{
				CommitAdjustedRect(lprc, adjustedRect);
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			ScopedDrawHdcFontOverride fontOverride(hdc);
			int ret = 0;
			if (!replaced.empty() && replaced != std::string(lpchText, length))
			{
				LogTextReplaceApiHitA(L"DrawTextA", std::string(lpchText, length), replaced);
				ret = rawDrawTextA(hdc, replaced.c_str(), (int)replaced.length(), const_cast<LPRECT>(effectiveRect), format);
			}
			else
			{
				ret = rawDrawTextA(hdc, lpchText, cchText, const_cast<LPRECT>(effectiveRect), format);
			}
			CommitAdjustedRect(lprc, adjustedRect);
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookDrawTextA()
		{
			return DetourAttachFunc(&rawDrawTextA, newDrawTextA);
		}
		//*********END Hook DrawTextA*********

		//*********Hook DrawTextW*********
		static pDrawTextW rawDrawTextW = DrawTextW;

		static int TryDrawTextAUnicodeFallback(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, int& handled)
		{
			handled = 0;
			if ((format & DT_MODIFYSTRING) != 0)
			{
				return 0;
			}

			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpchText, cchText, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return 0;
			}

			LogAnsiUnicodeFallback(L"DrawTextA", glyphStage);
			handled = 1;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawDrawTextW(hdc, glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length(), lprc, format);
		}

		int WINAPI newDrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchText == -1 ? (int)wcslen(lpchText) : cchText;
			std::wstring replaced = ProcessGlyphStageW(lpchText, length);
			const RECT* effectiveRect = lprc;
			RECT adjustedRect = {};
			if (lprc)
			{
				adjustedRect = BuildOffsetRect(lprc);
				effectiveRect = &adjustedRect;
			}
			ScopedDrawHdcFontOverride fontOverride(hdc);
			int ret = 0;
			if (!replaced.empty() && replaced != std::wstring(lpchText, length))
			{
				LogTextReplaceApiHitW(L"DrawTextW", std::wstring(lpchText, length), replaced);
				ret = rawDrawTextW(hdc, replaced.c_str(), (int)replaced.length(), const_cast<LPRECT>(effectiveRect), format);
			}
			else
			{
				ret = rawDrawTextW(hdc, lpchText, cchText, const_cast<LPRECT>(effectiveRect), format);
			}
			CommitAdjustedRect(lprc, adjustedRect);
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookDrawTextW()
		{
			return DetourAttachFunc(&rawDrawTextW, newDrawTextW);
		}
		//*********END Hook DrawTextW*********

		//*********Hook DrawTextExA*********
		static pDrawTextExA rawDrawTextExA = DrawTextExA;

		int WINAPI newDrawTextExA(HDC hdc, LPSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchText == -1 ? (int)strlen(lpchText) : cchText;
			std::string replaced = ProcessGlyphStageA(lpchText, length);
			const RECT* effectiveRect = lprc;
			RECT adjustedRect = {};
			if (lprc)
			{
				adjustedRect = BuildOffsetRect(lprc);
				effectiveRect = &adjustedRect;
			}
			int handled = 0;
			int unicodeRet = TryDrawTextExAUnicodeFallback(hdc, lpchText, length, const_cast<LPRECT>(effectiveRect), format, lpdtp, handled);
			if (handled)
			{
				CommitAdjustedRect(lprc, adjustedRect);
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (!replaced.empty() && replaced != std::string(lpchText, length))
			{
				LogTextReplaceApiHitA(L"DrawTextExA", std::string(lpchText, length), replaced);
				char* buffer = new char[replaced.length() + 1];
				strcpy_s(buffer, replaced.length() + 1, replaced.c_str());
				int result = rawDrawTextExA(hdc, buffer, (int)replaced.length(), const_cast<LPRECT>(effectiveRect), format, lpdtp);
				delete[] buffer;
				CommitAdjustedRect(lprc, adjustedRect);
				RestoreSpacingExtra(hdc, previousExtra);
				return result;
			}
			int ret = rawDrawTextExA(hdc, lpchText, cchText, const_cast<LPRECT>(effectiveRect), format, lpdtp);
			CommitAdjustedRect(lprc, adjustedRect);
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookDrawTextExA()
		{
			return DetourAttachFunc(&rawDrawTextExA, newDrawTextExA);
		}
		//*********END Hook DrawTextExA*********

		//*********Hook DrawTextExW*********
		static pDrawTextExW rawDrawTextExW = DrawTextExW;

		static int TryDrawTextExAUnicodeFallback(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp, int& handled)
		{
			handled = 0;
			if ((format & DT_MODIFYSTRING) != 0)
			{
				return 0;
			}

			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpchText, cchText, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return 0;
			}

			std::vector<wchar_t> buffer(glyphStage.mappedWide.begin(), glyphStage.mappedWide.end());
			buffer.push_back(L'\0');
			LogAnsiUnicodeFallback(L"DrawTextExA", glyphStage);
			handled = 1;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawDrawTextExW(hdc, buffer.data(), (int)glyphStage.mappedWide.length(), lprc, format, lpdtp);
		}

		int WINAPI newDrawTextExW(HDC hdc, LPWSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchText == -1 ? (int)wcslen(lpchText) : cchText;
			std::wstring replaced = ProcessGlyphStageW(lpchText, length);
			const RECT* effectiveRect = lprc;
			RECT adjustedRect = {};
			if (lprc)
			{
				adjustedRect = BuildOffsetRect(lprc);
				effectiveRect = &adjustedRect;
			}
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (!replaced.empty() && replaced != std::wstring(lpchText, length))
			{
				LogTextReplaceApiHitW(L"DrawTextExW", std::wstring(lpchText, length), replaced);
				wchar_t* buffer = new wchar_t[replaced.length() + 1];
				wcscpy_s(buffer, replaced.length() + 1, replaced.c_str());
				int result = rawDrawTextExW(hdc, buffer, (int)replaced.length(), const_cast<LPRECT>(effectiveRect), format, lpdtp);
				delete[] buffer;
				CommitAdjustedRect(lprc, adjustedRect);
				RestoreSpacingExtra(hdc, previousExtra);
				return result;
			}
			int ret = rawDrawTextExW(hdc, lpchText, cchText, const_cast<LPRECT>(effectiveRect), format, lpdtp);
			CommitAdjustedRect(lprc, adjustedRect);
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookDrawTextExW()
		{
			return DetourAttachFunc(&rawDrawTextExW, newDrawTextExW);
		}
		//*********END Hook DrawTextExW*********

		static pPolyTextOutA rawPolyTextOutA = PolyTextOutA;

		BOOL WINAPI newPolyTextOutA(HDC hdc, const POLYTEXTA* ppt, int nStrings)
		{
			if (!ppt || nStrings <= 0)
			{
				return rawPolyTextOutA(hdc, ppt, nStrings);
			}

			std::vector<POLYTEXTA> texts(ppt, ppt + nStrings);
			std::vector<std::string> replacedTexts;
			replacedTexts.reserve((size_t)nStrings);

			for (int i = 0; i < nStrings; i++)
			{
				texts[i].x = ApplyGlyphOffsetX(texts[i].x);
				texts[i].y = ApplyGlyphOffsetY(texts[i].y);
				texts[i].rcl = BuildOffsetRect(&texts[i].rcl);
				int length = texts[i].n == -1 ? (texts[i].lpstr ? (int)strlen(texts[i].lpstr) : 0) : texts[i].n;
				std::string replaced = ProcessGlyphStageA(texts[i].lpstr, length);
				if (length > 0 && !replaced.empty() && replaced != std::string(texts[i].lpstr, length))
				{
					LogTextReplaceApiHitA(L"PolyTextOutA", std::string(texts[i].lpstr, length), replaced);
					replacedTexts.push_back(replaced);
					texts[i].lpstr = replacedTexts.back().c_str();
					texts[i].n = (int)replacedTexts.back().length();
				}
			}

			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawPolyTextOutA(hdc, texts.data(), nStrings);
		}

		bool HookPolyTextOutA()
		{
			return DetourAttachFunc(&rawPolyTextOutA, newPolyTextOutA);
		}

		static pPolyTextOutW rawPolyTextOutW = PolyTextOutW;

		BOOL WINAPI newPolyTextOutW(HDC hdc, const POLYTEXTW* ppt, int nStrings)
		{
			if (!ppt || nStrings <= 0)
			{
				return rawPolyTextOutW(hdc, ppt, nStrings);
			}

			std::vector<POLYTEXTW> texts(ppt, ppt + nStrings);
			std::vector<std::wstring> replacedTexts;
			replacedTexts.reserve((size_t)nStrings);

			for (int i = 0; i < nStrings; i++)
			{
				texts[i].x = ApplyGlyphOffsetX(texts[i].x);
				texts[i].y = ApplyGlyphOffsetY(texts[i].y);
				texts[i].rcl = BuildOffsetRect(&texts[i].rcl);
				int length = texts[i].n == -1 ? (texts[i].lpstr ? (int)wcslen(texts[i].lpstr) : 0) : texts[i].n;
				std::wstring replaced = ProcessGlyphStageW(texts[i].lpstr, length);
				if (length > 0 && !replaced.empty() && replaced != std::wstring(texts[i].lpstr, length))
				{
					LogTextReplaceApiHitW(L"PolyTextOutW", std::wstring(texts[i].lpstr, length), replaced);
					replacedTexts.push_back(replaced);
					texts[i].lpstr = replacedTexts.back().c_str();
					texts[i].n = (int)replacedTexts.back().length();
				}
			}

			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawPolyTextOutW(hdc, texts.data(), nStrings);
		}

		bool HookPolyTextOutW()
		{
			return DetourAttachFunc(&rawPolyTextOutW, newPolyTextOutW);
		}

		static pTabbedTextOutA rawTabbedTextOutA = TabbedTextOutA;

		LONG WINAPI newTabbedTextOutA(HDC hdc, int x, int y, LPCSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions, int nTabOrigin)
		{
			int length = chCount == -1 ? (lpString ? (int)strlen(lpString) : 0) : chCount;
			std::string replaced = ProcessGlyphStageA(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::string(lpString, length))
			{
				LogTextReplaceApiHitA(L"TabbedTextOutA", std::string(lpString, length), replaced);
				return rawTabbedTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), replaced.c_str(), (int)replaced.length(), nTabPositions, lpnTabStopPositions, nTabOrigin);
			}
			return rawTabbedTextOutA(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), lpString, chCount, nTabPositions, lpnTabStopPositions, nTabOrigin);
		}

		bool HookTabbedTextOutA()
		{
			return DetourAttachFunc(&rawTabbedTextOutA, newTabbedTextOutA);
		}

		static pTabbedTextOutW rawTabbedTextOutW = TabbedTextOutW;

		LONG WINAPI newTabbedTextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions, int nTabOrigin)
		{
			int length = chCount == -1 ? (lpString ? (int)wcslen(lpString) : 0) : chCount;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"TabbedTextOutW", std::wstring(lpString, length), replaced);
				return rawTabbedTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), replaced.c_str(), (int)replaced.length(), nTabPositions, lpnTabStopPositions, nTabOrigin);
			}
			return rawTabbedTextOutW(hdc, ApplyGlyphOffsetX(x), ApplyGlyphOffsetY(y), lpString, chCount, nTabPositions, lpnTabStopPositions, nTabOrigin);
		}

		bool HookTabbedTextOutW()
		{
			return DetourAttachFunc(&rawTabbedTextOutW, newTabbedTextOutW);
		}

		static pGetTabbedTextExtentA rawGetTabbedTextExtentA = GetTabbedTextExtentA;

		DWORD WINAPI newGetTabbedTextExtentA(HDC hdc, LPCSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = chCount == -1 ? (lpString ? (int)strlen(lpString) : 0) : chCount;
			std::string replaced = ProcessGlyphStageA(lpString, length);
			DWORD ret = 0;
			if (length > 0 && !replaced.empty() && replaced != std::string(lpString, length))
			{
				LogTextReplaceApiHitA(L"GetTabbedTextExtentA", std::string(lpString, length), replaced);
				ret = rawGetTabbedTextExtentA(hdc, replaced.c_str(), (int)replaced.length(), nTabPositions, lpnTabStopPositions);
			}
			else
			{
				ret = rawGetTabbedTextExtentA(hdc, lpString, chCount, nTabPositions, lpnTabStopPositions);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTabbedTextExtentA()
		{
			return DetourAttachFunc(&rawGetTabbedTextExtentA, newGetTabbedTextExtentA);
		}

		static pGetTabbedTextExtentW rawGetTabbedTextExtentW = GetTabbedTextExtentW;

		DWORD WINAPI newGetTabbedTextExtentW(HDC hdc, LPCWSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = chCount == -1 ? (lpString ? (int)wcslen(lpString) : 0) : chCount;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			DWORD ret = 0;
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"GetTabbedTextExtentW", std::wstring(lpString, length), replaced);
				ret = rawGetTabbedTextExtentW(hdc, replaced.c_str(), (int)replaced.length(), nTabPositions, lpnTabStopPositions);
			}
			else
			{
				ret = rawGetTabbedTextExtentW(hdc, lpString, chCount, nTabPositions, lpnTabStopPositions);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTabbedTextExtentW()
		{
			return DetourAttachFunc(&rawGetTabbedTextExtentW, newGetTabbedTextExtentW);
		}

		static pGetTextExtentPoint32A rawGetTextExtentPoint32A = GetTextExtentPoint32A;

		BOOL WINAPI newGetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			LPCSTR safeString = lpString;
			int safeCount = c;
			if (sg_enableWaffleGetTextCrashPatch && safeString && strcmp(safeString, "\t") == 0)
			{
				LogMessage(LogLevel::Info, L"WafflePatch: replace tab with space in GetTextExtentPoint32A");
				safeString = " ";
				if (safeCount > 0)
				{
					safeCount = 1;
				}
			}
			int length = safeCount == -1 ? (safeString ? (int)strlen(safeString) : 0) : safeCount;
			BOOL handled = FALSE;
			BOOL unicodeRet = TryGetTextExtentPoint32AUnicodeFallback(hdc, safeString, length, psizl, handled);
			if (handled)
			{
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			std::string replaced = ProcessGlyphStageA(safeString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::string(safeString, length))
			{
				LogTextReplaceApiHitA(L"GetTextExtentPoint32A", std::string(safeString, length), replaced);
				ret = rawGetTextExtentPoint32A(hdc, replaced.c_str(), (int)replaced.length(), psizl);
			}
			else
			{
				ret = rawGetTextExtentPoint32A(hdc, safeString, safeCount, psizl);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentPoint32A()
		{
			return DetourAttachFunc(&rawGetTextExtentPoint32A, newGetTextExtentPoint32A);
		}

		static pGetTextExtentPoint32W rawGetTextExtentPoint32W = GetTextExtentPoint32W;

		static BOOL TryGetTextExtentPoint32AUnicodeFallback(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl, BOOL& handled)
		{
			handled = FALSE;
			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpString, c, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return FALSE;
			}

			LogAnsiUnicodeFallback(L"GetTextExtentPoint32A", glyphStage);
			handled = TRUE;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawGetTextExtentPoint32W(hdc, glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length(), psizl);
		}

		BOOL WINAPI newGetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = c == -1 ? (lpString ? (int)wcslen(lpString) : 0) : c;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"GetTextExtentPoint32W", std::wstring(lpString, length), replaced);
				ret = rawGetTextExtentPoint32W(hdc, replaced.c_str(), (int)replaced.length(), psizl);
			}
			else
			{
				ret = rawGetTextExtentPoint32W(hdc, lpString, c, psizl);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentPoint32W()
		{
			return DetourAttachFunc(&rawGetTextExtentPoint32W, newGetTextExtentPoint32W);
		}

		static pGetTextExtentExPointA rawGetTextExtentExPointA = GetTextExtentExPointA;

		BOOL WINAPI newGetTextExtentExPointA(HDC hdc, LPCSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchString == -1 ? (lpszStr ? (int)strlen(lpszStr) : 0) : cchString;
			BOOL handled = FALSE;
			BOOL unicodeRet = TryGetTextExtentExPointAUnicodeFallback(hdc, lpszStr, length, nMaxExtent, lpnFit, lpnDx, lpSize, handled);
			if (handled)
			{
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			std::string replaced = ProcessGlyphStageA(lpszStr, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::string(lpszStr, length))
			{
				LogTextReplaceApiHitA(L"GetTextExtentExPointA", std::string(lpszStr, length), replaced);
				ret = rawGetTextExtentExPointA(hdc, replaced.c_str(), (int)replaced.length(), nMaxExtent, lpnFit, lpnDx, lpSize);
			}
			else
			{
				ret = rawGetTextExtentExPointA(hdc, lpszStr, cchString, nMaxExtent, lpnFit, lpnDx, lpSize);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentExPointA()
		{
			return DetourAttachFunc(&rawGetTextExtentExPointA, newGetTextExtentExPointA);
		}

		static pGetTextExtentExPointW rawGetTextExtentExPointW = GetTextExtentExPointW;

		static BOOL TryGetTextExtentExPointAUnicodeFallback(HDC hdc, LPCSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize, BOOL& handled)
		{
			handled = FALSE;
			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpszStr, cchString, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return FALSE;
			}

			std::vector<int> wideDx(glyphStage.mappedWide.size());
			int wideFit = (int)glyphStage.mappedWide.size();
			LogAnsiUnicodeFallback(L"GetTextExtentExPointA", glyphStage);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			const std::wstring& glyphQuery = glyphStage.mappedWide;
			wideDx.assign(glyphQuery.size(), 0);
			wideFit = (int)glyphQuery.size();
			BOOL ret = rawGetTextExtentExPointW(hdc,
				glyphQuery.c_str(),
				(int)glyphQuery.length(),
				nMaxExtent,
				lpnFit ? &wideFit : nullptr,
				lpnDx ? wideDx.data() : nullptr,
				lpSize);
			if (!ret)
			{
				handled = TRUE;
				return ret;
			}

			if (lpnDx)
			{
				int ansiIndex = 0;
				int wideIndex = 0;
				int ansiLength = (int)glyphStage.sourceAnsi.size();
				while (wideIndex < wideFit && ansiIndex < ansiLength)
				{
					int byteCount = 1;
					if (IsDBCSLeadByteEx(sg_cnJpMapReadCodePage, (BYTE)glyphStage.sourceAnsi[ansiIndex]) && ansiIndex + 1 < ansiLength)
					{
						byteCount = 2;
					}

					for (int i = 0; i < byteCount && ansiIndex < ansiLength; ++i)
					{
						lpnDx[ansiIndex++] = wideDx[wideIndex];
					}
					++wideIndex;
				}
			}

			if (lpnFit)
			{
				int ansiFit = 0;
				int ansiLength = (int)glyphStage.sourceAnsi.size();
				for (int wideIndex = 0; wideIndex < wideFit && ansiFit < ansiLength; ++wideIndex)
				{
					int byteCount = 1;
					if (IsDBCSLeadByteEx(sg_cnJpMapReadCodePage, (BYTE)glyphStage.sourceAnsi[ansiFit]) && ansiFit + 1 < ansiLength)
					{
						byteCount = 2;
					}
					ansiFit += byteCount;
				}
				*lpnFit = ansiFit;
			}

			handled = TRUE;
			return ret;
		}

		BOOL WINAPI newGetTextExtentExPointW(HDC hdc, LPCWSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = cchString == -1 ? (lpszStr ? (int)wcslen(lpszStr) : 0) : cchString;
			std::wstring replaced = ProcessGlyphStageW(lpszStr, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpszStr, length))
			{
				LogTextReplaceApiHitW(L"GetTextExtentExPointW", std::wstring(lpszStr, length), replaced);
				ret = rawGetTextExtentExPointW(hdc, replaced.c_str(), (int)replaced.length(), nMaxExtent, lpnFit, lpnDx, lpSize);
			}
			else
			{
				ret = rawGetTextExtentExPointW(hdc, lpszStr, cchString, nMaxExtent, lpnFit, lpnDx, lpSize);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentExPointW()
		{
			return DetourAttachFunc(&rawGetTextExtentExPointW, newGetTextExtentExPointW);
		}

		static pGetTextExtentPointA rawGetTextExtentPointA = GetTextExtentPointA;

		BOOL WINAPI newGetTextExtentPointA(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = c == -1 ? (lpString ? (int)strlen(lpString) : 0) : c;
			BOOL handled = FALSE;
			BOOL unicodeRet = TryGetTextExtentPointAUnicodeFallback(hdc, lpString, length, lpsz, handled);
			if (handled)
			{
				RestoreSpacingExtra(hdc, previousExtra);
				return unicodeRet;
			}
			std::string replaced = ProcessGlyphStageA(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::string(lpString, length))
			{
				LogTextReplaceApiHitA(L"GetTextExtentPointA", std::string(lpString, length), replaced);
				ret = rawGetTextExtentPointA(hdc, replaced.c_str(), (int)replaced.length(), lpsz);
			}
			else
			{
				ret = rawGetTextExtentPointA(hdc, lpString, c, lpsz);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentPointA()
		{
			return DetourAttachFunc(&rawGetTextExtentPointA, newGetTextExtentPointA);
		}

		static pGetTextExtentPointW rawGetTextExtentPointW = GetTextExtentPointW;

		static BOOL TryGetTextExtentPointAUnicodeFallback(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz, BOOL& handled)
		{
			handled = FALSE;
			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(lpString, c, glyphStage) || !glyphStage.needUnicodeFallback)
			{
				return FALSE;
			}

			LogAnsiUnicodeFallback(L"GetTextExtentPointA", glyphStage);
			handled = TRUE;
			ScopedDrawHdcFontOverride fontOverride(hdc);
			return rawGetTextExtentPointW(hdc, glyphStage.mappedWide.c_str(), (int)glyphStage.mappedWide.length(), lpsz);
		}

		BOOL WINAPI newGetTextExtentPointW(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpsz)
		{
			int previousExtra = ApplySpacingExtra(hdc);
			int length = c == -1 ? (lpString ? (int)wcslen(lpString) : 0) : c;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			BOOL ret = FALSE;
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"GetTextExtentPointW", std::wstring(lpString, length), replaced);
				ret = rawGetTextExtentPointW(hdc, replaced.c_str(), (int)replaced.length(), lpsz);
			}
			else
			{
				ret = rawGetTextExtentPointW(hdc, lpString, c, lpsz);
			}
			RestoreSpacingExtra(hdc, previousExtra);
			return ret;
		}

		bool HookGetTextExtentPointW()
		{
			return DetourAttachFunc(&rawGetTextExtentPointW, newGetTextExtentPointW);
		}

		static pGetCharacterPlacementA rawGetCharacterPlacementA = GetCharacterPlacementA;

		DWORD WINAPI newGetCharacterPlacementA(HDC hdc, LPCSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSA lpResults, DWORD dwFlags)
		{
			int length = nCount == -1 ? (lpString ? (int)strlen(lpString) : 0) : nCount;
			std::string replaced = ProcessGlyphStageA(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::string(lpString, length))
			{
				LogTextReplaceApiHitA(L"GetCharacterPlacementA", std::string(lpString, length), replaced);
				return rawGetCharacterPlacementA(hdc, replaced.c_str(), (int)replaced.length(), nMaxExtent, lpResults, dwFlags);
			}
			return rawGetCharacterPlacementA(hdc, lpString, nCount, nMaxExtent, lpResults, dwFlags);
		}

		bool HookGetCharacterPlacementA()
		{
			return DetourAttachFunc(&rawGetCharacterPlacementA, newGetCharacterPlacementA);
		}

		static pGetCharacterPlacementW rawGetCharacterPlacementW = GetCharacterPlacementW;

		DWORD WINAPI newGetCharacterPlacementW(HDC hdc, LPCWSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSW lpResults, DWORD dwFlags)
		{
			int length = nCount == -1 ? (lpString ? (int)wcslen(lpString) : 0) : nCount;
			std::wstring replaced = ProcessGlyphStageW(lpString, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpString, length))
			{
				LogTextReplaceApiHitW(L"GetCharacterPlacementW", std::wstring(lpString, length), replaced);
				return rawGetCharacterPlacementW(hdc, replaced.c_str(), (int)replaced.length(), nMaxExtent, lpResults, dwFlags);
			}
			return rawGetCharacterPlacementW(hdc, lpString, nCount, nMaxExtent, lpResults, dwFlags);
		}

		bool HookGetCharacterPlacementW()
		{
			return DetourAttachFunc(&rawGetCharacterPlacementW, newGetCharacterPlacementW);
		}

		static pGetGlyphIndicesA rawGetGlyphIndicesA = GetGlyphIndicesA;

		DWORD WINAPI newGetGlyphIndicesA(HDC hdc, LPCSTR lpstr, int c, LPWORD pgi, DWORD fl)
		{
			int length = c == -1 ? (lpstr ? (int)strlen(lpstr) : 0) : c;
			std::string replaced = ProcessGlyphStageA(lpstr, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::string(lpstr, length))
			{
				LogTextReplaceApiHitA(L"GetGlyphIndicesA", std::string(lpstr, length), replaced);
				return rawGetGlyphIndicesA(hdc, replaced.c_str(), (int)replaced.length(), pgi, fl);
			}
			return rawGetGlyphIndicesA(hdc, lpstr, c, pgi, fl);
		}

		bool HookGetGlyphIndicesA()
		{
			return DetourAttachFunc(&rawGetGlyphIndicesA, newGetGlyphIndicesA);
		}

		DWORD WINAPI newGetGlyphIndicesW(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl)
		{
			int length = c == -1 ? (lpstr ? (int)wcslen(lpstr) : 0) : c;
			std::wstring replaced = ProcessGlyphStageW(lpstr, length);
			ScopedDrawHdcFontOverride fontOverride(hdc);
			if (length > 0 && !replaced.empty() && replaced != std::wstring(lpstr, length))
			{
				LogTextReplaceApiHitW(L"GetGlyphIndicesW", std::wstring(lpstr, length), replaced);
				return rawGetGlyphIndicesW(hdc, replaced.c_str(), (int)replaced.length(), pgi, fl);
			}
			return rawGetGlyphIndicesW(hdc, lpstr, c, pgi, fl);
		}

		bool HookGetGlyphIndicesW()
		{
			return DetourAttachFunc(&rawGetGlyphIndicesW, newGetGlyphIndicesW);
		}

		//*********Hook GetGlyphOutlineA*********
		static pGetGlyphOutlineA rawGetGlyphOutlineA = GetGlyphOutlineA;
		// 存储字符映射缓存，避免重复转换
		static std::map<UINT, UINT> sg_mapCharReplaceCache;

		DWORD WINAPI newGetGlyphOutlineA(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = 0;
			if ((fuFormat & GGO_GLYPH_INDEX) != 0)
			{
				ret = rawGetGlyphOutlineA(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
				if (ret != GDI_ERROR && lpgm)
				{
					lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
					lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
				}
				RestoreHdcFont(hdc, hOld, hNew);
				return ret;
			}
			bool handled = false;
			ret = TryGetGlyphOutlineAUnicodeFallback(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2, handled);
			if (handled)
			{
				if (ret != GDI_ERROR && lpgm)
				{
					lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
					lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
				}
				RestoreHdcFont(hdc, hOld, hNew);
				return ret;
			}
			if (!sg_vecTextReplaceRulesA.empty() || sg_enableCnJpMap)
			{
				// 将单个字符转换为字符串
				char szChar[4] = { 0 };
				int charLen = 0;
				
				// 判断是否为双字节字符（Shift-JIS、GBK等）
				if (uChar > 0xFF) 
				{
					// 双字节字符
					szChar[0] = (char)((uChar >> 8) & 0xFF);
					szChar[1] = (char)(uChar & 0xFF);
					charLen = 2;
				}
				else
				{
					// 单字节字符
					szChar[0] = (char)(uChar & 0xFF);
					charLen = 1;
				}
				
				// 尝试匹配替换规则
				std::string replaced = ProcessGlyphStageA(szChar, charLen);
				
				// 如果替换成功且结果不为空
				if (!replaced.empty() && replaced != std::string(szChar, charLen))
				{
					LogTextReplaceApiHitA(L"GetGlyphOutlineA", std::string(szChar, charLen), replaced);
					// 将替换后的字符串转换回字符码
					UINT newChar = 0;
					if (replaced.length() >= 2)
					{
						// 双字节
						newChar = ((unsigned char)replaced[0] << 8) | (unsigned char)replaced[1];
					}
					else if (replaced.length() == 1)
					{
						// 单字节
						newChar = (unsigned char)replaced[0];
					}
					else
					{
						// 替换结果无效，使用原字符
						ret = rawGetGlyphOutlineA(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
						if (ret != GDI_ERROR && lpgm)
						{
							lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
							lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
						}
						RestoreHdcFont(hdc, hOld, hNew);
						return ret;
					}
					
					ret = rawGetGlyphOutlineA(hdc, newChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
					if (ret != GDI_ERROR && lpgm)
					{
						lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
						lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
					}
					RestoreHdcFont(hdc, hOld, hNew);
					return ret;
				}
			}
			
			ret = rawGetGlyphOutlineA(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
			if (ret != GDI_ERROR && lpgm)
			{
				lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
				lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		bool HookGetGlyphOutlineA()
		{
			return DetourAttachFunc(&rawGetGlyphOutlineA, newGetGlyphOutlineA);
		}
		//*********END Hook GetGlyphOutlineA*********

		//*********Hook GetGlyphOutlineW*********
		static pGetGlyphOutlineW rawGetGlyphOutlineW = GetGlyphOutlineW;

		static DWORD TryGetGlyphOutlineAUnicodeFallback(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2, bool& handled)
		{
			handled = false;
			if ((fuFormat & GGO_GLYPH_INDEX) != 0)
			{
				return 0;
			}

			char szChar[2] = { 0 };
			int charLen = 1;
			if (uChar > 0xFF)
			{
				szChar[0] = (char)((uChar >> 8) & 0xFF);
				szChar[1] = (char)(uChar & 0xFF);
				charLen = 2;
			}
			else
			{
				szChar[0] = (char)(uChar & 0xFF);
			}

			AnsiGlyphStageResult glyphStage;
			if (!BuildAnsiGlyphStageResult(szChar, charLen, glyphStage) || !glyphStage.needUnicodeFallback || glyphStage.mappedWide.empty())
			{
				return 0;
			}

			LogAnsiUnicodeFallback(L"GetGlyphOutlineA", glyphStage);
			handled = true;
			return rawGetGlyphOutlineW(hdc, (UINT)glyphStage.mappedWide[0], fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
		}

		DWORD WINAPI newGetGlyphOutlineW(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = 0;
			if ((fuFormat & GGO_GLYPH_INDEX) != 0)
			{
				ret = rawGetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
				if (ret != GDI_ERROR && lpgm)
				{
					lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
					lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
				}
				RestoreHdcFont(hdc, hOld, hNew);
				return ret;
			}
			if (!sg_vecTextReplaceRulesW.empty() || sg_enableCnJpMap)
			{
				// 将单个字符转换为字符串
				wchar_t szChar[2] = { 0 };
				szChar[0] = (wchar_t)uChar;
				
				// 尝试匹配替换规则
				std::wstring replaced = ProcessGlyphStageW(szChar, 1);
				
				// 如果替换成功且结果不为空
				if (!replaced.empty() && replaced != std::wstring(szChar, 1))
				{
					LogTextReplaceApiHitW(L"GetGlyphOutlineW", std::wstring(szChar, 1), replaced);
					// 将替换后的字符串转换回字符码
					if (replaced.length() >= 1)
					{
						UINT newChar = (UINT)replaced[0];
						ret = rawGetGlyphOutlineW(hdc, newChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
						if (ret != GDI_ERROR && lpgm)
						{
							lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
							lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
						}
						RestoreHdcFont(hdc, hOld, hNew);
						return ret;
					}
				}
			}
			
			ret = rawGetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
			if (ret != GDI_ERROR && lpgm)
			{
				lpgm->gmptGlyphOrigin.x += sg_iGlyphOffsetX;
				lpgm->gmptGlyphOrigin.y += sg_iGlyphOffsetY;
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		bool HookGetGlyphOutlineW()
		{
			return DetourAttachFunc(&rawGetGlyphOutlineW, newGetGlyphOutlineW);
		}
		//*********END Hook GetGlyphOutlineW*********
		//*********END Text Replace*********


