#include <windows.h>

#include <string>
#include <vector>
#include <cstring>

/* ============================================================================
 * 控制台 UTF-8 / VT 初始化
 * ============================================================================ */

static void init_console_runtime() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE handles[] = { GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) };
    for (HANDLE handle : handles) {
        if (handle == INVALID_HANDLE_VALUE || handle == NULL)
            continue;

        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode))
            SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

/* ============================================================================
 * CLI 入口声明
 * ============================================================================ */

int litepak_cli_main(int argc, char** argv);

/* ============================================================================
 * UTF-16 -> UTF-8 转换
 * ============================================================================ */

static std::string wide_to_utf8(const wchar_t* text) {
    if (!text)
        return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return std::string();

    std::vector<char> buffer(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.data(), size, nullptr, nullptr) <= 0)
        return std::string();
    return std::string(buffer.data());
}

/* ============================================================================
 * 程序主入口
 * ============================================================================ */

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> utf8_args;
    std::vector<char*> argv8;

    init_console_runtime();

    utf8_args.reserve(static_cast<size_t>(argc));
    argv8.reserve(static_cast<size_t>(argc));

    for (int i = 0; i < argc; ++i)
        utf8_args.push_back(wide_to_utf8(argv[i]));

    for (int i = 0; i < argc; ++i)
        argv8.push_back(const_cast<char*>(utf8_args[i].c_str()));

    return litepak_cli_main(argc, argv8.data());
}
