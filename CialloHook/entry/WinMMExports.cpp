#include <Windows.h>
#include <mmsystem.h>

extern "C" __declspec(dllexport) int HookFontWinMMExportAnchor = 1;

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
#define DEFINE_FORWARD_STUB(fn) \
	extern "C" __declspec(naked) void __cdecl HookFont_##fn() \
	{ \
		__asm jmp dword ptr[g_forward_##fn] \
	}
FORWARDED_WINMM_EXPORTS(DEFINE_FORWARD_STUB)
extern "C" __declspec(naked) void __cdecl HookFont_Ordinal2()
{
	__asm jmp dword ptr[g_forward_ordinal2]
}
#undef DEFINE_FORWARD_STUB
#endif

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

extern "C" BOOL WINAPI HookFont_mciGetErrorStringA(MCIERROR mcierr, LPSTR pszText, UINT cchText)
{
	if (!EnsureRealWinmm()) return FALSE;
	return g_mciGetErrorStringA(mcierr, pszText, cchText);
}

extern "C" MCIERROR WINAPI HookFont_mciSendCommandA(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendCommandA(mciId, uMsg, dwParam1, dwParam2);
}

extern "C" BOOL WINAPI HookFont_mciGetErrorStringW(MCIERROR mcierr, LPWSTR pszText, UINT cchText)
{
	if (!EnsureRealWinmm()) return FALSE;
	return g_mciGetErrorStringW(mcierr, pszText, cchText);
}

extern "C" MCIERROR WINAPI HookFont_mciSendCommandW(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendCommandW(mciId, uMsg, dwParam1, dwParam2);
}

extern "C" MCIERROR WINAPI HookFont_mciSendStringA(LPCSTR lpszCommand, LPSTR lpszReturnString, UINT cchReturn, HWND hwndCallback)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendStringA(lpszCommand, lpszReturnString, cchReturn, hwndCallback);
}

extern "C" MCIERROR WINAPI HookFont_mciSendStringW(LPCWSTR lpszCommand, LPWSTR lpszReturnString, UINT cchReturn, HWND hwndCallback)
{
	if (!EnsureRealWinmm()) return MCIERR_UNSUPPORTED_FUNCTION;
	return g_mciSendStringW(lpszCommand, lpszReturnString, cchReturn, hwndCallback);
}

extern "C" MMRESULT WINAPI HookFont_midiOutClose(HMIDIOUT hmo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutClose(hmo);
}

extern "C" MMRESULT WINAPI HookFont_midiOutLongMsg(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutLongMsg(hmo, pmh, cbmh);
}

extern "C" DWORD WINAPI HookFont_midiOutMessage(HMIDIOUT hmo, UINT uMsg, DWORD_PTR dw1, DWORD_PTR dw2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutMessage(hmo, uMsg, dw1, dw2);
}

extern "C" MMRESULT WINAPI HookFont_midiOutOpen(LPHMIDIOUT phmo, UINT uDeviceID, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutOpen(phmo, uDeviceID, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI HookFont_midiOutPrepareHeader(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutPrepareHeader(hmo, pmh, cbmh);
}

extern "C" MMRESULT WINAPI HookFont_midiOutShortMsg(HMIDIOUT hmo, DWORD dwMsg)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutShortMsg(hmo, dwMsg);
}

extern "C" MMRESULT WINAPI HookFont_midiOutUnprepareHeader(HMIDIOUT hmo, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutUnprepareHeader(hmo, pmh, cbmh);
}

extern "C" MMRESULT WINAPI HookFont_mixerClose(HMIXER hmx)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerClose(hmx);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetControlDetailsA(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetControlDetailsA(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetControlDetailsW(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetControlDetailsW(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetDevCapsA(UINT_PTR uMxId, LPMIXERCAPSA pmxcaps, UINT cbmxcaps)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetDevCapsA(uMxId, pmxcaps, cbmxcaps);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetDevCapsW(UINT_PTR uMxId, LPMIXERCAPSW pmxcaps, UINT cbmxcaps)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetDevCapsW(uMxId, pmxcaps, cbmxcaps);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetLineControlsA(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineControlsA(hmxobj, pmxlc, fdwControls);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetLineControlsW(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSW pmxlc, DWORD fdwControls)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineControlsW(hmxobj, pmxlc, fdwControls);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetLineInfoA(HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineInfoA(hmxobj, pmxl, fdwInfo);
}

extern "C" MMRESULT WINAPI HookFont_mixerGetLineInfoW(HMIXEROBJ hmxobj, LPMIXERLINEW pmxl, DWORD fdwInfo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerGetLineInfoW(hmxobj, pmxl, fdwInfo);
}

extern "C" UINT WINAPI HookFont_mixerGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_mixerGetNumDevs();
}

extern "C" MMRESULT WINAPI HookFont_mixerOpen(LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerOpen(phmx, uMxId, dwCallback, dwInstance, fdwOpen);
}

extern "C" DWORD WINAPI HookFont_mixerMessage(HMIXER hmx, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerMessage(hmx, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI HookFont_mixerSetControlDetails(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_mixerSetControlDetails(hmxobj, pmxcd, fdwDetails);
}

extern "C" MMRESULT WINAPI HookFont_timeBeginPeriod(UINT uPeriod)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeBeginPeriod(uPeriod);
}

extern "C" MMRESULT WINAPI HookFont_timeEndPeriod(UINT uPeriod)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeEndPeriod(uPeriod);
}

extern "C" MMRESULT WINAPI HookFont_timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeGetDevCaps(ptc, cbtc);
}

extern "C" MMRESULT WINAPI HookFont_timeGetSystemTime(LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeGetSystemTime(pmmt, cbmmt);
}

extern "C" DWORD WINAPI HookFont_timeGetTime(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_timeGetTime();
}

extern "C" MMRESULT WINAPI HookFont_timeKillEvent(UINT uTimerID)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeKillEvent(uTimerID);
}

extern "C" MMRESULT WINAPI HookFont_timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc, DWORD_PTR dwUser, UINT fuEvent)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_timeSetEvent(uDelay, uResolution, lpTimeProc, dwUser, fuEvent);
}

extern "C" UINT WINAPI HookFont_midiOutGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_midiOutGetNumDevs();
}

