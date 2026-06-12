/*
 * ciallo_webm_player.c — CialloWebM 简易透明窗口播放器
 *
 * 功能：
 * - 加载 .webm 文件并逐帧解码
 * - 使用 WS_EX_LAYERED + UpdateLayeredWindow 实现真透明窗口
 * - 支持 VP9 Alpha 通道
 * - 支持循环播放、拖拽移动、按 ESC/点击关闭
 *
 * 用法：
 *   ciallo_webm_player.exe <文件路径.webm> [选项]
 *
 * 选项：
 *   --loop          循环播放
 *   --pos X Y       窗口位置
 *   --center        居中显示（默认）
 *   --topmost       置顶窗口（默认开启）
 *   --click-close   点击关闭
 *   --drag          拖拽移动
 *   --opacity N     透明度 0~100（默认 100）
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ciallo_webm.h"

/* ---- 播放器状态 ---- */
typedef struct PlayerState
{
    CIALLO_WEBM_HANDLE decoder;
    HWND               hwnd;
    DWORD              start_tick;
    int                width, height;
    BYTE               opacity;

    int                loop;
    int                click_close;
    int                drag;
    int                topmost;

    int                is_dragging;
    int                drag_start_x, drag_start_y;
    int                win_start_x, win_start_y;

    int                frame_ready;
    CialloWebMFrame    current_frame;

    /* 帧计时 */
    DWORD              next_frame_tick; /* 下一帧应该显示的时刻 */
} PlayerState;

static PlayerState g_ps = { 0 };

/* ---- 渲染当前帧 ---- */
static void RenderFrame(void)
{
    if (!g_ps.frame_ready || !g_ps.hwnd)
    {
        return;
    }

    const CialloWebMFrame* f = &g_ps.current_frame;

    /* 创建 DIB Section */
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)f->width;
    bmi.bmiHeader.biHeight = -(LONG)f->height; /* 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC scDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(scDC);

    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(scDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hBmp || !bits)
    {
        DeleteDC(memDC);
        ReleaseDC(NULL, scDC);
        return;
    }

    /* 复制帧数据到 DIB */
    memcpy(bits, f->pixels, f->stride * f->height);

    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    /* UpdateLayeredWindow */
    RECT wr;
    GetWindowRect(g_ps.hwnd, &wr);
    POINT pos = { wr.left, wr.top };
    SIZE sz = { (LONG)f->width, (LONG)f->height };
    POINT src = { 0, 0 };
    BLENDFUNCTION bl;
    bl.BlendOp = AC_SRC_OVER;
    bl.BlendFlags = 0;
    bl.SourceConstantAlpha = g_ps.opacity;
    bl.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(g_ps.hwnd, scDC, &pos, &sz, memDC, &src, 0, &bl,
                        ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, scDC);
}

/* ---- 推进到下一帧 ---- */
static int AdvanceFrame(void)
{
    int result = CialloWebM_ReadFrame(g_ps.decoder, &g_ps.current_frame);
    if (result == CIALLO_WEBM_OK)
    {
        g_ps.frame_ready = 1;
        return 1;
    }

    if (result == CIALLO_WEBM_EOF && g_ps.loop)
    {
        CialloWebM_Rewind(g_ps.decoder);
        result = CialloWebM_ReadFrame(g_ps.decoder, &g_ps.current_frame);
        if (result == CIALLO_WEBM_OK)
        {
            g_ps.frame_ready = 1;
            return 1;
        }
    }

    g_ps.frame_ready = 0;
    return 0;
}

