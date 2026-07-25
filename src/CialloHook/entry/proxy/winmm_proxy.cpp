#include <Windows.h>
#include <mmsystem.h>
#include "../../config/build_options.h"

#if defined(_MSC_VER) && defined(_WIN32) && CIALLOHOOK_FEATURE_CODECRYPT_PATCH
#define CIALLOHOOK_WINMM_PROTECTED_BEGIN __pragma(code_seg(push, ".lpksc$m"))
#define CIALLOHOOK_WINMM_PROTECTED_END __pragma(code_seg(pop))
#else
#define CIALLOHOOK_WINMM_PROTECTED_BEGIN
#define CIALLOHOOK_WINMM_PROTECTED_END
#endif

CIALLOHOOK_WINMM_PROTECTED_BEGIN

#ifndef _WIN64
using Fn_mciGetErrorStringA = decltype(&mciGetErrorStringA);
using Fn_mciGetErrorStringW = decltype(&mciGetErrorStringW);
using Fn_mciSendCommandA = decltype(&mciSendCommandA);
using Fn_mciSendCommandW = decltype(&mciSendCommandW);
using Fn_mciSendStringA = decltype(&mciSendStringA);
using Fn_mciSendStringW = decltype(&mciSendStringW);
using Fn_midiOutClose = decltype(&midiOutClose);
using Fn_midiOutGetNumDevs = decltype(&midiOutGetNumDevs);
using Fn_midiOutLongMsg = decltype(&midiOutLongMsg);
using Fn_midiOutMessage = decltype(&midiOutMessage);
using Fn_midiOutOpen = decltype(&midiOutOpen);
using Fn_midiOutPrepareHeader = decltype(&midiOutPrepareHeader);
using Fn_midiOutReset = decltype(&midiOutReset);
using Fn_midiOutSetVolume = decltype(&midiOutSetVolume);
using Fn_midiOutShortMsg = decltype(&midiOutShortMsg);
using Fn_midiOutUnprepareHeader = decltype(&midiOutUnprepareHeader);
using Fn_midiInAddBuffer = decltype(&midiInAddBuffer);
using Fn_midiInClose = decltype(&midiInClose);
using Fn_midiInGetNumDevs = decltype(&midiInGetNumDevs);
using Fn_midiInMessage = decltype(&midiInMessage);
using Fn_midiInOpen = decltype(&midiInOpen);
using Fn_midiInPrepareHeader = decltype(&midiInPrepareHeader);
using Fn_midiInReset = decltype(&midiInReset);
using Fn_midiInStart = decltype(&midiInStart);
using Fn_midiInStop = decltype(&midiInStop);
using Fn_midiInUnprepareHeader = decltype(&midiInUnprepareHeader);
using Fn_mixerClose = decltype(&mixerClose);
using Fn_mixerGetControlDetailsA = decltype(&mixerGetControlDetailsA);
using Fn_mixerGetControlDetailsW = decltype(&mixerGetControlDetailsW);
using Fn_mixerGetDevCapsA = decltype(&mixerGetDevCapsA);
using Fn_mixerGetDevCapsW = decltype(&mixerGetDevCapsW);
using Fn_mixerGetLineControlsA = decltype(&mixerGetLineControlsA);
using Fn_mixerGetLineControlsW = decltype(&mixerGetLineControlsW);
using Fn_mixerGetLineInfoA = decltype(&mixerGetLineInfoA);
using Fn_mixerGetLineInfoW = decltype(&mixerGetLineInfoW);
using Fn_mixerGetNumDevs = decltype(&mixerGetNumDevs);
using Fn_mixerMessage = decltype(&mixerMessage);
using Fn_mixerOpen = decltype(&mixerOpen);
using Fn_mixerSetControlDetails = decltype(&mixerSetControlDetails);
using Fn_timeBeginPeriod = decltype(&timeBeginPeriod);
using Fn_timeEndPeriod = decltype(&timeEndPeriod);
using Fn_timeGetDevCaps = decltype(&timeGetDevCaps);
using Fn_timeGetSystemTime = decltype(&timeGetSystemTime);
using Fn_timeGetTime = decltype(&timeGetTime);
using Fn_timeKillEvent = decltype(&timeKillEvent);
using Fn_timeSetEvent = decltype(&timeSetEvent);
using Fn_waveInAddBuffer = decltype(&waveInAddBuffer);
using Fn_waveInClose = decltype(&waveInClose);
using Fn_waveInGetNumDevs = decltype(&waveInGetNumDevs);
using Fn_waveInGetPosition = decltype(&waveInGetPosition);
using Fn_waveInMessage = decltype(&waveInMessage);
using Fn_waveInOpen = decltype(&waveInOpen);
using Fn_waveInPrepareHeader = decltype(&waveInPrepareHeader);
using Fn_waveInReset = decltype(&waveInReset);
using Fn_waveInStart = decltype(&waveInStart);
using Fn_waveInStop = decltype(&waveInStop);
using Fn_waveInUnprepareHeader = decltype(&waveInUnprepareHeader);
using Fn_waveOutBreakLoop = decltype(&waveOutBreakLoop);
using Fn_waveOutClose = decltype(&waveOutClose);
using Fn_waveOutGetNumDevs = decltype(&waveOutGetNumDevs);
using Fn_waveOutGetPosition = decltype(&waveOutGetPosition);
using Fn_waveOutGetVolume = decltype(&waveOutGetVolume);
using Fn_waveOutMessage = decltype(&waveOutMessage);
using Fn_waveOutOpen = decltype(&waveOutOpen);
using Fn_waveOutPause = decltype(&waveOutPause);
using Fn_waveOutPrepareHeader = decltype(&waveOutPrepareHeader);
using Fn_waveOutReset = decltype(&waveOutReset);
using Fn_waveOutRestart = decltype(&waveOutRestart);
using Fn_waveOutSetVolume = decltype(&waveOutSetVolume);
using Fn_waveOutUnprepareHeader = decltype(&waveOutUnprepareHeader);
using Fn_waveOutWrite = decltype(&waveOutWrite);

