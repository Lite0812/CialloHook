#ifndef CIALLO_OPUS_AUDIO_H
#define CIALLO_OPUS_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <Windows.h>
#include "webm_demuxer.h"

#ifdef __cplusplus
extern "C" {
#endif

int ciallo_extract_opus_wav(
    const uint8_t* webm_data,
    size_t webm_size,
    const WebmTrackInfo* audio_track,
    const wchar_t* wav_path,
    char* error_msg,
    size_t error_msg_size);

#ifdef __cplusplus
}
#endif

#endif /* CIALLO_OPUS_AUDIO_H */
