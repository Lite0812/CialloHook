#include <Windows.h>
#include <mmsystem.h>
#include "../config/build_options.h"

#if defined(_MSC_VER) && defined(_WIN32) && CIALLOHOOK_FEATURE_CODECRYPT_PATCH
#define CIALLOHOOK_WINMM_PROTECTED_BEGIN __pragma(code_seg(push, ".lpksc$m"))
#define CIALLOHOOK_WINMM_PROTECTED_END __pragma(code_seg(pop))
#else
#define CIALLOHOOK_WINMM_PROTECTED_BEGIN
#define CIALLOHOOK_WINMM_PROTECTED_END
#endif

extern "C" __declspec(dllexport) int CialloWinMMExportAnchor = 1;

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

#define BASE_WINMM_EXPORTS(X) \
	X(mciGetErrorStringA) \
	X(mciGetErrorStringW) \
	X(mciSendCommandA) \
	X(mciSendCommandW) \
	X(mciSendStringA) \
	X(mciSendStringW) \
	X(midiOutClose) \
	X(midiOutGetNumDevs) \
	X(midiOutLongMsg) \
	X(midiOutMessage) \
	X(midiOutOpen) \
	X(midiOutPrepareHeader) \
	X(midiOutReset) \
	X(midiOutSetVolume) \
	X(midiOutShortMsg) \
	X(midiOutUnprepareHeader) \
	X(midiInAddBuffer) \
	X(midiInClose) \
	X(midiInGetNumDevs) \
	X(midiInMessage) \
	X(midiInOpen) \
	X(midiInPrepareHeader) \
	X(midiInReset) \
	X(midiInStart) \
	X(midiInStop) \
	X(midiInUnprepareHeader) \
	X(mixerClose) \
	X(mixerGetControlDetailsA) \
	X(mixerGetControlDetailsW) \
	X(mixerGetDevCapsA) \
	X(mixerGetDevCapsW) \
	X(mixerGetLineControlsA) \
	X(mixerGetLineControlsW) \
	X(mixerGetLineInfoA) \
	X(mixerGetLineInfoW) \
	X(mixerGetNumDevs) \
	X(mixerMessage) \
	X(mixerOpen) \
	X(mixerSetControlDetails) \
	X(timeBeginPeriod) \
	X(timeEndPeriod) \
	X(timeGetDevCaps) \
	X(timeGetSystemTime) \
	X(timeGetTime) \
	X(timeKillEvent) \
	X(timeSetEvent) \
	X(waveInAddBuffer) \
	X(waveInClose) \
	X(waveInGetNumDevs) \
	X(waveInGetPosition) \
	X(waveInMessage) \
	X(waveInOpen) \
	X(waveInPrepareHeader) \
	X(waveInReset) \
	X(waveInStart) \
	X(waveInStop) \
	X(waveInUnprepareHeader) \
	X(waveOutBreakLoop) \
	X(waveOutClose) \
	X(waveOutGetNumDevs) \
	X(waveOutGetPosition) \
	X(waveOutGetVolume) \
	X(waveOutMessage) \
	X(waveOutOpen) \
	X(waveOutPause) \
	X(waveOutPrepareHeader) \
	X(waveOutReset) \
	X(waveOutRestart) \
	X(waveOutSetVolume) \
	X(waveOutUnprepareHeader) \
	X(waveOutWrite)

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
#define EXPORT_FORWARD_WINMM(fn) __pragma(comment(linker, "/EXPORT:" #fn "=winmm." #fn))
FORWARDED_WINMM_EXPORTS(EXPORT_FORWARD_WINMM)
BASE_WINMM_EXPORTS(EXPORT_FORWARD_WINMM)
#undef EXPORT_FORWARD_WINMM
#endif