static INIT_ONCE g_winmmInitOnce = INIT_ONCE_STATIC_INIT;
static HMODULE g_realWinmm = nullptr;
static Fn_mciGetErrorStringA g_mciGetErrorStringA = nullptr;
static Fn_mciGetErrorStringW g_mciGetErrorStringW = nullptr;
static Fn_mciSendCommandA g_mciSendCommandA = nullptr;
static Fn_mciSendCommandW g_mciSendCommandW = nullptr;
static Fn_mciSendStringA g_mciSendStringA = nullptr;
static Fn_mciSendStringW g_mciSendStringW = nullptr;
static Fn_midiOutClose g_midiOutClose = nullptr;
static Fn_midiOutGetNumDevs g_midiOutGetNumDevs = nullptr;
static Fn_midiOutLongMsg g_midiOutLongMsg = nullptr;
static Fn_midiOutMessage g_midiOutMessage = nullptr;
static Fn_midiOutOpen g_midiOutOpen = nullptr;
static Fn_midiOutPrepareHeader g_midiOutPrepareHeader = nullptr;
static Fn_midiOutReset g_midiOutReset = nullptr;
static Fn_midiOutSetVolume g_midiOutSetVolume = nullptr;
static Fn_midiOutShortMsg g_midiOutShortMsg = nullptr;
static Fn_midiOutUnprepareHeader g_midiOutUnprepareHeader = nullptr;
static Fn_midiInAddBuffer g_midiInAddBuffer = nullptr;
static Fn_midiInClose g_midiInClose = nullptr;
static Fn_midiInGetNumDevs g_midiInGetNumDevs = nullptr;
static Fn_midiInMessage g_midiInMessage = nullptr;
static Fn_midiInOpen g_midiInOpen = nullptr;
static Fn_midiInPrepareHeader g_midiInPrepareHeader = nullptr;
static Fn_midiInReset g_midiInReset = nullptr;
static Fn_midiInStart g_midiInStart = nullptr;
static Fn_midiInStop g_midiInStop = nullptr;
static Fn_midiInUnprepareHeader g_midiInUnprepareHeader = nullptr;
static Fn_mixerClose g_mixerClose = nullptr;
static Fn_mixerGetControlDetailsA g_mixerGetControlDetailsA = nullptr;
static Fn_mixerGetControlDetailsW g_mixerGetControlDetailsW = nullptr;
static Fn_mixerGetDevCapsA g_mixerGetDevCapsA = nullptr;
static Fn_mixerGetDevCapsW g_mixerGetDevCapsW = nullptr;
static Fn_mixerGetLineControlsA g_mixerGetLineControlsA = nullptr;
static Fn_mixerGetLineControlsW g_mixerGetLineControlsW = nullptr;
static Fn_mixerGetLineInfoA g_mixerGetLineInfoA = nullptr;
static Fn_mixerGetLineInfoW g_mixerGetLineInfoW = nullptr;
static Fn_mixerGetNumDevs g_mixerGetNumDevs = nullptr;
static Fn_mixerMessage g_mixerMessage = nullptr;
static Fn_mixerOpen g_mixerOpen = nullptr;
static Fn_mixerSetControlDetails g_mixerSetControlDetails = nullptr;
static Fn_timeBeginPeriod g_timeBeginPeriod = nullptr;
static Fn_timeEndPeriod g_timeEndPeriod = nullptr;
static Fn_timeGetDevCaps g_timeGetDevCaps = nullptr;
static Fn_timeGetSystemTime g_timeGetSystemTime = nullptr;
static Fn_timeGetTime g_timeGetTime = nullptr;
static Fn_timeKillEvent g_timeKillEvent = nullptr;
static Fn_timeSetEvent g_timeSetEvent = nullptr;
static Fn_waveInAddBuffer g_waveInAddBuffer = nullptr;
static Fn_waveInClose g_waveInClose = nullptr;
static Fn_waveInGetNumDevs g_waveInGetNumDevs = nullptr;
static Fn_waveInGetPosition g_waveInGetPosition = nullptr;
static Fn_waveInMessage g_waveInMessage = nullptr;
static Fn_waveInOpen g_waveInOpen = nullptr;
static Fn_waveInPrepareHeader g_waveInPrepareHeader = nullptr;
static Fn_waveInReset g_waveInReset = nullptr;
static Fn_waveInStart g_waveInStart = nullptr;
static Fn_waveInStop g_waveInStop = nullptr;
static Fn_waveInUnprepareHeader g_waveInUnprepareHeader = nullptr;
static Fn_waveOutBreakLoop g_waveOutBreakLoop = nullptr;
static Fn_waveOutClose g_waveOutClose = nullptr;
static Fn_waveOutGetNumDevs g_waveOutGetNumDevs = nullptr;
static Fn_waveOutGetPosition g_waveOutGetPosition = nullptr;
static Fn_waveOutGetVolume g_waveOutGetVolume = nullptr;
static Fn_waveOutMessage g_waveOutMessage = nullptr;
static Fn_waveOutOpen g_waveOutOpen = nullptr;
static Fn_waveOutPause g_waveOutPause = nullptr;
static Fn_waveOutPrepareHeader g_waveOutPrepareHeader = nullptr;
static Fn_waveOutReset g_waveOutReset = nullptr;
static Fn_waveOutRestart g_waveOutRestart = nullptr;
static Fn_waveOutSetVolume g_waveOutSetVolume = nullptr;
static Fn_waveOutUnprepareHeader g_waveOutUnprepareHeader = nullptr;
static Fn_waveOutWrite g_waveOutWrite = nullptr;
#endif