/* ---- 窗口过程 ---- */
static LRESULT CALLBACK PlayerWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == 1)
        {
            DWORD now = GetTickCount();

            /* 只有当到达下一帧的显示时刻时才推进 */
            if (now >= g_ps.next_frame_tick)
            {
                if (AdvanceFrame())
                {
                    RenderFrame();

                    /* 计算下一帧的显示时刻 */
                    UINT delay = g_ps.current_frame.duration_ms;
                    if (delay < 1) delay = 33;  /* 默认 ~30fps */
                    if (delay > 1000) delay = 1000;
                    g_ps.next_frame_tick = now + delay;
                }
                else
                {
                    /* 播放完毕 */
                    KillTimer(hwnd, 1);
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        if (g_ps.click_close)
        {
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
            return 0;
        }
        if (g_ps.drag)
        {
            g_ps.is_dragging = 1;
            POINT cursor;
            GetCursorPos(&cursor);
            g_ps.drag_start_x = cursor.x;
            g_ps.drag_start_y = cursor.y;
            RECT wr;
            GetWindowRect(hwnd, &wr);
            g_ps.win_start_x = wr.left;
            g_ps.win_start_y = wr.top;
            SetCapture(hwnd);
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        if (g_ps.is_dragging)
        {
            POINT cursor;
            GetCursorPos(&cursor);
            int nx = g_ps.win_start_x + (cursor.x - g_ps.drag_start_x);
            int ny = g_ps.win_start_y + (cursor.y - g_ps.drag_start_y);
            SetWindowPos(hwnd, NULL, nx, ny, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            /* 重新渲染（UpdateLayeredWindow 需要新位置） */
            if (g_ps.frame_ready)
            {
                RenderFrame();
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_ps.is_dragging)
        {
            g_ps.is_dragging = 0;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---- 命令行解析 ---- */
typedef struct CmdArgs
{
    const wchar_t* file_path;
    int            loop;
    int            click_close;
    int            drag;
    int            topmost;
    int            center;
    int            pos_x, pos_y;
    int            opacity;
} CmdArgs;

static void parse_args(int argc, wchar_t** argv, CmdArgs* args)
{
    memset(args, 0, sizeof(*args));
    args->topmost = 1;
    args->center = 1;
    args->opacity = 100;

    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--loop") == 0)
        {
            args->loop = 1;
        }
        else if (wcscmp(argv[i], L"--click-close") == 0)
        {
            args->click_close = 1;
        }
        else if (wcscmp(argv[i], L"--drag") == 0)
        {
            args->drag = 1;
        }
        else if (wcscmp(argv[i], L"--topmost") == 0)
        {
            args->topmost = 1;
        }
        else if (wcscmp(argv[i], L"--no-topmost") == 0)
        {
            args->topmost = 0;
        }
        else if (wcscmp(argv[i], L"--center") == 0)
        {
            args->center = 1;
        }
        else if (wcscmp(argv[i], L"--pos") == 0 && i + 2 < argc)
        {
            args->pos_x = _wtoi(argv[i + 1]);
            args->pos_y = _wtoi(argv[i + 2]);
            args->center = 0;
            i += 2;
        }
        else if (wcscmp(argv[i], L"--opacity") == 0 && i + 1 < argc)
        {
            args->opacity = _wtoi(argv[i + 1]);
            if (args->opacity < 0) args->opacity = 0;
            if (args->opacity > 100) args->opacity = 100;
            ++i;
        }
        else if (argv[i][0] != L'-')
        {
            args->file_path = argv[i];
        }
    }
}

/* ---- 主函数 ---- */
int wmain(int argc, wchar_t** argv)
{
    CmdArgs args;
    parse_args(argc, argv, &args);

    if (!args.file_path)
    {
        wprintf(L"CialloWebM Player v%hs\n", CialloWebM_GetVersion());
        wprintf(L"用法: ciallo_webm_player.exe <文件.webm> [选项]\n");
        wprintf(L"\n选项:\n");
        wprintf(L"  --loop          循环播放\n");
        wprintf(L"  --pos X Y       窗口位置\n");
        wprintf(L"  --center        居中显示（默认）\n");
        wprintf(L"  --drag          拖拽移动\n");
        wprintf(L"  --click-close   点击关闭\n");
        wprintf(L"  --opacity N     透明度 0~100\n");
        wprintf(L"\n按 ESC 键关闭窗口\n");
        return 1;
    }

    /* 打开 WebM */
    CIALLO_WEBM_HANDLE decoder = CialloWebM_Open(args.file_path);
    if (!decoder)
    {
        wprintf(L"错误: 无法打开 %s\n", args.file_path);
        return 1;
    }

    CialloWebMInfo info;
    CialloWebM_GetInfo(decoder, &info);
    wprintf(L"文件: %s\n", args.file_path);
    wprintf(L"分辨率: %ux%u  编码: VP%d  Alpha: %s  帧率: %.1f\n",
            info.width, info.height, info.codec,
            info.has_alpha ? L"是" : L"否",
            info.fps > 0 ? info.fps : 30.0);

    /* 设置播放器状态 */
    g_ps.decoder = decoder;
    g_ps.width = (int)info.width;
    g_ps.height = (int)info.height;
    g_ps.opacity = (BYTE)(args.opacity * 255 / 100);
    g_ps.loop = args.loop;
    g_ps.click_close = args.click_close;
    g_ps.drag = args.drag;
    g_ps.topmost = args.topmost;

    /* 计算窗口位置 */
    int pos_x, pos_y;
    if (args.center)
    {
        pos_x = (GetSystemMetrics(SM_CXSCREEN) - g_ps.width) / 2;
        pos_y = (GetSystemMetrics(SM_CYSCREEN) - g_ps.height) / 2;
    }
    else
    {
        pos_x = args.pos_x;
        pos_y = args.pos_y;
    }

    /* 创建窗口 */
    HINSTANCE hInst = GetModuleHandleW(NULL);
    static const wchar_t* cls = L"CialloWebMPlayerClass";
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PlayerWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, args.drag ? IDC_SIZEALL : IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    if (args.topmost) exStyle |= WS_EX_TOPMOST;

    HWND hwnd = CreateWindowExW(exStyle, cls, L"", WS_POPUP,
                                 pos_x, pos_y, g_ps.width, g_ps.height,
                                 NULL, NULL, hInst, NULL);
    if (!hwnd)
    {
        wprintf(L"错误: 创建窗口失败\n");
        CialloWebM_Close(decoder);
        return 1;
    }
    g_ps.hwnd = hwnd;

    /* 解码并渲染第一帧 */
    if (AdvanceFrame())
    {
        RenderFrame();
        /* 根据第一帧的持续时间设定下一帧时刻 */
        UINT delay = g_ps.current_frame.duration_ms;
        if (delay < 1) delay = 33;
        g_ps.next_frame_tick = GetTickCount() + delay;
    }

    ShowWindow(hwnd, SW_SHOW);
    SetTimer(hwnd, 1, 16, NULL); /* ~60fps 轮询 */

    /* 消息循环 */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* 清理 */
    UnregisterClassW(cls, hInst);
    CialloWebM_Close(decoder);

    wprintf(L"播放结束\n");
    return 0;
}