extern "C" MMRESULT WINAPI HookFont_midiOutReset(HMIDIOUT hmo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutReset(hmo);
}

extern "C" MMRESULT WINAPI HookFont_midiOutSetVolume(HMIDIOUT hmo, DWORD dwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiOutSetVolume(hmo, dwVolume);
}

extern "C" MMRESULT WINAPI HookFont_midiInAddBuffer(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInAddBuffer(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI HookFont_midiInClose(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInClose(hmi);
}

extern "C" UINT WINAPI HookFont_midiInGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_midiInGetNumDevs();
}

extern "C" MMRESULT WINAPI HookFont_midiInMessage(HMIDIIN hmi, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInMessage(hmi, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI HookFont_midiInOpen(LPHMIDIIN phmi, UINT uDeviceID, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInOpen(phmi, uDeviceID, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI HookFont_midiInPrepareHeader(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInPrepareHeader(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI HookFont_midiInReset(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInReset(hmi);
}

extern "C" MMRESULT WINAPI HookFont_midiInStart(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInStart(hmi);
}

extern "C" MMRESULT WINAPI HookFont_midiInStop(HMIDIIN hmi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInStop(hmi);
}

extern "C" MMRESULT WINAPI HookFont_midiInUnprepareHeader(HMIDIIN hmi, LPMIDIHDR pmh, UINT cbmh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_midiInUnprepareHeader(hmi, pmh, cbmh);
}

extern "C" MMRESULT WINAPI HookFont_waveInAddBuffer(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInAddBuffer(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI HookFont_waveInClose(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInClose(hwi);
}

extern "C" UINT WINAPI HookFont_waveInGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_waveInGetNumDevs();
}

extern "C" MMRESULT WINAPI HookFont_waveInGetPosition(HWAVEIN hwi, LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInGetPosition(hwi, pmmt, cbmmt);
}

extern "C" MMRESULT WINAPI HookFont_waveInMessage(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInMessage(hwi, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI HookFont_waveInOpen(LPHWAVEIN phwi, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInOpen(phwi, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI HookFont_waveInPrepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInPrepareHeader(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI HookFont_waveInReset(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInReset(hwi);
}

extern "C" MMRESULT WINAPI HookFont_waveInStart(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInStart(hwi);
}

extern "C" MMRESULT WINAPI HookFont_waveInStop(HWAVEIN hwi)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInStop(hwi);
}

extern "C" MMRESULT WINAPI HookFont_waveInUnprepareHeader(HWAVEIN hwi, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveInUnprepareHeader(hwi, pwh, cbwh);
}

extern "C" MMRESULT WINAPI HookFont_waveOutBreakLoop(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutBreakLoop(hwo);
}

extern "C" MMRESULT WINAPI HookFont_waveOutClose(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutClose(hwo);
}

extern "C" UINT WINAPI HookFont_waveOutGetNumDevs(void)
{
	if (!EnsureRealWinmm()) return 0;
	return g_waveOutGetNumDevs();
}

extern "C" MMRESULT WINAPI HookFont_waveOutGetPosition(HWAVEOUT hwo, LPMMTIME pmmt, UINT cbmmt)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutGetPosition(hwo, pmmt, cbmmt);
}

extern "C" MMRESULT WINAPI HookFont_waveOutGetVolume(HWAVEOUT hwo, LPDWORD pdwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutGetVolume(hwo, pdwVolume);
}

extern "C" MMRESULT WINAPI HookFont_waveOutMessage(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutMessage(hwo, uMsg, dwParam1, dwParam2);
}

extern "C" MMRESULT WINAPI HookFont_waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutOpen(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
}

extern "C" MMRESULT WINAPI HookFont_waveOutPause(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutPause(hwo);
}

extern "C" MMRESULT WINAPI HookFont_waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutPrepareHeader(hwo, pwh, cbwh);
}

extern "C" MMRESULT WINAPI HookFont_waveOutReset(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutReset(hwo);
}

extern "C" MMRESULT WINAPI HookFont_waveOutRestart(HWAVEOUT hwo)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutRestart(hwo);
}

extern "C" MMRESULT WINAPI HookFont_waveOutSetVolume(HWAVEOUT hwo, DWORD dwVolume)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutSetVolume(hwo, dwVolume);
}

extern "C" MMRESULT WINAPI HookFont_waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutUnprepareHeader(hwo, pwh, cbwh);
}

extern "C" MMRESULT WINAPI HookFont_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh)
{
	if (!EnsureRealWinmm()) return MMSYSERR_ERROR;
	return g_waveOutWrite(hwo, pwh, cbwh);
}

#ifdef _WIN64
#define EXPORT_FORWARD_WINMM(fn) __pragma(comment(linker, "/EXPORT:" #fn "=C:\\Windows\\System32\\winmm." #fn))
FORWARDED_WINMM_EXPORTS(EXPORT_FORWARD_WINMM)
#undef EXPORT_FORWARD_WINMM

#pragma comment(linker, "/EXPORT:mciGetErrorStringA=HookFont_mciGetErrorStringA")
#pragma comment(linker, "/EXPORT:mciGetErrorStringW=HookFont_mciGetErrorStringW")
#pragma comment(linker, "/EXPORT:mciSendCommandA=HookFont_mciSendCommandA")
#pragma comment(linker, "/EXPORT:mciSendCommandW=HookFont_mciSendCommandW")
#pragma comment(linker, "/EXPORT:mciSendStringA=HookFont_mciSendStringA")
#pragma comment(linker, "/EXPORT:mciSendStringW=HookFont_mciSendStringW")
#pragma comment(linker, "/EXPORT:midiOutClose=HookFont_midiOutClose")
#pragma comment(linker, "/EXPORT:midiOutGetNumDevs=HookFont_midiOutGetNumDevs")
#pragma comment(linker, "/EXPORT:midiOutLongMsg=HookFont_midiOutLongMsg")
#pragma comment(linker, "/EXPORT:midiOutMessage=HookFont_midiOutMessage")
#pragma comment(linker, "/EXPORT:midiOutOpen=HookFont_midiOutOpen")
#pragma comment(linker, "/EXPORT:midiOutPrepareHeader=HookFont_midiOutPrepareHeader")
#pragma comment(linker, "/EXPORT:midiOutReset=HookFont_midiOutReset")
#pragma comment(linker, "/EXPORT:midiOutSetVolume=HookFont_midiOutSetVolume")
#pragma comment(linker, "/EXPORT:midiOutShortMsg=HookFont_midiOutShortMsg")
#pragma comment(linker, "/EXPORT:midiOutUnprepareHeader=HookFont_midiOutUnprepareHeader")
#pragma comment(linker, "/EXPORT:midiInAddBuffer=HookFont_midiInAddBuffer")
#pragma comment(linker, "/EXPORT:midiInClose=HookFont_midiInClose")
#pragma comment(linker, "/EXPORT:midiInGetNumDevs=HookFont_midiInGetNumDevs")
#pragma comment(linker, "/EXPORT:midiInMessage=HookFont_midiInMessage")
#pragma comment(linker, "/EXPORT:midiInOpen=HookFont_midiInOpen")
#pragma comment(linker, "/EXPORT:midiInPrepareHeader=HookFont_midiInPrepareHeader")
#pragma comment(linker, "/EXPORT:midiInReset=HookFont_midiInReset")
#pragma comment(linker, "/EXPORT:midiInStart=HookFont_midiInStart")
#pragma comment(linker, "/EXPORT:midiInStop=HookFont_midiInStop")
#pragma comment(linker, "/EXPORT:midiInUnprepareHeader=HookFont_midiInUnprepareHeader")
#pragma comment(linker, "/EXPORT:mixerClose=HookFont_mixerClose")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsA=HookFont_mixerGetControlDetailsA")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsW=HookFont_mixerGetControlDetailsW")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsA=HookFont_mixerGetDevCapsA")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsW=HookFont_mixerGetDevCapsW")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsA=HookFont_mixerGetLineControlsA")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsW=HookFont_mixerGetLineControlsW")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoA=HookFont_mixerGetLineInfoA")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoW=HookFont_mixerGetLineInfoW")
#pragma comment(linker, "/EXPORT:mixerGetNumDevs=HookFont_mixerGetNumDevs")
#pragma comment(linker, "/EXPORT:mixerMessage=HookFont_mixerMessage")
#pragma comment(linker, "/EXPORT:mixerOpen=HookFont_mixerOpen")
#pragma comment(linker, "/EXPORT:mixerSetControlDetails=HookFont_mixerSetControlDetails")
#pragma comment(linker, "/EXPORT:timeBeginPeriod=HookFont_timeBeginPeriod")
#pragma comment(linker, "/EXPORT:timeEndPeriod=HookFont_timeEndPeriod")
#pragma comment(linker, "/EXPORT:timeGetDevCaps=HookFont_timeGetDevCaps")
#pragma comment(linker, "/EXPORT:timeGetSystemTime=HookFont_timeGetSystemTime")
#pragma comment(linker, "/EXPORT:timeGetTime=HookFont_timeGetTime")
#pragma comment(linker, "/EXPORT:timeKillEvent=HookFont_timeKillEvent")
#pragma comment(linker, "/EXPORT:timeSetEvent=HookFont_timeSetEvent")
#pragma comment(linker, "/EXPORT:waveInAddBuffer=HookFont_waveInAddBuffer")
#pragma comment(linker, "/EXPORT:waveInClose=HookFont_waveInClose")
#pragma comment(linker, "/EXPORT:waveInGetNumDevs=HookFont_waveInGetNumDevs")
#pragma comment(linker, "/EXPORT:waveInGetPosition=HookFont_waveInGetPosition")
#pragma comment(linker, "/EXPORT:waveInMessage=HookFont_waveInMessage")
#pragma comment(linker, "/EXPORT:waveInOpen=HookFont_waveInOpen")
#pragma comment(linker, "/EXPORT:waveInPrepareHeader=HookFont_waveInPrepareHeader")
#pragma comment(linker, "/EXPORT:waveInReset=HookFont_waveInReset")
#pragma comment(linker, "/EXPORT:waveInStart=HookFont_waveInStart")
#pragma comment(linker, "/EXPORT:waveInStop=HookFont_waveInStop")
#pragma comment(linker, "/EXPORT:waveInUnprepareHeader=HookFont_waveInUnprepareHeader")
#pragma comment(linker, "/EXPORT:waveOutBreakLoop=HookFont_waveOutBreakLoop")
#pragma comment(linker, "/EXPORT:waveOutClose=HookFont_waveOutClose")
#pragma comment(linker, "/EXPORT:waveOutGetNumDevs=HookFont_waveOutGetNumDevs")
#pragma comment(linker, "/EXPORT:waveOutGetPosition=HookFont_waveOutGetPosition")
#pragma comment(linker, "/EXPORT:waveOutGetVolume=HookFont_waveOutGetVolume")
#pragma comment(linker, "/EXPORT:waveOutMessage=HookFont_waveOutMessage")
#pragma comment(linker, "/EXPORT:waveOutOpen=HookFont_waveOutOpen")
#pragma comment(linker, "/EXPORT:waveOutPause=HookFont_waveOutPause")
#pragma comment(linker, "/EXPORT:waveOutPrepareHeader=HookFont_waveOutPrepareHeader")
#pragma comment(linker, "/EXPORT:waveOutReset=HookFont_waveOutReset")
#pragma comment(linker, "/EXPORT:waveOutRestart=HookFont_waveOutRestart")
#pragma comment(linker, "/EXPORT:waveOutSetVolume=HookFont_waveOutSetVolume")
#pragma comment(linker, "/EXPORT:waveOutUnprepareHeader=HookFont_waveOutUnprepareHeader")
#pragma comment(linker, "/EXPORT:waveOutWrite=HookFont_waveOutWrite")
#endif

#ifndef _WIN64
#pragma comment(linker, "/EXPORT:Noname2=_HookFont_Ordinal2,@2,NONAME")
#pragma comment(linker, "/EXPORT:mciExecute=_HookFont_mciExecute,@3")
#pragma comment(linker, "/EXPORT:CloseDriver=_HookFont_CloseDriver,@4")
#pragma comment(linker, "/EXPORT:DefDriverProc=_HookFont_DefDriverProc,@5")
#pragma comment(linker, "/EXPORT:DriverCallback=_HookFont_DriverCallback,@6")
#pragma comment(linker, "/EXPORT:DrvGetModuleHandle=_HookFont_DrvGetModuleHandle,@7")
#pragma comment(linker, "/EXPORT:GetDriverModuleHandle=_HookFont_GetDriverModuleHandle,@8")
#pragma comment(linker, "/EXPORT:NotifyCallbackData=_HookFont_NotifyCallbackData,@9")
#pragma comment(linker, "/EXPORT:OpenDriver=_HookFont_OpenDriver,@10")
#pragma comment(linker, "/EXPORT:PlaySound=_HookFont_PlaySound,@11")
#pragma comment(linker, "/EXPORT:PlaySoundA=_HookFont_PlaySoundA,@12")
#pragma comment(linker, "/EXPORT:PlaySoundW=_HookFont_PlaySoundW,@13")
#pragma comment(linker, "/EXPORT:SendDriverMessage=_HookFont_SendDriverMessage,@14")
#pragma comment(linker, "/EXPORT:WOW32DriverCallback=_HookFont_WOW32DriverCallback,@15")
#pragma comment(linker, "/EXPORT:WOW32ResolveMultiMediaHandle=_HookFont_WOW32ResolveMultiMediaHandle,@16")
#pragma comment(linker, "/EXPORT:WOWAppExit=_HookFont_WOWAppExit,@17")
#pragma comment(linker, "/EXPORT:aux32Message=_HookFont_aux32Message,@18")
#pragma comment(linker, "/EXPORT:auxGetDevCapsA=_HookFont_auxGetDevCapsA,@19")
#pragma comment(linker, "/EXPORT:auxGetDevCapsW=_HookFont_auxGetDevCapsW,@20")
#pragma comment(linker, "/EXPORT:auxGetNumDevs=_HookFont_auxGetNumDevs,@21")
#pragma comment(linker, "/EXPORT:auxGetVolume=_HookFont_auxGetVolume,@22")
#pragma comment(linker, "/EXPORT:auxOutMessage=_HookFont_auxOutMessage,@23")
#pragma comment(linker, "/EXPORT:auxSetVolume=_HookFont_auxSetVolume,@24")
#pragma comment(linker, "/EXPORT:joy32Message=_HookFont_joy32Message,@25")
#pragma comment(linker, "/EXPORT:joyConfigChanged=_HookFont_joyConfigChanged,@26")
#pragma comment(linker, "/EXPORT:joyGetDevCapsA=_HookFont_joyGetDevCapsA,@27")
#pragma comment(linker, "/EXPORT:joyGetDevCapsW=_HookFont_joyGetDevCapsW,@28")
#pragma comment(linker, "/EXPORT:joyGetNumDevs=_HookFont_joyGetNumDevs,@29")
#pragma comment(linker, "/EXPORT:joyGetPos=_HookFont_joyGetPos,@30")
#pragma comment(linker, "/EXPORT:joyGetPosEx=_HookFont_joyGetPosEx,@31")
#pragma comment(linker, "/EXPORT:joyGetThreshold=_HookFont_joyGetThreshold,@32")
#pragma comment(linker, "/EXPORT:joyReleaseCapture=_HookFont_joyReleaseCapture,@33")
#pragma comment(linker, "/EXPORT:joySetCapture=_HookFont_joySetCapture,@34")
#pragma comment(linker, "/EXPORT:joySetThreshold=_HookFont_joySetThreshold,@35")
#pragma comment(linker, "/EXPORT:mci32Message=_HookFont_mci32Message,@36")
#pragma comment(linker, "/EXPORT:mciDriverNotify=_HookFont_mciDriverNotify,@37")
#pragma comment(linker, "/EXPORT:mciDriverYield=_HookFont_mciDriverYield,@38")
#pragma comment(linker, "/EXPORT:mciFreeCommandResource=_HookFont_mciFreeCommandResource,@39")
#pragma comment(linker, "/EXPORT:mciGetCreatorTask=_HookFont_mciGetCreatorTask,@40")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDA=_HookFont_mciGetDeviceIDA,@41")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDA=_HookFont_mciGetDeviceIDFromElementIDA,@42")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDW=_HookFont_mciGetDeviceIDFromElementIDW,@43")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDW=_HookFont_mciGetDeviceIDW,@44")
#pragma comment(linker, "/EXPORT:mciGetDriverData=_HookFont_mciGetDriverData,@45")
#pragma comment(linker, "/EXPORT:mciGetErrorStringA=_HookFont_mciGetErrorStringA@12,@46")
#pragma comment(linker, "/EXPORT:mciGetErrorStringW=_HookFont_mciGetErrorStringW@12,@47")
#pragma comment(linker, "/EXPORT:mciGetYieldProc=_HookFont_mciGetYieldProc,@48")
#pragma comment(linker, "/EXPORT:mciLoadCommandResource=_HookFont_mciLoadCommandResource,@49")
#pragma comment(linker, "/EXPORT:mciSendCommandA=_HookFont_mciSendCommandA@16,@50")
#pragma comment(linker, "/EXPORT:mciSendCommandW=_HookFont_mciSendCommandW@16,@51")
#pragma comment(linker, "/EXPORT:mciSendStringA=_HookFont_mciSendStringA@16,@52")
#pragma comment(linker, "/EXPORT:mciSendStringW=_HookFont_mciSendStringW@16,@53")
#pragma comment(linker, "/EXPORT:mciSetDriverData=_HookFont_mciSetDriverData,@54")
#pragma comment(linker, "/EXPORT:mciSetYieldProc=_HookFont_mciSetYieldProc,@55")
#pragma comment(linker, "/EXPORT:mid32Message=_HookFont_mid32Message,@56")
#pragma comment(linker, "/EXPORT:midiConnect=_HookFont_midiConnect,@57")
#pragma comment(linker, "/EXPORT:midiDisconnect=_HookFont_midiDisconnect,@58")
#pragma comment(linker, "/EXPORT:midiInAddBuffer=_HookFont_midiInAddBuffer@12,@59")
#pragma comment(linker, "/EXPORT:midiInClose=_HookFont_midiInClose@4,@60")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsA=_HookFont_midiInGetDevCapsA,@61")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsW=_HookFont_midiInGetDevCapsW,@62")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextA=_HookFont_midiInGetErrorTextA,@63")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextW=_HookFont_midiInGetErrorTextW,@64")
#pragma comment(linker, "/EXPORT:midiInGetID=_HookFont_midiInGetID,@65")
#pragma comment(linker, "/EXPORT:midiInGetNumDevs=_HookFont_midiInGetNumDevs@0,@66")
#pragma comment(linker, "/EXPORT:midiInMessage=_HookFont_midiInMessage@16,@67")
#pragma comment(linker, "/EXPORT:midiInOpen=_HookFont_midiInOpen@20,@68")
#pragma comment(linker, "/EXPORT:midiInPrepareHeader=_HookFont_midiInPrepareHeader@12,@69")
#pragma comment(linker, "/EXPORT:midiInReset=_HookFont_midiInReset@4,@70")
#pragma comment(linker, "/EXPORT:midiInStart=_HookFont_midiInStart@4,@71")
#pragma comment(linker, "/EXPORT:midiInStop=_HookFont_midiInStop@4,@72")
#pragma comment(linker, "/EXPORT:midiInUnprepareHeader=_HookFont_midiInUnprepareHeader@12,@73")
#pragma comment(linker, "/EXPORT:midiOutCacheDrumPatches=_HookFont_midiOutCacheDrumPatches,@74")
#pragma comment(linker, "/EXPORT:midiOutCachePatches=_HookFont_midiOutCachePatches,@75")
#pragma comment(linker, "/EXPORT:midiOutClose=_HookFont_midiOutClose@4,@76")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsA=_HookFont_midiOutGetDevCapsA,@77")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsW=_HookFont_midiOutGetDevCapsW,@78")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextA=_HookFont_midiOutGetErrorTextA,@79")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextW=_HookFont_midiOutGetErrorTextW,@80")
#pragma comment(linker, "/EXPORT:midiOutGetID=_HookFont_midiOutGetID,@81")
#pragma comment(linker, "/EXPORT:midiOutGetNumDevs=_HookFont_midiOutGetNumDevs@0,@82")
#pragma comment(linker, "/EXPORT:midiOutGetVolume=_HookFont_midiOutGetVolume,@83")
#pragma comment(linker, "/EXPORT:midiOutLongMsg=_HookFont_midiOutLongMsg@12,@84")
#pragma comment(linker, "/EXPORT:midiOutMessage=_HookFont_midiOutMessage@16,@85")
#pragma comment(linker, "/EXPORT:midiOutOpen=_HookFont_midiOutOpen@20,@86")
#pragma comment(linker, "/EXPORT:midiOutPrepareHeader=_HookFont_midiOutPrepareHeader@12,@87")
#pragma comment(linker, "/EXPORT:midiOutReset=_HookFont_midiOutReset@4,@88")
#pragma comment(linker, "/EXPORT:midiOutSetVolume=_HookFont_midiOutSetVolume@8,@89")
#pragma comment(linker, "/EXPORT:midiOutShortMsg=_HookFont_midiOutShortMsg@8,@90")
#pragma comment(linker, "/EXPORT:midiOutUnprepareHeader=_HookFont_midiOutUnprepareHeader@12,@91")
#pragma comment(linker, "/EXPORT:midiStreamClose=_HookFont_midiStreamClose,@92")
#pragma comment(linker, "/EXPORT:midiStreamOpen=_HookFont_midiStreamOpen,@93")
#pragma comment(linker, "/EXPORT:midiStreamOut=_HookFont_midiStreamOut,@94")
#pragma comment(linker, "/EXPORT:midiStreamPause=_HookFont_midiStreamPause,@95")
#pragma comment(linker, "/EXPORT:midiStreamPosition=_HookFont_midiStreamPosition,@96")
#pragma comment(linker, "/EXPORT:midiStreamProperty=_HookFont_midiStreamProperty,@97")
#pragma comment(linker, "/EXPORT:midiStreamRestart=_HookFont_midiStreamRestart,@98")
#pragma comment(linker, "/EXPORT:midiStreamStop=_HookFont_midiStreamStop,@99")
#pragma comment(linker, "/EXPORT:mixerClose=_HookFont_mixerClose@4,@100")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsA=_HookFont_mixerGetControlDetailsA@12,@101")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsW=_HookFont_mixerGetControlDetailsW@12,@102")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsA=_HookFont_mixerGetDevCapsA@12,@103")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsW=_HookFont_mixerGetDevCapsW@12,@104")
#pragma comment(linker, "/EXPORT:mixerGetID=_HookFont_mixerGetID,@105")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsA=_HookFont_mixerGetLineControlsA@12,@106")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsW=_HookFont_mixerGetLineControlsW@12,@107")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoA=_HookFont_mixerGetLineInfoA@12,@108")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoW=_HookFont_mixerGetLineInfoW@12,@109")
#pragma comment(linker, "/EXPORT:mixerGetNumDevs=_HookFont_mixerGetNumDevs@0,@110")
#pragma comment(linker, "/EXPORT:mixerMessage=_HookFont_mixerMessage@16,@111")
#pragma comment(linker, "/EXPORT:mixerOpen=_HookFont_mixerOpen@20,@112")
#pragma comment(linker, "/EXPORT:mixerSetControlDetails=_HookFont_mixerSetControlDetails@12,@113")
#pragma comment(linker, "/EXPORT:mmDrvInstall=_HookFont_mmDrvInstall,@114")
#pragma comment(linker, "/EXPORT:mmGetCurrentTask=_HookFont_mmGetCurrentTask,@115")
#pragma comment(linker, "/EXPORT:mmTaskBlock=_HookFont_mmTaskBlock,@116")
#pragma comment(linker, "/EXPORT:mmTaskCreate=_HookFont_mmTaskCreate,@117")
#pragma comment(linker, "/EXPORT:mmTaskSignal=_HookFont_mmTaskSignal,@118")
#pragma comment(linker, "/EXPORT:mmTaskYield=_HookFont_mmTaskYield,@119")
#pragma comment(linker, "/EXPORT:mmioAdvance=_HookFont_mmioAdvance,@120")
#pragma comment(linker, "/EXPORT:mmioAscend=_HookFont_mmioAscend,@121")
#pragma comment(linker, "/EXPORT:mmioClose=_HookFont_mmioClose,@122")
#pragma comment(linker, "/EXPORT:mmioCreateChunk=_HookFont_mmioCreateChunk,@123")
#pragma comment(linker, "/EXPORT:mmioDescend=_HookFont_mmioDescend,@124")
#pragma comment(linker, "/EXPORT:mmioFlush=_HookFont_mmioFlush,@125")
#pragma comment(linker, "/EXPORT:mmioGetInfo=_HookFont_mmioGetInfo,@126")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcA=_HookFont_mmioInstallIOProcA,@127")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcW=_HookFont_mmioInstallIOProcW,@128")
#pragma comment(linker, "/EXPORT:mmioOpenA=_HookFont_mmioOpenA,@129")
#pragma comment(linker, "/EXPORT:mmioOpenW=_HookFont_mmioOpenW,@130")
#pragma comment(linker, "/EXPORT:mmioRead=_HookFont_mmioRead,@131")
#pragma comment(linker, "/EXPORT:mmioRenameA=_HookFont_mmioRenameA,@132")
#pragma comment(linker, "/EXPORT:mmioRenameW=_HookFont_mmioRenameW,@133")
#pragma comment(linker, "/EXPORT:mmioSeek=_HookFont_mmioSeek,@134")
#pragma comment(linker, "/EXPORT:mmioSendMessage=_HookFont_mmioSendMessage,@135")
#pragma comment(linker, "/EXPORT:mmioSetBuffer=_HookFont_mmioSetBuffer,@136")
#pragma comment(linker, "/EXPORT:mmioSetInfo=_HookFont_mmioSetInfo,@137")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCA=_HookFont_mmioStringToFOURCCA,@138")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCW=_HookFont_mmioStringToFOURCCW,@139")
#pragma comment(linker, "/EXPORT:mmioWrite=_HookFont_mmioWrite,@140")
#pragma comment(linker, "/EXPORT:mmsystemGetVersion=_HookFont_mmsystemGetVersion,@141")
#pragma comment(linker, "/EXPORT:mod32Message=_HookFont_mod32Message,@142")
#pragma comment(linker, "/EXPORT:mxd32Message=_HookFont_mxd32Message,@143")
#pragma comment(linker, "/EXPORT:sndPlaySoundA=_HookFont_sndPlaySoundA,@144")
#pragma comment(linker, "/EXPORT:sndPlaySoundW=_HookFont_sndPlaySoundW,@145")
#pragma comment(linker, "/EXPORT:tid32Message=_HookFont_tid32Message,@146")
#pragma comment(linker, "/EXPORT:timeBeginPeriod=_HookFont_timeBeginPeriod@4,@147")
#pragma comment(linker, "/EXPORT:timeEndPeriod=_HookFont_timeEndPeriod@4,@148")
#pragma comment(linker, "/EXPORT:timeGetDevCaps=_HookFont_timeGetDevCaps@8,@149")
#pragma comment(linker, "/EXPORT:timeGetSystemTime=_HookFont_timeGetSystemTime@8,@150")
#pragma comment(linker, "/EXPORT:timeGetTime=_HookFont_timeGetTime@0,@151")
#pragma comment(linker, "/EXPORT:timeKillEvent=_HookFont_timeKillEvent@4,@152")
#pragma comment(linker, "/EXPORT:timeSetEvent=_HookFont_timeSetEvent@20,@153")
#pragma comment(linker, "/EXPORT:waveInAddBuffer=_HookFont_waveInAddBuffer@12,@154")
#pragma comment(linker, "/EXPORT:waveInClose=_HookFont_waveInClose@4,@155")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsA=_HookFont_waveInGetDevCapsA,@156")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsW=_HookFont_waveInGetDevCapsW,@157")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextA=_HookFont_waveInGetErrorTextA,@158")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextW=_HookFont_waveInGetErrorTextW,@159")
#pragma comment(linker, "/EXPORT:waveInGetID=_HookFont_waveInGetID,@160")
#pragma comment(linker, "/EXPORT:waveInGetNumDevs=_HookFont_waveInGetNumDevs@0,@161")
#pragma comment(linker, "/EXPORT:waveInGetPosition=_HookFont_waveInGetPosition@12,@162")
#pragma comment(linker, "/EXPORT:waveInMessage=_HookFont_waveInMessage@16,@163")
#pragma comment(linker, "/EXPORT:waveInOpen=_HookFont_waveInOpen@24,@164")
#pragma comment(linker, "/EXPORT:waveInPrepareHeader=_HookFont_waveInPrepareHeader@12,@165")
#pragma comment(linker, "/EXPORT:waveInReset=_HookFont_waveInReset@4,@166")
#pragma comment(linker, "/EXPORT:waveInStart=_HookFont_waveInStart@4,@167")
#pragma comment(linker, "/EXPORT:waveInStop=_HookFont_waveInStop@4,@168")
#pragma comment(linker, "/EXPORT:waveInUnprepareHeader=_HookFont_waveInUnprepareHeader@12,@169")
#pragma comment(linker, "/EXPORT:waveOutBreakLoop=_HookFont_waveOutBreakLoop@4,@170")
#pragma comment(linker, "/EXPORT:waveOutClose=_HookFont_waveOutClose@4,@171")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsA=_HookFont_waveOutGetDevCapsA,@172")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsW=_HookFont_waveOutGetDevCapsW,@173")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextA=_HookFont_waveOutGetErrorTextA,@174")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextW=_HookFont_waveOutGetErrorTextW,@175")
#pragma comment(linker, "/EXPORT:waveOutGetID=_HookFont_waveOutGetID,@176")
#pragma comment(linker, "/EXPORT:waveOutGetNumDevs=_HookFont_waveOutGetNumDevs@0,@177")
#pragma comment(linker, "/EXPORT:waveOutGetPitch=_HookFont_waveOutGetPitch,@178")
#pragma comment(linker, "/EXPORT:waveOutGetPlaybackRate=_HookFont_waveOutGetPlaybackRate,@179")
#pragma comment(linker, "/EXPORT:waveOutGetPosition=_HookFont_waveOutGetPosition@12,@180")
#pragma comment(linker, "/EXPORT:waveOutGetVolume=_HookFont_waveOutGetVolume@8,@181")
#pragma comment(linker, "/EXPORT:waveOutMessage=_HookFont_waveOutMessage@16,@182")
#pragma comment(linker, "/EXPORT:waveOutOpen=_HookFont_waveOutOpen@24,@183")
#pragma comment(linker, "/EXPORT:waveOutPause=_HookFont_waveOutPause@4,@184")
#pragma comment(linker, "/EXPORT:waveOutPrepareHeader=_HookFont_waveOutPrepareHeader@12,@185")
#pragma comment(linker, "/EXPORT:waveOutReset=_HookFont_waveOutReset@4,@186")
#pragma comment(linker, "/EXPORT:waveOutRestart=_HookFont_waveOutRestart@4,@187")
#pragma comment(linker, "/EXPORT:waveOutSetPitch=_HookFont_waveOutSetPitch,@188")
#pragma comment(linker, "/EXPORT:waveOutSetPlaybackRate=_HookFont_waveOutSetPlaybackRate,@189")
#pragma comment(linker, "/EXPORT:waveOutSetVolume=_HookFont_waveOutSetVolume@8,@190")
#pragma comment(linker, "/EXPORT:waveOutUnprepareHeader=_HookFont_waveOutUnprepareHeader@12,@191")
#pragma comment(linker, "/EXPORT:waveOutWrite=_HookFont_waveOutWrite@12,@192")
#pragma comment(linker, "/EXPORT:wid32Message=_HookFont_wid32Message,@193")
#pragma comment(linker, "/EXPORT:wod32Message=_HookFont_wod32Message,@194")
#endif