#ifndef _WIN64
#pragma comment(linker, "/EXPORT:Noname2=_CialloWinMM_Ordinal2,@2,NONAME")
#pragma comment(linker, "/EXPORT:mciExecute=_CialloWinMM_mciExecute,@3")
#pragma comment(linker, "/EXPORT:CloseDriver=_CialloWinMM_CloseDriver,@4")
#pragma comment(linker, "/EXPORT:DefDriverProc=_CialloWinMM_DefDriverProc,@5")
#pragma comment(linker, "/EXPORT:DriverCallback=_CialloWinMM_DriverCallback,@6")
#pragma comment(linker, "/EXPORT:DrvGetModuleHandle=_CialloWinMM_DrvGetModuleHandle,@7")
#pragma comment(linker, "/EXPORT:GetDriverModuleHandle=_CialloWinMM_GetDriverModuleHandle,@8")
#pragma comment(linker, "/EXPORT:NotifyCallbackData=_CialloWinMM_NotifyCallbackData,@9")
#pragma comment(linker, "/EXPORT:OpenDriver=_CialloWinMM_OpenDriver,@10")
#pragma comment(linker, "/EXPORT:PlaySound=_CialloWinMM_PlaySound,@11")
#pragma comment(linker, "/EXPORT:PlaySoundA=_CialloWinMM_PlaySoundA,@12")
#pragma comment(linker, "/EXPORT:PlaySoundW=_CialloWinMM_PlaySoundW,@13")
#pragma comment(linker, "/EXPORT:SendDriverMessage=_CialloWinMM_SendDriverMessage,@14")
#pragma comment(linker, "/EXPORT:WOW32DriverCallback=_CialloWinMM_WOW32DriverCallback,@15")
#pragma comment(linker, "/EXPORT:WOW32ResolveMultiMediaHandle=_CialloWinMM_WOW32ResolveMultiMediaHandle,@16")
#pragma comment(linker, "/EXPORT:WOWAppExit=_CialloWinMM_WOWAppExit,@17")
#pragma comment(linker, "/EXPORT:aux32Message=_CialloWinMM_aux32Message,@18")
#pragma comment(linker, "/EXPORT:auxGetDevCapsA=_CialloWinMM_auxGetDevCapsA,@19")
#pragma comment(linker, "/EXPORT:auxGetDevCapsW=_CialloWinMM_auxGetDevCapsW,@20")
#pragma comment(linker, "/EXPORT:auxGetNumDevs=_CialloWinMM_auxGetNumDevs,@21")
#pragma comment(linker, "/EXPORT:auxGetVolume=_CialloWinMM_auxGetVolume,@22")
#pragma comment(linker, "/EXPORT:auxOutMessage=_CialloWinMM_auxOutMessage,@23")
#pragma comment(linker, "/EXPORT:auxSetVolume=_CialloWinMM_auxSetVolume,@24")
#pragma comment(linker, "/EXPORT:joy32Message=_CialloWinMM_joy32Message,@25")
#pragma comment(linker, "/EXPORT:joyConfigChanged=_CialloWinMM_joyConfigChanged,@26")
#pragma comment(linker, "/EXPORT:joyGetDevCapsA=_CialloWinMM_joyGetDevCapsA,@27")
#pragma comment(linker, "/EXPORT:joyGetDevCapsW=_CialloWinMM_joyGetDevCapsW,@28")
#pragma comment(linker, "/EXPORT:joyGetNumDevs=_CialloWinMM_joyGetNumDevs,@29")
#pragma comment(linker, "/EXPORT:joyGetPos=_CialloWinMM_joyGetPos,@30")
#pragma comment(linker, "/EXPORT:joyGetPosEx=_CialloWinMM_joyGetPosEx,@31")
#pragma comment(linker, "/EXPORT:joyGetThreshold=_CialloWinMM_joyGetThreshold,@32")
#pragma comment(linker, "/EXPORT:joyReleaseCapture=_CialloWinMM_joyReleaseCapture,@33")
#pragma comment(linker, "/EXPORT:joySetCapture=_CialloWinMM_joySetCapture,@34")
#pragma comment(linker, "/EXPORT:joySetThreshold=_CialloWinMM_joySetThreshold,@35")
#pragma comment(linker, "/EXPORT:mci32Message=_CialloWinMM_mci32Message,@36")
#pragma comment(linker, "/EXPORT:mciDriverNotify=_CialloWinMM_mciDriverNotify,@37")
#pragma comment(linker, "/EXPORT:mciDriverYield=_CialloWinMM_mciDriverYield,@38")
#pragma comment(linker, "/EXPORT:mciFreeCommandResource=_CialloWinMM_mciFreeCommandResource,@39")
#pragma comment(linker, "/EXPORT:mciGetCreatorTask=_CialloWinMM_mciGetCreatorTask,@40")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDA=_CialloWinMM_mciGetDeviceIDA,@41")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDA=_CialloWinMM_mciGetDeviceIDFromElementIDA,@42")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDW=_CialloWinMM_mciGetDeviceIDFromElementIDW,@43")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDW=_CialloWinMM_mciGetDeviceIDW,@44")
#pragma comment(linker, "/EXPORT:mciGetDriverData=_CialloWinMM_mciGetDriverData,@45")
#pragma comment(linker, "/EXPORT:mciGetErrorStringA=_CialloWinMM_mciGetErrorStringA@12,@46")
#pragma comment(linker, "/EXPORT:mciGetErrorStringW=_CialloWinMM_mciGetErrorStringW@12,@47")
#pragma comment(linker, "/EXPORT:mciGetYieldProc=_CialloWinMM_mciGetYieldProc,@48")
#pragma comment(linker, "/EXPORT:mciLoadCommandResource=_CialloWinMM_mciLoadCommandResource,@49")
#pragma comment(linker, "/EXPORT:mciSendCommandA=_CialloWinMM_mciSendCommandA@16,@50")
#pragma comment(linker, "/EXPORT:mciSendCommandW=_CialloWinMM_mciSendCommandW@16,@51")
#pragma comment(linker, "/EXPORT:mciSendStringA=_CialloWinMM_mciSendStringA@16,@52")
#pragma comment(linker, "/EXPORT:mciSendStringW=_CialloWinMM_mciSendStringW@16,@53")
#pragma comment(linker, "/EXPORT:mciSetDriverData=_CialloWinMM_mciSetDriverData,@54")
#pragma comment(linker, "/EXPORT:mciSetYieldProc=_CialloWinMM_mciSetYieldProc,@55")
#pragma comment(linker, "/EXPORT:mid32Message=_CialloWinMM_mid32Message,@56")
#pragma comment(linker, "/EXPORT:midiConnect=_CialloWinMM_midiConnect,@57")
#pragma comment(linker, "/EXPORT:midiDisconnect=_CialloWinMM_midiDisconnect,@58")
#pragma comment(linker, "/EXPORT:midiInAddBuffer=_CialloWinMM_midiInAddBuffer@12,@59")
#pragma comment(linker, "/EXPORT:midiInClose=_CialloWinMM_midiInClose@4,@60")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsA=_CialloWinMM_midiInGetDevCapsA,@61")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsW=_CialloWinMM_midiInGetDevCapsW,@62")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextA=_CialloWinMM_midiInGetErrorTextA,@63")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextW=_CialloWinMM_midiInGetErrorTextW,@64")
#pragma comment(linker, "/EXPORT:midiInGetID=_CialloWinMM_midiInGetID,@65")
#pragma comment(linker, "/EXPORT:midiInGetNumDevs=_CialloWinMM_midiInGetNumDevs@0,@66")
#pragma comment(linker, "/EXPORT:midiInMessage=_CialloWinMM_midiInMessage@16,@67")
#pragma comment(linker, "/EXPORT:midiInOpen=_CialloWinMM_midiInOpen@20,@68")
#pragma comment(linker, "/EXPORT:midiInPrepareHeader=_CialloWinMM_midiInPrepareHeader@12,@69")
#pragma comment(linker, "/EXPORT:midiInReset=_CialloWinMM_midiInReset@4,@70")
#pragma comment(linker, "/EXPORT:midiInStart=_CialloWinMM_midiInStart@4,@71")
#pragma comment(linker, "/EXPORT:midiInStop=_CialloWinMM_midiInStop@4,@72")
#pragma comment(linker, "/EXPORT:midiInUnprepareHeader=_CialloWinMM_midiInUnprepareHeader@12,@73")
#pragma comment(linker, "/EXPORT:midiOutCacheDrumPatches=_CialloWinMM_midiOutCacheDrumPatches,@74")
#pragma comment(linker, "/EXPORT:midiOutCachePatches=_CialloWinMM_midiOutCachePatches,@75")
#pragma comment(linker, "/EXPORT:midiOutClose=_CialloWinMM_midiOutClose@4,@76")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsA=_CialloWinMM_midiOutGetDevCapsA,@77")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsW=_CialloWinMM_midiOutGetDevCapsW,@78")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextA=_CialloWinMM_midiOutGetErrorTextA,@79")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextW=_CialloWinMM_midiOutGetErrorTextW,@80")
#pragma comment(linker, "/EXPORT:midiOutGetID=_CialloWinMM_midiOutGetID,@81")
#pragma comment(linker, "/EXPORT:midiOutGetNumDevs=_CialloWinMM_midiOutGetNumDevs@0,@82")
#pragma comment(linker, "/EXPORT:midiOutGetVolume=_CialloWinMM_midiOutGetVolume,@83")
#pragma comment(linker, "/EXPORT:midiOutLongMsg=_CialloWinMM_midiOutLongMsg@12,@84")
#pragma comment(linker, "/EXPORT:midiOutMessage=_CialloWinMM_midiOutMessage@16,@85")
#pragma comment(linker, "/EXPORT:midiOutOpen=_CialloWinMM_midiOutOpen@20,@86")
#pragma comment(linker, "/EXPORT:midiOutPrepareHeader=_CialloWinMM_midiOutPrepareHeader@12,@87")
#pragma comment(linker, "/EXPORT:midiOutReset=_CialloWinMM_midiOutReset@4,@88")
#pragma comment(linker, "/EXPORT:midiOutSetVolume=_CialloWinMM_midiOutSetVolume@8,@89")
#pragma comment(linker, "/EXPORT:midiOutShortMsg=_CialloWinMM_midiOutShortMsg@8,@90")
#pragma comment(linker, "/EXPORT:midiOutUnprepareHeader=_CialloWinMM_midiOutUnprepareHeader@12,@91")
#pragma comment(linker, "/EXPORT:midiStreamClose=_CialloWinMM_midiStreamClose,@92")
#pragma comment(linker, "/EXPORT:midiStreamOpen=_CialloWinMM_midiStreamOpen,@93")
#pragma comment(linker, "/EXPORT:midiStreamOut=_CialloWinMM_midiStreamOut,@94")
#pragma comment(linker, "/EXPORT:midiStreamPause=_CialloWinMM_midiStreamPause,@95")
#pragma comment(linker, "/EXPORT:midiStreamPosition=_CialloWinMM_midiStreamPosition,@96")
#pragma comment(linker, "/EXPORT:midiStreamProperty=_CialloWinMM_midiStreamProperty,@97")
#pragma comment(linker, "/EXPORT:midiStreamRestart=_CialloWinMM_midiStreamRestart,@98")
#pragma comment(linker, "/EXPORT:midiStreamStop=_CialloWinMM_midiStreamStop,@99")
#pragma comment(linker, "/EXPORT:mixerClose=_CialloWinMM_mixerClose@4,@100")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsA=_CialloWinMM_mixerGetControlDetailsA@12,@101")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsW=_CialloWinMM_mixerGetControlDetailsW@12,@102")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsA=_CialloWinMM_mixerGetDevCapsA@12,@103")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsW=_CialloWinMM_mixerGetDevCapsW@12,@104")
#pragma comment(linker, "/EXPORT:mixerGetID=_CialloWinMM_mixerGetID,@105")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsA=_CialloWinMM_mixerGetLineControlsA@12,@106")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsW=_CialloWinMM_mixerGetLineControlsW@12,@107")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoA=_CialloWinMM_mixerGetLineInfoA@12,@108")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoW=_CialloWinMM_mixerGetLineInfoW@12,@109")
#pragma comment(linker, "/EXPORT:mixerGetNumDevs=_CialloWinMM_mixerGetNumDevs@0,@110")
#pragma comment(linker, "/EXPORT:mixerMessage=_CialloWinMM_mixerMessage@16,@111")
#pragma comment(linker, "/EXPORT:mixerOpen=_CialloWinMM_mixerOpen@20,@112")
#pragma comment(linker, "/EXPORT:mixerSetControlDetails=_CialloWinMM_mixerSetControlDetails@12,@113")
#pragma comment(linker, "/EXPORT:mmDrvInstall=_CialloWinMM_mmDrvInstall,@114")
#pragma comment(linker, "/EXPORT:mmGetCurrentTask=_CialloWinMM_mmGetCurrentTask,@115")
#pragma comment(linker, "/EXPORT:mmTaskBlock=_CialloWinMM_mmTaskBlock,@116")
#pragma comment(linker, "/EXPORT:mmTaskCreate=_CialloWinMM_mmTaskCreate,@117")
#pragma comment(linker, "/EXPORT:mmTaskSignal=_CialloWinMM_mmTaskSignal,@118")
#pragma comment(linker, "/EXPORT:mmTaskYield=_CialloWinMM_mmTaskYield,@119")
#pragma comment(linker, "/EXPORT:mmioAdvance=_CialloWinMM_mmioAdvance,@120")
#pragma comment(linker, "/EXPORT:mmioAscend=_CialloWinMM_mmioAscend,@121")
#pragma comment(linker, "/EXPORT:mmioClose=_CialloWinMM_mmioClose,@122")
#pragma comment(linker, "/EXPORT:mmioCreateChunk=_CialloWinMM_mmioCreateChunk,@123")
#pragma comment(linker, "/EXPORT:mmioDescend=_CialloWinMM_mmioDescend,@124")
#pragma comment(linker, "/EXPORT:mmioFlush=_CialloWinMM_mmioFlush,@125")
#pragma comment(linker, "/EXPORT:mmioGetInfo=_CialloWinMM_mmioGetInfo,@126")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcA=_CialloWinMM_mmioInstallIOProcA,@127")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcW=_CialloWinMM_mmioInstallIOProcW,@128")
#pragma comment(linker, "/EXPORT:mmioOpenA=_CialloWinMM_mmioOpenA,@129")
#pragma comment(linker, "/EXPORT:mmioOpenW=_CialloWinMM_mmioOpenW,@130")
#pragma comment(linker, "/EXPORT:mmioRead=_CialloWinMM_mmioRead,@131")
#pragma comment(linker, "/EXPORT:mmioRenameA=_CialloWinMM_mmioRenameA,@132")
#pragma comment(linker, "/EXPORT:mmioRenameW=_CialloWinMM_mmioRenameW,@133")
#pragma comment(linker, "/EXPORT:mmioSeek=_CialloWinMM_mmioSeek,@134")
#pragma comment(linker, "/EXPORT:mmioSendMessage=_CialloWinMM_mmioSendMessage,@135")
#pragma comment(linker, "/EXPORT:mmioSetBuffer=_CialloWinMM_mmioSetBuffer,@136")
#pragma comment(linker, "/EXPORT:mmioSetInfo=_CialloWinMM_mmioSetInfo,@137")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCA=_CialloWinMM_mmioStringToFOURCCA,@138")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCW=_CialloWinMM_mmioStringToFOURCCW,@139")
#pragma comment(linker, "/EXPORT:mmioWrite=_CialloWinMM_mmioWrite,@140")
#pragma comment(linker, "/EXPORT:mmsystemGetVersion=_CialloWinMM_mmsystemGetVersion,@141")
#pragma comment(linker, "/EXPORT:mod32Message=_CialloWinMM_mod32Message,@142")
#pragma comment(linker, "/EXPORT:mxd32Message=_CialloWinMM_mxd32Message,@143")
#pragma comment(linker, "/EXPORT:sndPlaySoundA=_CialloWinMM_sndPlaySoundA,@144")
#pragma comment(linker, "/EXPORT:sndPlaySoundW=_CialloWinMM_sndPlaySoundW,@145")
#pragma comment(linker, "/EXPORT:tid32Message=_CialloWinMM_tid32Message,@146")
#pragma comment(linker, "/EXPORT:timeBeginPeriod=_CialloWinMM_timeBeginPeriod@4,@147")
#pragma comment(linker, "/EXPORT:timeEndPeriod=_CialloWinMM_timeEndPeriod@4,@148")
#pragma comment(linker, "/EXPORT:timeGetDevCaps=_CialloWinMM_timeGetDevCaps@8,@149")
#pragma comment(linker, "/EXPORT:timeGetSystemTime=_CialloWinMM_timeGetSystemTime@8,@150")
#pragma comment(linker, "/EXPORT:timeGetTime=_CialloWinMM_timeGetTime@0,@151")
#pragma comment(linker, "/EXPORT:timeKillEvent=_CialloWinMM_timeKillEvent@4,@152")
#pragma comment(linker, "/EXPORT:timeSetEvent=_CialloWinMM_timeSetEvent@20,@153")
#pragma comment(linker, "/EXPORT:waveInAddBuffer=_CialloWinMM_waveInAddBuffer@12,@154")
#pragma comment(linker, "/EXPORT:waveInClose=_CialloWinMM_waveInClose@4,@155")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsA=_CialloWinMM_waveInGetDevCapsA,@156")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsW=_CialloWinMM_waveInGetDevCapsW,@157")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextA=_CialloWinMM_waveInGetErrorTextA,@158")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextW=_CialloWinMM_waveInGetErrorTextW,@159")
#pragma comment(linker, "/EXPORT:waveInGetID=_CialloWinMM_waveInGetID,@160")
#pragma comment(linker, "/EXPORT:waveInGetNumDevs=_CialloWinMM_waveInGetNumDevs@0,@161")
#pragma comment(linker, "/EXPORT:waveInGetPosition=_CialloWinMM_waveInGetPosition@12,@162")
#pragma comment(linker, "/EXPORT:waveInMessage=_CialloWinMM_waveInMessage@16,@163")
#pragma comment(linker, "/EXPORT:waveInOpen=_CialloWinMM_waveInOpen@24,@164")
#pragma comment(linker, "/EXPORT:waveInPrepareHeader=_CialloWinMM_waveInPrepareHeader@12,@165")
#pragma comment(linker, "/EXPORT:waveInReset=_CialloWinMM_waveInReset@4,@166")
#pragma comment(linker, "/EXPORT:waveInStart=_CialloWinMM_waveInStart@4,@167")
#pragma comment(linker, "/EXPORT:waveInStop=_CialloWinMM_waveInStop@4,@168")
#pragma comment(linker, "/EXPORT:waveInUnprepareHeader=_CialloWinMM_waveInUnprepareHeader@12,@169")
#pragma comment(linker, "/EXPORT:waveOutBreakLoop=_CialloWinMM_waveOutBreakLoop@4,@170")
#pragma comment(linker, "/EXPORT:waveOutClose=_CialloWinMM_waveOutClose@4,@171")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsA=_CialloWinMM_waveOutGetDevCapsA,@172")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsW=_CialloWinMM_waveOutGetDevCapsW,@173")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextA=_CialloWinMM_waveOutGetErrorTextA,@174")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextW=_CialloWinMM_waveOutGetErrorTextW,@175")
#pragma comment(linker, "/EXPORT:waveOutGetID=_CialloWinMM_waveOutGetID,@176")
#pragma comment(linker, "/EXPORT:waveOutGetNumDevs=_CialloWinMM_waveOutGetNumDevs@0,@177")
#pragma comment(linker, "/EXPORT:waveOutGetPitch=_CialloWinMM_waveOutGetPitch,@178")
#pragma comment(linker, "/EXPORT:waveOutGetPlaybackRate=_CialloWinMM_waveOutGetPlaybackRate,@179")
#pragma comment(linker, "/EXPORT:waveOutGetPosition=_CialloWinMM_waveOutGetPosition@12,@180")
#pragma comment(linker, "/EXPORT:waveOutGetVolume=_CialloWinMM_waveOutGetVolume@8,@181")
#pragma comment(linker, "/EXPORT:waveOutMessage=_CialloWinMM_waveOutMessage@16,@182")
#pragma comment(linker, "/EXPORT:waveOutOpen=_CialloWinMM_waveOutOpen@24,@183")
#pragma comment(linker, "/EXPORT:waveOutPause=_CialloWinMM_waveOutPause@4,@184")
#pragma comment(linker, "/EXPORT:waveOutPrepareHeader=_CialloWinMM_waveOutPrepareHeader@12,@185")
#pragma comment(linker, "/EXPORT:waveOutReset=_CialloWinMM_waveOutReset@4,@186")
#pragma comment(linker, "/EXPORT:waveOutRestart=_CialloWinMM_waveOutRestart@4,@187")
#pragma comment(linker, "/EXPORT:waveOutSetPitch=_CialloWinMM_waveOutSetPitch,@188")
#pragma comment(linker, "/EXPORT:waveOutSetPlaybackRate=_CialloWinMM_waveOutSetPlaybackRate,@189")
#pragma comment(linker, "/EXPORT:waveOutSetVolume=_CialloWinMM_waveOutSetVolume@8,@190")
#pragma comment(linker, "/EXPORT:waveOutUnprepareHeader=_CialloWinMM_waveOutUnprepareHeader@12,@191")
#pragma comment(linker, "/EXPORT:waveOutWrite=_CialloWinMM_waveOutWrite@12,@192")
#pragma comment(linker, "/EXPORT:wid32Message=_CialloWinMM_wid32Message,@193")
#pragma comment(linker, "/EXPORT:wod32Message=_CialloWinMM_wod32Message,@194")
#endif