#define FORWARDED_WINMM_EXPORTS(X) \
	X(mciExecute) \
	X(CloseDriver) \
	X(DefDriverProc) \
	X(DriverCallback) \
	X(DrvGetModuleHandle) \
	X(GetDriverModuleHandle) \
	X(NotifyCallbackData) \
	X(OpenDriver) \
	X(PlaySound) \
	X(PlaySoundA) \
	X(PlaySoundW) \
	X(SendDriverMessage) \
	X(WOW32DriverCallback) \
	X(WOW32ResolveMultiMediaHandle) \
	X(WOWAppExit) \
	X(aux32Message) \
	X(auxGetDevCapsA) \
	X(auxGetDevCapsW) \
	X(auxGetNumDevs) \
	X(auxGetVolume) \
	X(auxOutMessage) \
	X(auxSetVolume) \
	X(joy32Message) \
	X(joyConfigChanged) \
	X(joyGetDevCapsA) \
	X(joyGetDevCapsW) \
	X(joyGetNumDevs) \
	X(joyGetPos) \
	X(joyGetPosEx) \
	X(joyGetThreshold) \
	X(joyReleaseCapture) \
	X(joySetCapture) \
	X(joySetThreshold) \
	X(mci32Message) \
	X(mciDriverNotify) \
	X(mciDriverYield) \
	X(mciFreeCommandResource) \
	X(mciGetCreatorTask) \
	X(mciGetDeviceIDA) \
	X(mciGetDeviceIDFromElementIDA) \
	X(mciGetDeviceIDFromElementIDW) \
	X(mciGetDeviceIDW) \
	X(mciGetDriverData) \
	X(mciGetYieldProc) \
	X(mciLoadCommandResource) \
	X(mciSetDriverData) \
	X(mciSetYieldProc) \
	X(mid32Message) \
	X(midiConnect) \
	X(midiDisconnect) \
	X(midiInGetDevCapsA) \
	X(midiInGetDevCapsW) \
	X(midiInGetErrorTextA) \
	X(midiInGetErrorTextW) \
	X(midiInGetID) \
	X(midiOutCacheDrumPatches) \
	X(midiOutCachePatches) \
	X(midiOutGetDevCapsA) \
	X(midiOutGetDevCapsW) \
	X(midiOutGetErrorTextA) \
	X(midiOutGetErrorTextW) \
	X(midiOutGetID) \
	X(midiOutGetVolume) \
	X(midiStreamClose) \
	X(midiStreamOpen) \
	X(midiStreamOut) \
	X(midiStreamPause) \
	X(midiStreamPosition) \
	X(midiStreamProperty) \
	X(midiStreamRestart) \
	X(midiStreamStop) \
	X(mixerGetID) \
	X(mmDrvInstall) \
	X(mmGetCurrentTask) \
	X(mmTaskBlock) \
	X(mmTaskCreate) \
	X(mmTaskSignal) \
	X(mmTaskYield) \
	X(mmioAdvance) \
	X(mmioAscend) \
	X(mmioClose) \
	X(mmioCreateChunk) \
	X(mmioDescend) \
	X(mmioFlush) \
	X(mmioGetInfo) \
	X(mmioInstallIOProcA) \
	X(mmioInstallIOProcW) \
	X(mmioOpenA) \
	X(mmioOpenW) \
	X(mmioRead) \
	X(mmioRenameA) \
	X(mmioRenameW) \
	X(mmioSeek) \
	X(mmioSendMessage) \
	X(mmioSetBuffer) \
	X(mmioSetInfo) \
	X(mmioStringToFOURCCA) \
	X(mmioStringToFOURCCW) \
	X(mmioWrite) \
	X(mmsystemGetVersion) \
	X(mod32Message) \
	X(mxd32Message) \
	X(sndPlaySoundA) \
	X(sndPlaySoundW) \
	X(tid32Message) \
	X(waveInGetDevCapsA) \
	X(waveInGetDevCapsW) \
	X(waveInGetErrorTextA) \
	X(waveInGetErrorTextW) \
	X(waveInGetID) \
	X(waveOutGetDevCapsA) \
	X(waveOutGetDevCapsW) \
	X(waveOutGetErrorTextA) \
	X(waveOutGetErrorTextW) \
	X(waveOutGetID) \
	X(waveOutGetPitch) \
	X(waveOutGetPlaybackRate) \
	X(waveOutSetPitch) \
	X(waveOutSetPlaybackRate) \
	X(wid32Message) \
	X(wod32Message)

