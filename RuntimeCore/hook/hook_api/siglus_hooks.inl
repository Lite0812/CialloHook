		//*********START Siglus XOR Key Extract*********
		class HardwareBreakpoint
		{
		public:
			HardwareBreakpoint() : m_index(-1) {}
			~HardwareBreakpoint() { Clear(); }

			enum Condition { Write = 1, ReadWrite = 3 };

			void Set(void* address, int len, Condition when)
			{
				if (m_index != -1)
				{
					return;
				}

				CONTEXT context = {};
				HANDLE thisThread = GetCurrentThread();

				switch (len)
				{
				case 1:
					len = 0;
					break;
				case 2:
					len = 1;
					break;
				case 4:
					len = 3;
					break;
				default:
					return;
				}

				context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if (!GetThreadContext(thisThread, &context))
				{
					return;
				}

				for (m_index = 0; m_index < 4; ++m_index)
				{
					if ((context.Dr7 & (1ULL << (m_index * 2))) == 0)
					{
						break;
					}
				}
				if (m_index >= 4)
				{
					m_index = -1;
					return;
				}

				switch (m_index)
				{
				case 0:
					context.Dr0 = (DWORD_PTR)address;
					break;
				case 1:
					context.Dr1 = (DWORD_PTR)address;
					break;
				case 2:
					context.Dr2 = (DWORD_PTR)address;
					break;
				case 3:
					context.Dr3 = (DWORD_PTR)address;
					break;
				}

				SetBits(context.Dr7, 16 + (m_index * 4), 2, when);
				SetBits(context.Dr7, 18 + (m_index * 4), 2, len);
				SetBits(context.Dr7, m_index * 2, 1, 1);

				SetThreadContext(thisThread, &context);
			}

			void Clear(PCONTEXT context)
			{
				if (m_index == -1 || !context)
				{
					return;
				}
				SetBits(context->Dr7, m_index * 2, 1, 0);
				context->Dr0 = 0;
				context->Dr1 = 0;
				context->Dr2 = 0;
				context->Dr3 = 0;
				m_index = -1;
			}

			void Clear()
			{
				if (m_index == -1)
				{
					return;
				}

				CONTEXT context = {};
				HANDLE thisThread = GetCurrentThread();
				context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if (GetThreadContext(thisThread, &context))
				{
					SetBits(context.Dr7, m_index * 2, 1, 0);
					SetThreadContext(thisThread, &context);
				}
				m_index = -1;
			}

		private:
			inline void SetBits(DWORD_PTR& value, int lowBit, int bits, int newValue)
			{
				DWORD_PTR mask = (1ULL << bits) - 1;
				value = (value & ~(mask << lowBit)) | ((DWORD_PTR)newValue << lowBit);
			}

			int m_index;
		};

		static uint8_t sg_siglusXorKey[16] = {};
		static bool sg_siglusKeyExtracted = false;
		static bool sg_siglusKeyExtractEnabled = false;
		static bool sg_siglusShowMsgBox = false;
		static std::wstring sg_siglusGameexePath = L"Gameexe.dat";
		static std::wstring sg_siglusKeyOutputPath = L"siglus_key.txt";
		static std::wstring sg_siglusGameexeFullPathLower;
		static std::wstring sg_siglusGameexeFileNameLower = L"gameexe.dat";
		static BYTE sg_siglusGameexeBytes[24] = {};
		static HANDLE sg_siglusGameexeHandle = INVALID_HANDLE_VALUE;
		static PBYTE sg_siglusAccessPtr = nullptr;
		static volatile bool sg_siglusHookOnce = false;
		static volatile bool sg_siglusInitOnce = false;
		static volatile bool sg_siglusInitKey = false;
		static HardwareBreakpoint sg_siglusHwBp;
		static PVOID sg_siglusExceptionHandler = nullptr;
		static pCreateFileW_File rawCreateFileW_KeyExtract = nullptr;
		static pReadFile_File rawReadFile_KeyExtract = nullptr;

		static std::wstring NormalizeSiglusSlashes(std::wstring value)
		{
			for (wchar_t& c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			return value;
		}

		static std::wstring ToLowerSiglus(std::wstring value)
		{
			std::transform(value.begin(), value.end(), value.begin(), towlower);
			return value;
		}

		static std::wstring GetSiglusAbsoluteLowerPath(const wchar_t* path)
		{
			if (!path || path[0] == L'\0')
			{
				return L"";
			}

			DWORD len = GetFullPathNameW(path, 0, nullptr, nullptr);
			if (len == 0)
			{
				return ToLowerSiglus(NormalizeSiglusSlashes(path));
			}

			std::vector<wchar_t> buffer(len);
			DWORD actualLen = GetFullPathNameW(path, len, buffer.data(), nullptr);
			if (actualLen == 0)
			{
				return ToLowerSiglus(NormalizeSiglusSlashes(path));
			}

			return ToLowerSiglus(NormalizeSiglusSlashes(std::wstring(buffer.data(), actualLen)));
		}

		static std::wstring GetSiglusFileNameLower(const std::wstring& path)
		{
			std::wstring normalized = NormalizeSiglusSlashes(path);
			size_t pos = normalized.find_last_of(L"\\/");
			std::wstring fileName = pos == std::wstring::npos ? normalized : normalized.substr(pos + 1);
			return ToLowerSiglus(fileName);
		}

		void SetKeyExtractConfig(const wchar_t* gameexePath, const wchar_t* outputPath, bool showMsgBox)
		{
			if (gameexePath && gameexePath[0] != L'\0')
			{
				sg_siglusGameexePath = gameexePath;
			}
			if (outputPath && outputPath[0] != L'\0')
			{
				sg_siglusKeyOutputPath = outputPath;
			}
			sg_siglusShowMsgBox = showMsgBox;
			sg_siglusGameexeFullPathLower = GetSiglusAbsoluteLowerPath(sg_siglusGameexePath.c_str());
			sg_siglusGameexeFileNameLower = GetSiglusFileNameLower(sg_siglusGameexePath);
			LogMessage(LogLevel::Info, L"SetKeyExtractConfig: gameexe=%s output=%s msgbox=%d",
				sg_siglusGameexePath.c_str(),
				sg_siglusKeyOutputPath.c_str(),
				sg_siglusShowMsgBox ? 1 : 0);
		}

		bool IsSiglusKeyExtracted()
		{
			return sg_siglusKeyExtracted;
		}

		static std::string GetSiglusKeyHexString()
		{
			char buffer[33] = {};
			for (int i = 0; i < 16; ++i)
			{
				sprintf_s(buffer + i * 2, sizeof(buffer) - i * 2, "%02X", sg_siglusXorKey[i]);
			}
			return buffer;
		}

		static void SaveKeyToFile()
		{
			if (sg_siglusKeyOutputPath.empty())
			{
				return;
			}

			FILE* file = nullptr;
			if (_wfopen_s(&file, sg_siglusKeyOutputPath.c_str(), L"w") != 0 || !file)
			{
				LogMessage(LogLevel::Warn, L"SiglusKeyExtract: failed to open output file %s", sg_siglusKeyOutputPath.c_str());
				return;
			}

			std::string hexKey = GetSiglusKeyHexString();
			fprintf(file, "# Siglus XOR Key (16 bytes hex)\n");
			fprintf(file, "%s\n\n", hexKey.c_str());
			fprintf(file, "# Key bytes:\n");
			for (int i = 0; i < 16; ++i)
			{
				fprintf(file, "0x%02X", sg_siglusXorKey[i]);
				if (i < 15)
				{
					fprintf(file, ", ");
				}
			}
			fprintf(file, "\n");
			fclose(file);
			LogMessage(LogLevel::Info, L"SiglusKeyExtract: saved key to %s", sg_siglusKeyOutputPath.c_str());
		}

		static LONG NTAPI KeyExtractExceptionHandler(PEXCEPTION_POINTERS exceptionInfo)
		{
			if (!exceptionInfo || sg_siglusInitOnce)
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}
			if (exceptionInfo->ExceptionRecord->ExceptionCode != STATUS_SINGLE_STEP)
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			sg_siglusHwBp.Clear(exceptionInfo->ContextRecord);
			if (!sg_siglusAccessPtr)
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			PBYTE decrypted = sg_siglusAccessPtr - 16;
			PBYTE encrypted = &sg_siglusGameexeBytes[8];
			for (int i = 0; i < 16; ++i)
			{
				sg_siglusXorKey[i] = decrypted[i] ^ encrypted[i];
			}

			sg_siglusKeyExtracted = true;
			sg_siglusInitOnce = true;
			SaveKeyToFile();

			if (sg_siglusExceptionHandler)
			{
				RemoveVectoredExceptionHandler(sg_siglusExceptionHandler);
				sg_siglusExceptionHandler = nullptr;
			}

			if (sg_siglusShowMsgBox)
			{
				std::string hexKey = GetSiglusKeyHexString();
				char message[256] = {};
				sprintf_s(message, "Siglus XOR Key Extracted:\n%s", hexKey.c_str());
				MessageBoxA(nullptr, message, "CialloHook - Key Extract", MB_OK | MB_ICONINFORMATION);
			}

			LogMessage(LogLevel::Info, L"SiglusKeyExtract: key extracted successfully");
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		static bool IsGameexeFile(LPCWSTR fileName)
		{
			if (!fileName || fileName[0] == L'\0')
			{
				return false;
			}

			std::wstring fullPathLower = GetSiglusAbsoluteLowerPath(fileName);
			if (!sg_siglusGameexeFullPathLower.empty() && !fullPathLower.empty() && fullPathLower == sg_siglusGameexeFullPathLower)
			{
				return true;
			}

			return GetSiglusFileNameLower(fileName) == sg_siglusGameexeFileNameLower;
		}

		static bool IsFromMainExe(void* returnAddress)
		{
			if (!returnAddress)
			{
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi = {};
			SIZE_T queried = VirtualQuery(returnAddress, &mbi, sizeof(mbi));
			return queried == sizeof(mbi) && mbi.AllocationBase == GetModuleHandleW(nullptr);
		}

		static HANDLE WINAPI newCreateFileW_KeyExtract(
			LPCWSTR lpFileName,
			DWORD dwDesiredAccess,
			DWORD dwShareMode,
			LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			DWORD dwCreationDisposition,
			DWORD dwFlagsAndAttributes,
			HANDLE hTemplateFile)
		{
			HANDLE hFile = rawCreateFileW_KeyExtract
				? rawCreateFileW_KeyExtract(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
				: INVALID_HANDLE_VALUE;

			if (!sg_siglusInitKey
				&& hFile != INVALID_HANDLE_VALUE
				&& IsGameexeFile(lpFileName)
				&& IsFromMainExe(_ReturnAddress()))
			{
				sg_siglusGameexeHandle = hFile;
				sg_siglusInitKey = true;
				LogMessage(LogLevel::Info, L"SiglusKeyExtract: captured Gameexe handle %p (%s)", hFile, lpFileName ? lpFileName : L"");
			}

			return hFile;
		}

		static BOOL WINAPI newReadFile_KeyExtract(
			HANDLE hFile,
			LPVOID lpBuffer,
			DWORD nNumberOfBytesToRead,
			LPDWORD lpNumberOfBytesRead,
			LPOVERLAPPED lpOverlapped)
		{
			DWORD offset = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);
			BOOL result = rawReadFile_KeyExtract
				? rawReadFile_KeyExtract(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped)
				: FALSE;

			if (result
				&& hFile == sg_siglusGameexeHandle
				&& offset == 0
				&& !sg_siglusHookOnce
				&& lpBuffer
				&& (!lpNumberOfBytesRead || *lpNumberOfBytesRead >= 24))
			{
				sg_siglusAccessPtr = (PBYTE)lpBuffer + 24;
				sg_siglusHwBp.Set((PBYTE)lpBuffer + 24, 1, HardwareBreakpoint::Write);
				sg_siglusExceptionHandler = AddVectoredExceptionHandler(1, KeyExtractExceptionHandler);
				sg_siglusGameexeHandle = INVALID_HANDLE_VALUE;
				sg_siglusHookOnce = true;
				LogMessage(LogLevel::Info, L"SiglusKeyExtract: hardware breakpoint armed");
			}

			return result;
		}

		static bool DetectNeedKeyExtract()
		{
			HANDLE hFile = CreateFileW(sg_siglusGameexePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				LogMessage(LogLevel::Warn, L"SiglusKeyExtract: failed to open %s for probe", sg_siglusGameexePath.c_str());
				return false;
			}

			DWORD bytesRead = 0;
			BOOL readOk = ReadFile(hFile, sg_siglusGameexeBytes, sizeof(sg_siglusGameexeBytes), &bytesRead, nullptr);
			CloseHandle(hFile);

			if (!readOk || bytesRead != sizeof(sg_siglusGameexeBytes))
			{
				LogMessage(LogLevel::Warn, L"SiglusKeyExtract: probe read failed for %s", sg_siglusGameexePath.c_str());
				return false;
			}

			return (*(PDWORD)&sg_siglusGameexeBytes[4] != 0);
		}

		bool EnableSiglusKeyExtract()
		{
			if (sg_siglusKeyExtractEnabled)
			{
				return true;
			}

			if (!DetectNeedKeyExtract())
			{
				LogMessage(LogLevel::Info, L"SiglusKeyExtract: second-layer key not required or probe failed");
				return false;
			}

			rawCreateFileW_KeyExtract = CreateFileW;
			rawReadFile_KeyExtract = ReadFile;

			bool hasFailed = false;
			hasFailed |= DetourAttachFunc(&rawCreateFileW_KeyExtract, newCreateFileW_KeyExtract);
			hasFailed |= DetourAttachFunc(&rawReadFile_KeyExtract, newReadFile_KeyExtract);

			sg_siglusKeyExtractEnabled = !hasFailed;
			LogMessage(sg_siglusKeyExtractEnabled ? LogLevel::Info : LogLevel::Warn,
				L"SiglusKeyExtract: enable result=%d",
				sg_siglusKeyExtractEnabled ? 1 : 0);
			return sg_siglusKeyExtractEnabled;
		}
		//*********END Siglus XOR Key Extract*********