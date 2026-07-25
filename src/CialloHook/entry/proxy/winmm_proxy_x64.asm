option casemap:none

EXTERN g_cialloWinmmX64Targets:QWORD
EXTERN CialloHook_ResolveWinmmX64:PROC

.code

; All WinMM entry points use the same x64 calling convention.  Each small stub
; jumps straight to the cached system function.  On the first call the shared
; resolver preserves all possible register arguments, loads System32\winmm.dll,
; resolves the matching native ordinal, restores the arguments, and tail-jumps.
CialloWinMM64_ResolveAndJump PROC FRAME
	sub rsp, 0A8h
	.allocstack 0A8h
	.endprolog

	mov qword ptr [rsp + 20h], rcx
	mov qword ptr [rsp + 28h], rdx
	mov qword ptr [rsp + 30h], r8
	mov qword ptr [rsp + 38h], r9
	movdqu xmmword ptr [rsp + 40h], xmm0
	movdqu xmmword ptr [rsp + 50h], xmm1
	movdqu xmmword ptr [rsp + 60h], xmm2
	movdqu xmmword ptr [rsp + 70h], xmm3

	mov ecx, r10d
	call CialloHook_ResolveWinmmX64

	movdqu xmm0, xmmword ptr [rsp + 40h]
	movdqu xmm1, xmmword ptr [rsp + 50h]
	movdqu xmm2, xmmword ptr [rsp + 60h]
	movdqu xmm3, xmmword ptr [rsp + 70h]
	mov rcx, qword ptr [rsp + 20h]
	mov rdx, qword ptr [rsp + 28h]
	mov r8, qword ptr [rsp + 30h]
	mov r9, qword ptr [rsp + 38h]
	add rsp, 0A8h

	test rax, rax
	jz resolver_failed
	jmp rax

resolver_failed:
	ret
CialloWinMM64_ResolveAndJump ENDP

WINMM_STUB MACRO ordinalValue:req, functionName:req
PUBLIC CialloWinMM64_&functionName
CialloWinMM64_&functionName PROC
	mov rax, qword ptr [g_cialloWinmmX64Targets + (ordinalValue * 8)]
	test rax, rax
	jnz function_resolved
	mov r10d, ordinalValue
	jmp CialloWinMM64_ResolveAndJump
function_resolved:
	jmp rax
CialloWinMM64_&functionName ENDP
ENDM