#ifndef _WIN64

#define DECLARE_FORWARD_PTR(fn) static FARPROC g_forward_##fn = nullptr;
FORWARDED_WINMM_EXPORTS(DECLARE_FORWARD_PTR)
static FARPROC g_forward_ordinal2 = nullptr;
#undef DECLARE_FORWARD_PTR
#endif

#ifndef _WIN64
static bool EnsureRealWinmm();

#define DEFINE_FORWARD_STUB(fn) \
	extern "C" __declspec(naked) void __cdecl CialloWinMM_##fn() \
	{ \
		__asm mov eax, dword ptr[g_forward_##fn] \
		__asm test eax, eax \
		__asm jne resolved_##fn \
		__asm pushad \
		__asm call EnsureRealWinmm \
		__asm popad \
		__asm mov eax, dword ptr[g_forward_##fn] \
		__asm test eax, eax \
		__asm jne resolved_##fn \
		__asm xor eax, eax \
		__asm ret \
		__asm resolved_##fn: \
		__asm jmp eax \
	}
FORWARDED_WINMM_EXPORTS(DEFINE_FORWARD_STUB)
extern "C" __declspec(naked) void __cdecl CialloWinMM_Ordinal2()
{
	__asm mov eax, dword ptr[g_forward_ordinal2]
	__asm test eax, eax
	__asm jne resolved_ordinal2
	__asm pushad
	__asm call EnsureRealWinmm
	__asm popad
	__asm mov eax, dword ptr[g_forward_ordinal2]
	__asm test eax, eax
	__asm jne resolved_ordinal2
	__asm xor eax, eax
	__asm ret
	__asm resolved_ordinal2:
	__asm jmp eax
}
#undef DEFINE_FORWARD_STUB
#endif

#ifndef _WIN64
static BOOL CALLBACK InitRealWinmm(PINIT_ONCE, PVOID, PVOID*)
{
	wchar_t realDllPath[MAX_PATH] = {};
	if (GetSystemDirectoryW(realDllPath, MAX_PATH) == 0)
	{
		return FALSE;
	}
	wcscat_s(realDllPath, L"\\winmm.dll");

	g_realWinmm = LoadLibraryW(realDllPath);
	if (g_realWinmm == nullptr)
	{
		return FALSE;
	}

#define RESOLVE_WINMM(fn) g_##fn = reinterpret_cast<Fn_##fn>(GetProcAddress(g_realWinmm, #fn))
	RESOLVE_WINMM(mciGetErrorStringA);
	RESOLVE_WINMM(mciGetErrorStringW);
	RESOLVE_WINMM(mciSendCommandA);
	RESOLVE_WINMM(mciSendCommandW);
	RESOLVE_WINMM(mciSendStringA);
	RESOLVE_WINMM(mciSendStringW);
	RESOLVE_WINMM(midiOutClose);
	RESOLVE_WINMM(midiOutGetNumDevs);
	RESOLVE_WINMM(midiOutLongMsg);
	RESOLVE_WINMM(midiOutMessage);
	RESOLVE_WINMM(midiOutOpen);
	RESOLVE_WINMM(midiOutPrepareHeader);
	RESOLVE_WINMM(midiOutReset);
	RESOLVE_WINMM(midiOutSetVolume);
	RESOLVE_WINMM(midiOutShortMsg);
	RESOLVE_WINMM(midiOutUnprepareHeader);
	RESOLVE_WINMM(midiInAddBuffer);
	RESOLVE_WINMM(midiInClose);
	RESOLVE_WINMM(midiInGetNumDevs);
	RESOLVE_WINMM(midiInMessage);
	RESOLVE_WINMM(midiInOpen);
	RESOLVE_WINMM(midiInPrepareHeader);
	RESOLVE_WINMM(midiInReset);
	RESOLVE_WINMM(midiInStart);
	RESOLVE_WINMM(midiInStop);
	RESOLVE_WINMM(midiInUnprepareHeader);
	RESOLVE_WINMM(mixerClose);
	RESOLVE_WINMM(mixerGetControlDetailsA);
	RESOLVE_WINMM(mixerGetControlDetailsW);
	RESOLVE_WINMM(mixerGetDevCapsA);
	RESOLVE_WINMM(mixerGetDevCapsW);
	RESOLVE_WINMM(mixerGetLineControlsA);
	RESOLVE_WINMM(mixerGetLineControlsW);
	RESOLVE_WINMM(mixerGetLineInfoA);
	RESOLVE_WINMM(mixerGetLineInfoW);
	RESOLVE_WINMM(mixerGetNumDevs);
	RESOLVE_WINMM(mixerMessage);
	RESOLVE_WINMM(mixerOpen);
	RESOLVE_WINMM(mixerSetControlDetails);
	RESOLVE_WINMM(timeBeginPeriod);
	RESOLVE_WINMM(timeEndPeriod);
	RESOLVE_WINMM(timeGetDevCaps);
	RESOLVE_WINMM(timeGetSystemTime);
	RESOLVE_WINMM(timeGetTime);
	RESOLVE_WINMM(timeKillEvent);
	RESOLVE_WINMM(timeSetEvent);
	RESOLVE_WINMM(waveInAddBuffer);
	RESOLVE_WINMM(waveInClose);
	RESOLVE_WINMM(waveInGetNumDevs);
	RESOLVE_WINMM(waveInGetPosition);
	RESOLVE_WINMM(waveInMessage);
	RESOLVE_WINMM(waveInOpen);
	RESOLVE_WINMM(waveInPrepareHeader);
	RESOLVE_WINMM(waveInReset);
	RESOLVE_WINMM(waveInStart);
	RESOLVE_WINMM(waveInStop);
	RESOLVE_WINMM(waveInUnprepareHeader);
	RESOLVE_WINMM(waveOutBreakLoop);
	RESOLVE_WINMM(waveOutClose);
	RESOLVE_WINMM(waveOutGetNumDevs);
	RESOLVE_WINMM(waveOutGetPosition);
	RESOLVE_WINMM(waveOutGetVolume);
	RESOLVE_WINMM(waveOutMessage);
	RESOLVE_WINMM(waveOutOpen);
	RESOLVE_WINMM(waveOutPause);
	RESOLVE_WINMM(waveOutPrepareHeader);
	RESOLVE_WINMM(waveOutReset);
	RESOLVE_WINMM(waveOutRestart);
	RESOLVE_WINMM(waveOutSetVolume);
	RESOLVE_WINMM(waveOutUnprepareHeader);
	RESOLVE_WINMM(waveOutWrite);
#undef RESOLVE_WINMM

#ifndef _WIN64
#define RESOLVE_FORWARD_WINMM(fn) g_forward_##fn = GetProcAddress(g_realWinmm, #fn);
	FORWARDED_WINMM_EXPORTS(RESOLVE_FORWARD_WINMM);
#undef RESOLVE_FORWARD_WINMM
	g_forward_ordinal2 = GetProcAddress(g_realWinmm, MAKEINTRESOURCEA(2));
#endif

	const bool baseResolved = g_mciGetErrorStringA && g_mciGetErrorStringW && g_mciSendCommandA && g_mciSendCommandW &&
		g_mciSendStringA && g_mciSendStringW && g_midiOutClose && g_midiOutGetNumDevs &&
		g_midiOutLongMsg && g_midiOutMessage && g_midiOutOpen && g_midiOutPrepareHeader &&
		g_midiOutReset && g_midiOutSetVolume && g_midiOutShortMsg && g_midiOutUnprepareHeader &&
		g_midiInAddBuffer && g_midiInClose && g_midiInGetNumDevs && g_midiInMessage &&
		g_midiInOpen && g_midiInPrepareHeader && g_midiInReset && g_midiInStart &&
		g_midiInStop && g_midiInUnprepareHeader && g_mixerClose && g_mixerGetControlDetailsA &&
		g_mixerGetControlDetailsW && g_mixerGetDevCapsA && g_mixerGetDevCapsW &&
		g_mixerGetLineControlsA && g_mixerGetLineControlsW && g_mixerGetLineInfoA &&
		g_mixerGetLineInfoW && g_mixerGetNumDevs && g_mixerMessage && g_mixerOpen &&
		g_mixerSetControlDetails && g_timeBeginPeriod && g_timeEndPeriod && g_timeGetDevCaps &&
		g_timeGetSystemTime && g_timeGetTime && g_timeKillEvent && g_timeSetEvent &&
		g_waveInAddBuffer && g_waveInClose && g_waveInGetNumDevs && g_waveInGetPosition &&
		g_waveInMessage && g_waveInOpen && g_waveInPrepareHeader && g_waveInReset &&
		g_waveInStart && g_waveInStop && g_waveInUnprepareHeader && g_waveOutBreakLoop &&
		g_waveOutClose && g_waveOutGetNumDevs && g_waveOutGetPosition && g_waveOutGetVolume &&
		g_waveOutMessage && g_waveOutOpen && g_waveOutPause && g_waveOutPrepareHeader &&
		g_waveOutReset && g_waveOutRestart && g_waveOutSetVolume && g_waveOutUnprepareHeader &&
		g_waveOutWrite;
#ifndef _WIN64
	return baseResolved;
#else
	return baseResolved;
#endif
}

static bool EnsureRealWinmm()
{
	return InitOnceExecuteOnce(&g_winmmInitOnce, InitRealWinmm, nullptr, nullptr) != FALSE && g_realWinmm != nullptr;
}

extern "C" bool CialloHook_EnsureRealWinmm()
{
	return EnsureRealWinmm();
}

extern "C" BOOL WINAPI CialloWinMM_mciGetErrorStringA(MCIERROR mcierr, LPSTR pszText, UINT cchText)
{
	if (!EnsureRealWinmm()) return FALSE;
	return g_mciGetErrorStringA(mcierr, pszText, cchText);
}

extern "C" MCIERROR WINAPI CialloWinMM_mciSendCommandA(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendCommandA(mciId, uMsg, dwParam1, dwParam2);
}

extern "C" BOOL WINAPI CialloWinMM_mciGetErrorStringW(MCIERROR mcierr, LPWSTR pszText, UINT cchText)
{
	if (!EnsureRealWinmm()) return FALSE;
	return g_mciGetErrorStringW(mcierr, pszText, cchText);
}

extern "C" MCIERROR WINAPI CialloWinMM_mciSendCommandW(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendCommandW(mciId, uMsg, dwParam1, dwParam2);
}

extern "C" MCIERROR WINAPI CialloWinMM_mciSendStringA(LPCSTR lpszCommand, LPSTR lpszReturnString, UINT cchReturn, HWND hwndCallback)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendStringA(lpszCommand, lpszReturnString, cchReturn, hwndCallback);
}

extern "C" MCIERROR WINAPI CialloWinMM_mciSendStringW(LPCWSTR lpszCommand, LPWSTR lpszReturnString, UINT cchReturn, HWND hwndCallback)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendStringW(lpszCommand, lpszReturnString, cchReturn, hwndCallback);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutClose(HMIDIOUT hmo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutClose(hmo);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutLongMsg(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutLongMsg(hmo, pmh, cbmh);
}

extern "C" DWORD WINAPI CialloWinMM_midiOutMessage(HMIDIOUT hmo, UINT uMsg, DWORD_PTR dw1, DWORD_PTR dw2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutMessage(hmo, uMsg, dw1, dw2);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutOpen(LPHMIDIOUT phmo, UINT uDeviceID, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutOpen(phmo, uDeviceID, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutPrepareHeader(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutPrepareHeader(hmo, pmh, cbmh);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutShortMsg(HMIDIOUT hmo, DWORD dwMsg)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutShortMsg(hmo, dwMsg);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutUnprepareHeader(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutUnprepareHeader(hmo, pmh, cbmh);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerClose(HMIXER hmx)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerClose(hmx);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetControlDetailsA(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetControlDetailsA(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetControlDetailsW(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetControlDetailsW(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetDevCapsA(UINT_PTR uMxId, LPMIXERCAPSA pmxcaps, UINT cbmxcaps)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetDevCapsA(uMxId, pmxcaps, cbmxcaps);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetDevCapsW(UINT_PTR uMxId, LPMIXERCAPSW pmxcaps, UINT cbmxcaps)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetDevCapsW(uMxId, pmxcaps, cbmxcaps);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetLineControlsA(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineControlsA(hmxobj, pmxlc, fdwControls);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetLineControlsW(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSW pmxlc, DWORD fdwControls)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineControlsW(hmxobj, pmxlc, fdwControls);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetLineInfoA(HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineInfoA(hmxobj, pmxl, fdwInfo);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerGetLineInfoW(HMIXEROBJ hmxobj, LPMIXERLINEW pmxl, DWORD fdwInfo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineInfoW(hmxobj, pmxl, fdwInfo);
}

extern "C" UINT WINAPI CialloWinMM_mixerGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_mixerGetNumDevs();
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerOpen(LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerOpen(phmx, uMxId, dwCallback, dwInstance, fdwOpen);
}

extern "C" DWORD WINAPI CialloWinMM_mixerMessage(HMIXER hmx, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerMessage(hmx, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI CialloWinMM_mixerSetControlDetails(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerSetControlDetails(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI CialloWinMM_timeBeginPeriod(UINT uPeriod)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeBeginPeriod(uPeriod);
}

extern "C" MMRESULT WINAPI CialloWinMM_timeEndPeriod(UINT uPeriod)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeEndPeriod(uPeriod);
}

extern "C" MMRESULT WINAPI CialloWinMM_timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeGetDevCaps(ptc, cbtc);
}

extern "C" MMRESULT WINAPI CialloWinMM_timeGetSystemTime(LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeGetSystemTime(pmmt, cbmmt);
}

extern "C" DWORD WINAPI CialloWinMM_timeGetTime(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_timeGetTime();
}

extern "C" MMRESULT WINAPI CialloWinMM_timeKillEvent(UINT uTimerID)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeKillEvent(uTimerID);
}

extern "C" MMRESULT WINAPI CialloWinMM_timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc, DWORD_PTR dwUser, UINT fuEvent)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeSetEvent(uDelay, uResolution, lpTimeProc, dwUser, fuEvent);
}

extern "C" UINT WINAPI CialloWinMM_midiOutGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_midiOutGetNumDevs();
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutReset(HMIDIOUT hmo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutReset(hmo);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiOutSetVolume(HMIDIOUT hmo, DWORD dwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutSetVolume(hmo, dwVolume);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInAddBuffer(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInAddBuffer(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInClose(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInClose(hmi);
}

extern "C" UINT WINAPI CialloWinMM_midiInGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_midiInGetNumDevs();
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInMessage(HMIDIIN hmi, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInMessage(hmi, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInOpen(LPHMIDIIN phmi, UINT uDeviceID, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInOpen(phmi, uDeviceID, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInPrepareHeader(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInPrepareHeader(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInReset(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInReset(hmi);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInStart(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInStart(hmi);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInStop(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInStop(hmi);
}

extern "C" MMRESULT WINAPI CialloWinMM_midiInUnprepareHeader(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInUnprepareHeader(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInAddBuffer(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInAddBuffer(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInClose(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInClose(hwi);
}

extern "C" UINT WINAPI CialloWinMM_waveInGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_waveInGetNumDevs();
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInGetPosition(HWAVEIN hwi, LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInGetPosition(hwi, pmmt, cbmmt);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInMessage(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInMessage(hwi, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInOpen(LPHWAVEIN phwi, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInOpen(phwi, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInPrepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInPrepareHeader(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInReset(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInReset(hwi);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInStart(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInStart(hwi);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInStop(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInStop(hwi);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveInUnprepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInUnprepareHeader(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutBreakLoop(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutBreakLoop(hwo);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutClose(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutClose(hwo);
}

extern "C" UINT WINAPI CialloWinMM_waveOutGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_waveOutGetNumDevs();
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutGetPosition(HWAVEOUT hwo, LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutGetPosition(hwo, pmmt, cbmmt);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutGetVolume(HWAVEOUT hwo, LPDWORD pdwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutGetVolume(hwo, pdwVolume);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutMessage(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutMessage(hwo, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutOpen(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutPause(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutPause(hwo);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutPrepareHeader(hwo, pwh, cbwh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutReset(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutReset(hwo);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutRestart(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutRestart(hwo);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutSetVolume(hwo, dwVolume);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutUnprepareHeader(hwo, pwh, cbwh);
}

extern "C" MMRESULT WINAPI CialloWinMM_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutWrite(hwo, pwh, cbwh);
}
#endif

CIALLOHOOK_WINMM_PROTECTED_END

#ifdef _WIN64
static INIT_ONCE g_winmmInitOnce = INIT_ONCE_STATIC_INIT;
static HMODULE g_realWinmm = nullptr;

extern "C"
{
	// x64 MASM stubs index this table directly by the native winmm ordinal.
	// The largest current x64 winmm ordinal is 182; slot zero and slot one are unused.
	PVOID g_cialloWinmmX64Targets[183] = {};
}

static BOOL CALLBACK InitRealWinmm(PINIT_ONCE, PVOID, PVOID*)
{
	wchar_t realDllPath[MAX_PATH] = {};
	if (GetSystemDirectoryW(realDllPath, MAX_PATH) == 0)
	{
		return FALSE;
	}
	wcscat_s(realDllPath, L"\\winmm.dll");

	g_realWinmm = LoadLibraryW(realDllPath);
	return g_realWinmm != nullptr;
}

static bool EnsureRealWinmm()
{
	return InitOnceExecuteOnce(&g_winmmInitOnce, InitRealWinmm, nullptr, nullptr) != FALSE && g_realWinmm != nullptr;
}

extern "C" bool CialloHook_EnsureRealWinmm()
{
	return EnsureRealWinmm();
}

extern "C" FARPROC CialloHook_ResolveWinmmX64(UINT ordinal)
{
	if (ordinal < 2 || ordinal >= _countof(g_cialloWinmmX64Targets))
	{
		return nullptr;
	}

	PVOID volatile* targetSlot = &g_cialloWinmmX64Targets[ordinal];
	PVOID cached = InterlockedCompareExchangePointer(targetSlot, nullptr, nullptr);
	if (cached != nullptr)
	{
		return reinterpret_cast<FARPROC>(cached);
	}

	if (!EnsureRealWinmm())
	{
		return nullptr;
	}

	FARPROC resolved = GetProcAddress(g_realWinmm, MAKEINTRESOURCEA(ordinal));
	if (resolved == nullptr)
	{
		return nullptr;
	}

	PVOID resolvedPointer = reinterpret_cast<PVOID>(resolved);
	PVOID previous = InterlockedCompareExchangePointer(targetSlot, resolvedPointer, nullptr);
	return reinterpret_cast<FARPROC>(previous != nullptr ? previous : resolvedPointer);
}
#endif
