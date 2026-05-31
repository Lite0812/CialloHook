#include <windows.h>
#include <conio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using pfn_litepak_pack_ex = int (__cdecl*)(const char*, const char*, const char*, bool, bool,
                                           const char*, int, int, const char*, int);
using pfn_litepak_unpack_ex2 = int (__cdecl*)(const char*, const char*, const char*, bool, bool, int, const char*);
using pfn_litepak_info_ex = int (__cdecl*)(const char*, const char*);
using pfn_litepak_verify_ex = int (__cdecl*)(const char*, const char*);
using pfn_litepak_extract_by_name = int (__cdecl*)(const char*, const char*, const char*);

struct PackDll {
    HMODULE module = nullptr;
    pfn_litepak_pack_ex pack_ex = nullptr;
};

struct CoreDll {
    HMODULE module = nullptr;
    pfn_litepak_unpack_ex2 unpack_ex2 = nullptr;
    pfn_litepak_info_ex info_ex = nullptr;
    pfn_litepak_verify_ex verify_ex = nullptr;
    pfn_litepak_extract_by_name extract_by_name = nullptr;
};

static void wait_for_any_key(void) {
    DWORD mode = 0;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        printf("\n按任意键退出...");
        fflush(stdout);
        _getch();
    }
}

static fs::path exe_dir(void) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return fs::current_path();
    return fs::path(path).parent_path();
}

static HMODULE load_dll_from_exe_dir(const wchar_t* dll_name, const char* purpose) {
    fs::path dll_path = exe_dir() / dll_name;
    HMODULE module = LoadLibraryW(dll_path.c_str());
    if (!module) {
        printf("%s requires %ls next to LitePAK_tool.exe\n", purpose, dll_name);
        return nullptr;
    }
    return module;
}

static FARPROC resolve_proc(HMODULE module, const char* name, const wchar_t* dll_name) {
    FARPROC proc = GetProcAddress(module, name);
    if (!proc)
        printf("%ls 缺少导出函数: %s\n", dll_name, name);
    return proc;
}

static bool load_pack_dll(PackDll* out) {
    out->module = load_dll_from_exe_dir(L"litepak_pk.dll", "pack");
    if (!out->module)
        return false;
    out->pack_ex = reinterpret_cast<pfn_litepak_pack_ex>(resolve_proc(out->module, "litepak_pack_ex", L"litepak_pk.dll"));
    return out->pack_ex != nullptr;
}

static bool load_core_dll(CoreDll* out) {
    out->module = load_dll_from_exe_dir(L"litepak.dll", "read/unpack/info/verify");
    if (!out->module)
        return false;
    out->unpack_ex2 = reinterpret_cast<pfn_litepak_unpack_ex2>(resolve_proc(out->module, "litepak_unpack_ex2", L"litepak.dll"));
    out->info_ex = reinterpret_cast<pfn_litepak_info_ex>(resolve_proc(out->module, "litepak_info_ex", L"litepak.dll"));
    out->verify_ex = reinterpret_cast<pfn_litepak_verify_ex>(resolve_proc(out->module, "litepak_verify_ex", L"litepak.dll"));
    out->extract_by_name = reinterpret_cast<pfn_litepak_extract_by_name>(resolve_proc(out->module, "litepak_extract_by_name", L"litepak.dll"));
    return out->unpack_ex2 && out->info_ex && out->verify_ex && out->extract_by_name;
}

static const char* find_option_value(int argc, char** argv, const char* name) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    }
    return nullptr;
}

