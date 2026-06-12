/*
 * splash_webm_integration.h — CialloHook 闪屏 WebM 集成代码
 *
 * 将此文件的内容合并到 hook_manager.cpp 中即可启用 WebM 闪屏。
 * 在 INI 中设置 ImageFile = splash.webm 即可使用。
 *
 * 集成步骤：
 * 1. 在 hook_manager.cpp 顶部添加 #include "../../CialloWebM/include/ciallo_webm.h"
 * 2. 在 RunSplashAnimation() 之前插入下面的 RunWebMSplashAnimation() 函数
 * 3. 在 ShowSplashFromEntryPoint() 中，判断文件扩展名后选择调用哪个函数
 *
 * 或者更简单：CialloHook.vcxproj 链接 ciallo_webm.lib，
 * 然后动态 LoadLibrary("ciallo_webm.dll") 也可以。
 */

#ifndef CIALLO_SPLASH_WEBM_INTEGRATION_H
#define CIALLO_SPLASH_WEBM_INTEGRATION_H

/*
 * 示例：动态加载方式（不需要链接时依赖 ciallo_webm.lib）
 */

/* === 添加到 hook_manager.cpp 的 SplashImage 区域 === */

typedef void* CIALLO_WEBM_HANDLE_T;

typedef struct CialloWebMInfo_T
{
    uint32_t width;
    uint32_t height;
    int      codec;
    int      has_alpha;
    double   duration_sec;
    double   fps;
    uint32_t frame_count_hint;
} CialloWebMInfo_T;

typedef struct CialloWebMFrame_T
{
    const uint8_t* pixels;
    uint32_t       stride;
    uint32_t       width;
    uint32_t       height;
    uint32_t       duration_ms;
    double         timestamp;
    int            is_key;
} CialloWebMFrame_T;

typedef CIALLO_WEBM_HANDLE_T (*PFN_Open)(const uint8_t*, size_t);
typedef int  (*PFN_GetInfo)(CIALLO_WEBM_HANDLE_T, CialloWebMInfo_T*);
typedef int  (*PFN_ReadFrame)(CIALLO_WEBM_HANDLE_T, CialloWebMFrame_T*);
typedef int  (*PFN_Rewind)(CIALLO_WEBM_HANDLE_T);
typedef void (*PFN_Close)(CIALLO_WEBM_HANDLE_T);

/*
 * RunWebMSplashAnimation — 使用 ciallo_webm.dll 播放 WebM 闪屏
 *
 * 与现有 RunSplashAnimation 使用相同的窗口风格和交互逻辑，
 * 只是将 GDI+ Bitmap 替换为 WebM 逐帧解码。
 */
