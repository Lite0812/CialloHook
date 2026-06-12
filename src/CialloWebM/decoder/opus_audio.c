#include "opus_audio.h"

#include <opus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define OPUS_OUTPUT_SAMPLE_RATE 48000
#define OPUS_MAX_CHANNELS 2
#define OPUS_MAX_FRAME_SAMPLES 5760

static void opus_set_error(char* error_msg, size_t error_msg_size, const char* message)
{
    if (!error_msg || error_msg_size == 0) return;
    strncpy(error_msg, message, error_msg_size - 1);
    error_msg[error_msg_size - 1] = '\0';
}

static void wav_write_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wav_write_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int opus_channels_from_private(const uint8_t* codec_private, size_t codec_private_len)
{
    if (codec_private && codec_private_len >= 10 && memcmp(codec_private, "OpusHead", 8) == 0)
    {
        int channels = codec_private[9];
        if (channels > 0 && channels <= OPUS_MAX_CHANNELS) return channels;
    }
    return 2;
}

static int append_pcm(int16_t** pcm_data, int* pcm_cap, int* pcm_total,
                      const int16_t* samples, int sample_count)
{
    int needed = *pcm_total + sample_count;
    while (needed > *pcm_cap)
    {
        int new_cap = (*pcm_cap > 0) ? (*pcm_cap * 2) : sample_count;
        if (new_cap < needed) new_cap = needed;
        int16_t* tmp = (int16_t*)realloc(*pcm_data, (size_t)new_cap * sizeof(int16_t));
        if (!tmp) return 0;
        *pcm_data = tmp;
        *pcm_cap = new_cap;
    }

    memcpy(*pcm_data + *pcm_total, samples, (size_t)sample_count * sizeof(int16_t));
    *pcm_total += sample_count;
    return 1;
}

int ciallo_extract_opus_wav(
    const uint8_t* webm_data,
    size_t webm_size,
    const WebmTrackInfo* audio_track,
    const wchar_t* wav_path,
    char* error_msg,
    size_t error_msg_size)
{
    if (!webm_data || webm_size == 0 || !audio_track || !wav_path)
    {
        opus_set_error(error_msg, error_msg_size, "Invalid Opus extraction parameter");
        return -6;
    }

    int channels = opus_channels_from_private(audio_track->codec_private, audio_track->codec_private_len);
    int opus_err = OPUS_OK;
    OpusDecoder* decoder = opus_decoder_create(OPUS_OUTPUT_SAMPLE_RATE, channels, &opus_err);
    if (!decoder || opus_err != OPUS_OK)
    {
        opus_set_error(error_msg, error_msg_size, "Failed to initialize Opus decoder");
        if (decoder) opus_decoder_destroy(decoder);
        return -4;
    }

    WebmDemuxer audio_demuxer;
    if (!webm_demuxer_init(&audio_demuxer, webm_data, webm_size))
    {
        opus_decoder_destroy(decoder);
        opus_set_error(error_msg, error_msg_size, "Failed to initialize audio demuxer");
        return -2;
    }

    int pcm_cap = OPUS_OUTPUT_SAMPLE_RATE * 30 * channels;
    int16_t* pcm_data = (int16_t*)malloc((size_t)pcm_cap * sizeof(int16_t));
    if (!pcm_data)
    {
        opus_decoder_destroy(decoder);
        return -7;
    }

    int pcm_total = 0;
    int16_t decode_buf[OPUS_MAX_FRAME_SAMPLES * OPUS_MAX_CHANNELS];
    WebmPacket pkt;

    while (webm_demuxer_read_next_packet(&audio_demuxer, &pkt))
    {
        if (pkt.track_number != audio_track->track_number) continue;
        if (!pkt.data || pkt.data_size == 0) continue;

        int frame_samples = opus_decode(decoder,
            pkt.data,
            (opus_int32)pkt.data_size,
            decode_buf,
            OPUS_MAX_FRAME_SAMPLES,
            0);
        if (frame_samples < 0)
        {
            continue;
        }

        if (!append_pcm(&pcm_data, &pcm_cap, &pcm_total, decode_buf, frame_samples * channels))
        {
            free(pcm_data);
            opus_decoder_destroy(decoder);
            return -7;
        }
    }

    opus_decoder_destroy(decoder);

    if (pcm_total == 0)
    {
        free(pcm_data);
        opus_set_error(error_msg, error_msg_size, "No Opus audio samples decoded");
        return -5;
    }

    uint32_t data_size = (uint32_t)(pcm_total * sizeof(int16_t));
    HANDLE hf = CreateFileW(wav_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hf == INVALID_HANDLE_VALUE)
    {
        free(pcm_data);
        return -1;
    }

    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);
    wav_write_u32(hdr + 4, 36 + data_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wav_write_u32(hdr + 16, 16);
    wav_write_u16(hdr + 20, 1);
    wav_write_u16(hdr + 22, (uint16_t)channels);
    wav_write_u32(hdr + 24, OPUS_OUTPUT_SAMPLE_RATE);
    wav_write_u32(hdr + 28, (uint32_t)(OPUS_OUTPUT_SAMPLE_RATE * channels * 2));
    wav_write_u16(hdr + 32, (uint16_t)(channels * 2));
    wav_write_u16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4);
    wav_write_u32(hdr + 40, data_size);

    DWORD written = 0;
    WriteFile(hf, hdr, sizeof(hdr), &written, NULL);
    WriteFile(hf, pcm_data, data_size, &written, NULL);
    CloseHandle(hf);
    free(pcm_data);

    return 0;
}