static bool has_option(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

static const char* find_verify_key_option(int argc, char** argv, bool* ok) {
    const char* verify_key = find_option_value(argc, argv, "--verify-key");
    const char* legacy_sign_key = find_option_value(argc, argv, "--sign-key");
    if (ok)
        *ok = true;
    if (verify_key && legacy_sign_key) {
        printf("read-side key 参数冲突: --verify-key 与 --sign-key 不能同时使用\n");
        if (ok)
            *ok = false;
        return nullptr;
    }
    if (legacy_sign_key) {
        printf("警告: read-side --sign-key 已废弃；请改用 --verify-key <32字节public key>\n");
        return legacy_sign_key;
    }
    return verify_key;
}

static int parse_int_option(int argc, char** argv, const char* name, int default_value) {
    const char* value = find_option_value(argc, argv, name);
    if (!value || !*value)
        return default_value;
    return atoi(value);
}

static void print_usage(void) {
    printf("LitePAK v6 工具\n\n");
    printf("用法:\n");
    printf("  LitePAK_tool.exe pack   --input <目录> --pak <文件> --manifest <文件> [选项]\n");
    printf("  LitePAK_tool.exe unpack --pak <文件> --output <目录> [--manifest <文件>] [选项]\n");
    printf("  LitePAK_tool.exe info   --pak <文件>\n");
    printf("  LitePAK_tool.exe read   --pak <文件> --name <相对路径> --output <文件>\n");
    printf("  LitePAK_tool.exe verify --pak <文件>\n\n");
    printf("DLL:\n");
    printf("  pack 需要 litepak_pk.dll\n");
    printf("  unpack/read/info/verify 需要 litepak.dll\n\n");
    printf("常用选项:\n");
    printf("  --workers <N>        pack 使用多线程预读；unpack 共享线程安全缓存\n");
    printf("  --compression <模式> raw|zlib|zstd|lzma|auto\n");
    printf("  --sign-key <文件>    pack 签名私有seed文件 (32字节)\n");
    printf("  --verify-key <文件>  unpack/info/verify 验签public key文件 (32字节)\n\n");
    printf("拖拽模式:\n");
    printf("  1个文件夹           -> 封包为 .lpk + 生成清单 .txt\n");
    printf("  1个 .lpk 文件       -> 按 hash 解包\n");
    printf("  1个 .lpk + 1个 .txt -> 按清单恢复原始路径解包\n");
}

extern "C" void litepak_auto_drag_mode(int argc, char** argv) {
    if (argc == 1) {
        fs::path input = fs::u8path(argv[0]);
        if (fs::is_directory(input)) {
            PackDll dll;
            std::wstring stem = input.filename().wstring();
            fs::path pak_path = input.parent_path() / (stem + L".lpk");
            fs::path manifest_path = input.parent_path() / (stem + L"_manifest.txt");
            if (load_pack_dll(&dll) && dll.pack_ex(input.u8string().c_str(), pak_path.u8string().c_str(), manifest_path.u8string().c_str(),
                                                   true, true, "auto", 128, 1024, nullptr, 1) == 0) {
                printf("\n已封包: %s\n", pak_path.u8string().c_str());
                printf("已生成清单: %s\n", manifest_path.u8string().c_str());
            }
            wait_for_any_key();
            return;
        }
        if (input.has_extension() && _wcsicmp(input.extension().c_str(), L".lpk") == 0 && fs::is_regular_file(input)) {
            CoreDll dll;
            std::wstring stem = input.stem().wstring();
            fs::path output_dir = input.parent_path() / (stem + L"_unpacked");
            if (load_core_dll(&dll) && dll.unpack_ex2(input.u8string().c_str(), output_dir.u8string().c_str(), nullptr, true, true, 1, nullptr) == 0)
                printf("\n已按 hash 解包到: %s\n", output_dir.u8string().c_str());
            wait_for_any_key();
            return;
        }
    }

    if (argc == 2) {
        fs::path p1 = fs::u8path(argv[0]);
        fs::path p2 = fs::u8path(argv[1]);
        fs::path pak_path;
        fs::path manifest_path;

        if (p1.has_extension() && _wcsicmp(p1.extension().c_str(), L".lpk") == 0)
            pak_path = p1;
        if (p2.has_extension() && _wcsicmp(p2.extension().c_str(), L".lpk") == 0)
            pak_path = p2;
        if (p1.has_extension() && _wcsicmp(p1.extension().c_str(), L".txt") == 0)
            manifest_path = p1;
        if (p2.has_extension() && _wcsicmp(p2.extension().c_str(), L".txt") == 0)
            manifest_path = p2;

        if (!pak_path.empty() && !manifest_path.empty()) {
            CoreDll dll;
            std::wstring stem = pak_path.stem().wstring();
            fs::path output_dir = pak_path.parent_path() / (stem + L"_unpacked");
            if (load_core_dll(&dll) && dll.unpack_ex2(pak_path.u8string().c_str(), output_dir.u8string().c_str(),
                                                       manifest_path.u8string().c_str(), true, true, 1, nullptr) == 0) {
                printf("\n已按清单解包到: %s\n", output_dir.u8string().c_str());
            }
            wait_for_any_key();
            return;
        }
    }

    print_usage();
    wait_for_any_key();
}

static int run_pack_command(int argc, char** argv) {
    const char* input = find_option_value(argc, argv, "--input");
    const char* pak = find_option_value(argc, argv, "--pak");
    const char* manifest = find_option_value(argc, argv, "--manifest");
    const char* compression = find_option_value(argc, argv, "--compression");
    const char* sign_key = find_option_value(argc, argv, "--sign-key");
    bool dedup_mode = !has_option(argc, argv, "--no-dedup");
    int cdc_avg_kb = parse_int_option(argc, argv, "--cdc-avg-kb", 128);
    int whole_file_threshold_kb = parse_int_option(argc, argv, "--whole-file-threshold-kb", 1024);
    int workers = parse_int_option(argc, argv, "--workers", 1);
    PackDll dll;

    if (!input || !pak || !manifest) {
        print_usage();
        return 1;
    }
    if (!load_pack_dll(&dll))
        return 1;
    return dll.pack_ex(input, pak, manifest, dedup_mode, true,
                       compression ? compression : "auto",
                       cdc_avg_kb, whole_file_threshold_kb, sign_key, workers) == 0 ? 0 : 1;
}

static int run_unpack_command(int argc, char** argv) {
    const char* pak = find_option_value(argc, argv, "--pak");
    const char* output = find_option_value(argc, argv, "--output");
    const char* manifest = find_option_value(argc, argv, "--manifest");
    bool key_ok = true;
    const char* verify_key = find_verify_key_option(argc, argv, &key_ok);
    bool verify_trailer = !has_option(argc, argv, "--no-trailer-check");
    int workers = parse_int_option(argc, argv, "--workers", 1);
    CoreDll dll;

    if (!key_ok)
        return 1;
    if (!pak || !output) {
        print_usage();
        return 1;
    }
    if (!load_core_dll(&dll))
        return 1;
    return dll.unpack_ex2(pak, output, manifest, true, verify_trailer, workers, verify_key) == 0 ? 0 : 1;
}

static int run_info_command(int argc, char** argv) {
    const char* pak = find_option_value(argc, argv, "--pak");
    bool key_ok = true;
    const char* verify_key = find_verify_key_option(argc, argv, &key_ok);
    CoreDll dll;
    if (!key_ok)
        return 1;
    if (!pak) {
        print_usage();
        return 1;
    }
    if (!load_core_dll(&dll))
        return 1;
    return dll.info_ex(pak, verify_key) == 0 ? 0 : 1;
}

static int run_read_command(int argc, char** argv) {
    const char* pak = find_option_value(argc, argv, "--pak");
    const char* name = find_option_value(argc, argv, "--name");
    const char* output = find_option_value(argc, argv, "--output");
    CoreDll dll;
    if (!pak || !name || !output) {
        print_usage();
        return 1;
    }
    if (!load_core_dll(&dll))
        return 1;
    return dll.extract_by_name(pak, name, output) == 0 ? 0 : 1;
}

static int run_verify_command(int argc, char** argv) {
    const char* pak = find_option_value(argc, argv, "--pak");
    bool key_ok = true;
    const char* verify_key = find_verify_key_option(argc, argv, &key_ok);
    CoreDll dll;
    if (!key_ok)
        return 1;
    if (!pak) {
        print_usage();
        return 1;
    }
    if (!load_core_dll(&dll))
        return 1;
    return dll.verify_ex(pak, verify_key) == 0 ? 0 : 1;
}

int litepak_cli_main(int argc, char** argv) {
    if (argc <= 1) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "pack") != 0 && strcmp(argv[1], "unpack") != 0 &&
        strcmp(argv[1], "info") != 0 && strcmp(argv[1], "read") != 0 &&
        strcmp(argv[1], "verify") != 0) {
        litepak_auto_drag_mode(argc - 1, argv + 1);
        return 0;
    }

    if (strcmp(argv[1], "pack") == 0)
        return run_pack_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "unpack") == 0)
        return run_unpack_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "info") == 0)
        return run_info_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "read") == 0)
        return run_read_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "verify") == 0)
        return run_verify_command(argc - 1, argv + 1);

    print_usage();
    return 1;
}