static void RunWebMSplashAnimation(
    const uint8_t* webmData, size_t webmSize,
    const SplashImageSettings& settings)
{
    /* 动态加载 ciallo_webm.dll */
    HMODULE hWebM = LoadLibraryW(L"ciallo_webm.dll");
    if (!hWebM)
    {
        /* DLL 不存在，静默跳过 */
        return;
    }

    PFN_Open    pfnOpen     = (PFN_Open)GetProcAddress(hWebM, "CialloWebM_OpenMemory");
    PFN_GetInfo pfnGetInfo  = (PFN_GetInfo)GetProcAddress(hWebM, "CialloWebM_GetInfo");
    PFN_ReadFrame pfnRead   = (PFN_ReadFrame)GetProcAddress(hWebM, "CialloWebM_ReadFrame");
    PFN_Rewind  pfnRewind   = (PFN_Rewind)GetProcAddress(hWebM, "CialloWebM_Rewind");
    PFN_Close   pfnClose    = (PFN_Close)GetProcAddress(hWebM, "CialloWebM_Close");

    if (!pfnOpen || !pfnGetInfo || !pfnRead || !pfnClose)
    {
        FreeLibrary(hWebM);
        return;
    }

    CIALLO_WEBM_HANDLE_T decoder = pfnOpen(webmData, webmSize);
    if (!decoder)
    {
        FreeLibrary(hWebM);
        return;
    }

    CialloWebMInfo_T info;
    pfnGetInfo(decoder, &info);

    int W = settings.width > 0 ? settings.width : (int)info.width;
    int H = settings.height > 0 ? settings.height : (int)info.height;

    /* 位置计算（与现有代码相同） */
    const int MARGIN = 20;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX, posY;
    switch (settings.position)
    {
    case 2: posX = MARGIN; posY = MARGIN; break;
    case 3: posX = screenW - W - MARGIN; posY = MARGIN; break;
    case 4: posX = MARGIN; posY = screenH - H - MARGIN; break;
    case 5: posX = screenW - W - MARGIN; posY = screenH - H - MARGIN; break;
    default: posX = (screenW - W) / 2; posY = (screenH - H) / 2; break;
    }

    BYTE opacity = (BYTE)(settings.opacity * 255 / 100);

    /* 创建分层窗口 */
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static const wchar_t* cls = L"CialloWebMSplashClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW; /* WebM 闪屏用简化的窗口过程 */
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        cls, L"", WS_POPUP,
        posX, posY, W, H, nullptr, nullptr, hInst, nullptr);

    if (!hWnd)
    {
        pfnClose(decoder);
        FreeLibrary(hWebM);
        return;
    }

    /* 播放循环 */
    DWORD totalMs = settings.durationMs > 0 ? (DWORD)settings.durationMs : 0;
    DWORD startTick = GetTickCount();
    int firstFrame = 1;

    CialloWebMFrame_T frame;
    while (pfnRead(decoder, &frame) == 0 /* CIALLO_WEBM_OK */)
    {
        /* 超时检查 */
        if (totalMs > 0 && (GetTickCount() - startTick) >= totalMs)
        {
            break;
        }

        /* 创建 DIB 并用 UpdateLayeredWindow 显示 */
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)frame.width;
        bmi.bmiHeader.biHeight = -(LONG)frame.height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC scDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(scDC);
        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(scDC, &bmi, DIB_RGB_COLORS,
                                         &bits, nullptr, 0);
        if (hBmp && bits)
        {
            /* 如果需要缩放，这里可以用 StretchBlt 或 GDI+
             * 如果 WebM 分辨率等于目标尺寸则直接复制 */
            if (frame.width == (uint32_t)W && frame.height == (uint32_t)H)
            {
                memcpy(bits, frame.pixels, frame.stride * frame.height);
            }
            else
            {
                /* 简单最近邻缩放（保持性能） */
                uint8_t* dst = (uint8_t*)bits;
                for (int y = 0; y < H; ++y)
                {
                    int srcY = y * (int)frame.height / H;
                    const uint8_t* srcRow = frame.pixels + srcY * frame.stride;
                    for (int x = 0; x < W; ++x)
                    {
                        int srcX = x * (int)frame.width / W;
                        const uint8_t* sp = srcRow + srcX * 4;
                        dst[0] = sp[0];
                        dst[1] = sp[1];
                        dst[2] = sp[2];
                        dst[3] = sp[3];
                        dst += 4;
                    }
                }
            }

            HBITMAP old = (HBITMAP)SelectObject(memDC, hBmp);
            BLENDFUNCTION bl = {};
            bl.BlendOp = AC_SRC_OVER;
            bl.SourceConstantAlpha = opacity;
            bl.AlphaFormat = AC_SRC_ALPHA;
            POINT src = { 0, 0 };
            SIZE sz = { (LONG)W, (LONG)H };
            POINT pos = { (LONG)posX, (LONG)posY };
            UpdateLayeredWindow(hWnd, scDC, &pos, &sz, memDC, &src,
                                0, &bl, ULW_ALPHA);
            SelectObject(memDC, old);
            DeleteObject(hBmp);
        }
        DeleteDC(memDC);
        ReleaseDC(nullptr, scDC);

        if (firstFrame)
        {
            ShowWindow(hWnd, SW_SHOW);
            firstFrame = 0;
        }

        /* 帧间延迟 */
        DWORD delay = frame.duration_ms;
        if (delay < 1) delay = 33;
        if (delay > 1000) delay = 1000;
        Sleep(delay);

        /* 处理消息（允许关闭） */
        MSG msg;
        while (PeekMessageW(&msg, hWnd, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                goto done;
            }
            DispatchMessageW(&msg);
        }
    }

done:
    DestroyWindow(hWnd);
    UnregisterClassW(cls, hInst);
    pfnClose(decoder);
    FreeLibrary(hWebM);
}

/*
 * === 修改 ShowSplashFromEntryPoint() 中的调度逻辑 ===
 *
 * 在 "if (!found || imgData.empty()) return;" 之后，
 * 将原来的 "RunSplashAnimationSafe(imgData.data(), imgData.size(), &ss);"
 * 替换为：
 *
 *   // 判断文件扩展名
 *   bool isWebM = false;
 *   {
 *       size_t dot = ss.imageFile.find_last_of(L'.');
 *       if (dot != std::wstring::npos)
 *       {
 *           std::wstring ext = ToLowerCopy(ss.imageFile.substr(dot));
 *           isWebM = (ext == L".webm");
 *       }
 *   }
 *
 *   if (isWebM)
 *   {
 *       __try { RunWebMSplashAnimation(imgData.data(), imgData.size(), ss); }
 *       __except (EXCEPTION_EXECUTE_HANDLER) { }
 *   }
 *   else
 *   {
 *       RunSplashAnimationSafe(imgData.data(), imgData.size(), &ss);
 *   }
 */

#endif /* CIALLO_SPLASH_WEBM_INTEGRATION_H */