WINMM_STUB 2, Ordinal2
WINMM_STUB 3, mciExecute
WINMM_STUB 4, CloseDriver
WINMM_STUB 5, DefDriverProc
WINMM_STUB 6, DriverCallback
WINMM_STUB 7, DrvGetModuleHandle
WINMM_STUB 8, GetDriverModuleHandle
WINMM_STUB 9, OpenDriver
WINMM_STUB 10, PlaySound
WINMM_STUB 11, PlaySoundA
WINMM_STUB 12, PlaySoundW
WINMM_STUB 13, SendDriverMessage
WINMM_STUB 14, WOWAppExit
WINMM_STUB 15, auxGetDevCapsA
WINMM_STUB 16, auxGetDevCapsW
WINMM_STUB 17, auxGetNumDevs
WINMM_STUB 18, auxGetVolume
WINMM_STUB 19, auxOutMessage
WINMM_STUB 20, auxSetVolume
WINMM_STUB 21, joyConfigChanged
WINMM_STUB 22, joyGetDevCapsA
WINMM_STUB 23, joyGetDevCapsW
WINMM_STUB 24, joyGetNumDevs
WINMM_STUB 25, joyGetPos
WINMM_STUB 26, joyGetPosEx
WINMM_STUB 27, joyGetThreshold
WINMM_STUB 28, joyReleaseCapture
WINMM_STUB 29, joySetCapture
WINMM_STUB 30, joySetThreshold
WINMM_STUB 31, mciDriverNotify
WINMM_STUB 32, mciDriverYield
WINMM_STUB 33, mciFreeCommandResource
WINMM_STUB 34, mciGetCreatorTask
WINMM_STUB 35, mciGetDeviceIDA
WINMM_STUB 36, mciGetDeviceIDFromElementIDA
WINMM_STUB 37, mciGetDeviceIDFromElementIDW
WINMM_STUB 38, mciGetDeviceIDW
WINMM_STUB 39, mciGetDriverData
WINMM_STUB 40, mciGetErrorStringA
WINMM_STUB 41, mciGetErrorStringW
WINMM_STUB 42, mciGetYieldProc
WINMM_STUB 43, mciLoadCommandResource
WINMM_STUB 44, mciSendCommandA
WINMM_STUB 45, mciSendCommandW
WINMM_STUB 46, mciSendStringA
WINMM_STUB 47, mciSendStringW
WINMM_STUB 48, mciSetDriverData
WINMM_STUB 49, mciSetYieldProc
WINMM_STUB 50, midiConnect
WINMM_STUB 51, midiDisconnect
WINMM_STUB 52, midiInAddBuffer
WINMM_STUB 53, midiInClose
WINMM_STUB 54, midiInGetDevCapsA
WINMM_STUB 55, midiInGetDevCapsW
WINMM_STUB 56, midiInGetErrorTextA
WINMM_STUB 57, midiInGetErrorTextW
WINMM_STUB 58, midiInGetID
WINMM_STUB 59, midiInGetNumDevs
WINMM_STUB 60, midiInMessage
WINMM_STUB 61, midiInOpen
WINMM_STUB 62, midiInPrepareHeader
WINMM_STUB 63, midiInReset
WINMM_STUB 64, midiInStart
WINMM_STUB 65, midiInStop
WINMM_STUB 66, midiInUnprepareHeader
WINMM_STUB 67, midiOutCacheDrumPatches
WINMM_STUB 68, midiOutCachePatches
WINMM_STUB 69, midiOutClose
WINMM_STUB 70, midiOutGetDevCapsA
WINMM_STUB 71, midiOutGetDevCapsW
WINMM_STUB 72, midiOutGetErrorTextA
WINMM_STUB 73, midiOutGetErrorTextW
WINMM_STUB 74, midiOutGetID
WINMM_STUB 75, midiOutGetNumDevs
WINMM_STUB 76, midiOutGetVolume
WINMM_STUB 77, midiOutLongMsg
WINMM_STUB 78, midiOutMessage
WINMM_STUB 79, midiOutOpen
WINMM_STUB 80, midiOutPrepareHeader
WINMM_STUB 81, midiOutReset
WINMM_STUB 82, midiOutSetVolume
WINMM_STUB 83, midiOutShortMsg
WINMM_STUB 84, midiOutUnprepareHeader
WINMM_STUB 85, midiStreamClose
WINMM_STUB 86, midiStreamOpen
WINMM_STUB 87, midiStreamOut
WINMM_STUB 88, midiStreamPause
WINMM_STUB 89, midiStreamPosition
WINMM_STUB 90, midiStreamProperty
WINMM_STUB 91, midiStreamRestart
WINMM_STUB 92, midiStreamStop
WINMM_STUB 93, mixerClose
WINMM_STUB 94, mixerGetControlDetailsA
WINMM_STUB 95, mixerGetControlDetailsW
WINMM_STUB 96, mixerGetDevCapsA
WINMM_STUB 97, mixerGetDevCapsW
WINMM_STUB 98, mixerGetID
WINMM_STUB 99, mixerGetLineControlsA
WINMM_STUB 100, mixerGetLineControlsW
WINMM_STUB 101, mixerGetLineInfoA
WINMM_STUB 102, mixerGetLineInfoW
WINMM_STUB 103, mixerGetNumDevs
WINMM_STUB 104, mixerMessage
WINMM_STUB 105, mixerOpen
WINMM_STUB 106, mixerSetControlDetails
WINMM_STUB 107, mmDrvInstall
WINMM_STUB 108, mmGetCurrentTask
WINMM_STUB 109, mmTaskBlock
WINMM_STUB 110, mmTaskCreate
WINMM_STUB 111, mmTaskSignal
WINMM_STUB 112, mmTaskYield
WINMM_STUB 113, mmioAdvance
WINMM_STUB 114, mmioAscend
WINMM_STUB 115, mmioClose
WINMM_STUB 116, mmioCreateChunk
WINMM_STUB 117, mmioDescend
WINMM_STUB 118, mmioFlush
WINMM_STUB 119, mmioGetInfo
WINMM_STUB 120, mmioInstallIOProcA
WINMM_STUB 121, mmioInstallIOProcW
WINMM_STUB 122, mmioOpenA
WINMM_STUB 123, mmioOpenW
WINMM_STUB 124, mmioRead
WINMM_STUB 125, mmioRenameA
WINMM_STUB 126, mmioRenameW
WINMM_STUB 127, mmioSeek
WINMM_STUB 128, mmioSendMessage
WINMM_STUB 129, mmioSetBuffer
WINMM_STUB 130, mmioSetInfo
WINMM_STUB 131, mmioStringToFOURCCA
WINMM_STUB 132, mmioStringToFOURCCW
WINMM_STUB 133, mmioWrite
WINMM_STUB 134, mmsystemGetVersion
WINMM_STUB 135, sndPlaySoundA
WINMM_STUB 136, sndPlaySoundW
WINMM_STUB 137, timeBeginPeriod
WINMM_STUB 138, timeEndPeriod
WINMM_STUB 139, timeGetDevCaps
WINMM_STUB 140, timeGetSystemTime
WINMM_STUB 141, timeGetTime
WINMM_STUB 142, timeKillEvent
WINMM_STUB 143, timeSetEvent
WINMM_STUB 144, waveInAddBuffer
WINMM_STUB 145, waveInClose
WINMM_STUB 146, waveInGetDevCapsA
WINMM_STUB 147, waveInGetDevCapsW
WINMM_STUB 148, waveInGetErrorTextA
WINMM_STUB 149, waveInGetErrorTextW
WINMM_STUB 150, waveInGetID
WINMM_STUB 151, waveInGetNumDevs
WINMM_STUB 152, waveInGetPosition
WINMM_STUB 153, waveInMessage
WINMM_STUB 154, waveInOpen
WINMM_STUB 155, waveInPrepareHeader
WINMM_STUB 156, waveInReset
WINMM_STUB 157, waveInStart
WINMM_STUB 158, waveInStop
WINMM_STUB 159, waveInUnprepareHeader
WINMM_STUB 160, waveOutBreakLoop
WINMM_STUB 161, waveOutClose
WINMM_STUB 162, waveOutGetDevCapsA
WINMM_STUB 163, waveOutGetDevCapsW
WINMM_STUB 164, waveOutGetErrorTextA
WINMM_STUB 165, waveOutGetErrorTextW
WINMM_STUB 166, waveOutGetID
WINMM_STUB 167, waveOutGetNumDevs
WINMM_STUB 168, waveOutGetPitch
WINMM_STUB 169, waveOutGetPlaybackRate
WINMM_STUB 170, waveOutGetPosition
WINMM_STUB 171, waveOutGetVolume
WINMM_STUB 172, waveOutMessage
WINMM_STUB 173, waveOutOpen
WINMM_STUB 174, waveOutPause
WINMM_STUB 175, waveOutPrepareHeader
WINMM_STUB 176, waveOutReset
WINMM_STUB 177, waveOutRestart
WINMM_STUB 178, waveOutSetPitch
WINMM_STUB 179, waveOutSetPlaybackRate
WINMM_STUB 180, waveOutSetVolume
WINMM_STUB 181, waveOutUnprepareHeader
WINMM_STUB 182, waveOutWrite

END
