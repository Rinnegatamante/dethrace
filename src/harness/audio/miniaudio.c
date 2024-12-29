// Disable miniaudio's 'null' device fallback. A proper device must be found to enable playback
#define MA_NO_NULL

#include "harness/audio.h"
#include "harness/config.h"
#include "harness/os.h"
#include "harness/trace.h"

// Must come before miniaudio.h
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

// Must come after miniaudio.h
#undef STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// duplicates DETHRACE/constants.h but is a necessary evil(?)
static int kMem_S3_DOS_SOS_channel = 234;

typedef struct tMiniaudio_sample {
    ma_audio_buffer_ref buffer_ref;
    ma_sound sound;
    int init_volume;
    int init_pan;
    int init_new_rate;
    int initialized;
} tMiniaudio_sample;

typedef struct tMiniaudio_stream {
    int frame_size_in_bytes;
    int sample_rate;
    int needs_converting;
    ma_paged_audio_buffer_data paged_audio_buffer_data;
    ma_paged_audio_buffer paged_audio_buffer;
    ma_data_converter data_converter;
    ma_sound sound;
} tMiniaudio_stream;

ma_engine engine;
ma_sound cda_sound;
int cda_sound_initialized;

#ifdef __vita__
#define NUM_SAMPLES (512)
#define NUM_CHANNELS (2)
#define SAMPLERATE (48000)
#ifdef USE_SDL2
#include <SDL2/SDL.h>
void data_callback(void* pUserData, ma_uint8* pBuffer, int bufferSizeInBytes) {
    float bufferF32[NUM_SAMPLES * NUM_CHANNELS];
    ma_uint32 bufferSizeInFrames = (ma_uint32)(bufferSizeInBytes * 2) / ma_get_bytes_per_frame(ma_format_f32, ma_engine_get_channels(&engine));
    ma_engine_read_pcm_frames(&engine, bufferF32, bufferSizeInFrames, NULL);
    int16_t *pBufferS16 = (int16_t *)pBuffer;
    for (int i = 0; i < bufferSizeInBytes / 2; i++) {
        pBufferS16[i] = (int16_t)(bufferF32[i] * 32767.0f - 1.0f);
    }
}
#else
#include <vitasdk.h>
static int vita_audio_thread(int args, void *argp) {
    float bufferF32[NUM_SAMPLES * NUM_CHANNELS];
    int16_t bufferS16[2][NUM_SAMPLES * NUM_CHANNELS];
    int chn = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, NUM_SAMPLES, 48000, NUM_CHANNELS == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
    sceAudioOutSetConfig(chn, -1, -1, -1);
    int vol[] = {32767, 32767};
    sceAudioOutSetVolume(chn, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    
    int buf_idx = 0;
    for (;;) {
        ma_uint32 bufferSizeInFrames = (ma_uint32)(NUM_SAMPLES * NUM_CHANNELS * 4) / ma_get_bytes_per_frame(ma_format_f32, ma_engine_get_channels(&engine));
        ma_engine_read_pcm_frames(&engine, bufferF32, bufferSizeInFrames, NULL);
        for (int i = 0; i < NUM_SAMPLES * NUM_CHANNELS; i++) {
            bufferS16[buf_idx][i] = (int16_t)(bufferF32[i] * 32767.0f - 1.0f);
        }
        sceAudioOutOutput(chn, bufferS16[buf_idx]);
        buf_idx = (buf_idx + 1) % 2;
    }
    sceAudioOutReleasePort(chn);
    return sceKernelExitDeleteThread(0);
}
#endif
#endif

tAudioBackend_error_code AudioBackend_Init(void) {
    ma_result result;
    ma_engine_config config;

    config = ma_engine_config_init();
#ifdef __vita__
    config.noDevice = MA_TRUE;
    config.channels = NUM_CHANNELS;
    config.sampleRate = SAMPLERATE;
#endif
    result = ma_engine_init(&config, &engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize audio engine. (%x)\n", result);
        return eAB_error;
    }
#ifdef __vita__
#ifdef USE_SDL2
    SDL_AudioSpec desiredSpec;
    SDL_AudioSpec obtainedSpec;
    SDL_AudioDeviceID deviceID;
    
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("Failed to initialize SDL sub-system.\n");
        return eAB_error;
    }

    MA_ZERO_OBJECT(&desiredSpec);
    desiredSpec.freq     = SAMPLERATE;
    desiredSpec.format   = AUDIO_S16;
    desiredSpec.channels = NUM_CHANNELS;
    desiredSpec.samples  = NUM_SAMPLES;
    desiredSpec.callback = data_callback;
    desiredSpec.userdata = NULL;

    deviceID = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &obtainedSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (deviceID == 0) {
        printf("Failed to open SDL audio device.\n");
        return eAB_error;
    }

    printf("Obtained: srate: %d chns: %d, samples: %d, format: %x\n", obtainedSpec.freq, obtainedSpec.channels, obtainedSpec.samples, obtainedSpec.format);
    SDL_PauseAudioDevice(deviceID, 0);
#else
    SceUID audiothread = sceKernelCreateThread("Audio Thread", (void*)&vita_audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(audiothread, 0, NULL);
#endif
#else
    LOG_INFO("Playback device: '%s'", engine.pDevice->playback.name);
#endif
    ma_engine_set_volume(&engine, harness_game_config.volume_multiplier);

    return eAB_success;
}

tAudioBackend_error_code AudioBackend_InitCDA(void) {
    // check if music files are present or not
    if (access("MUSIC/Track02.ogg", F_OK) == -1) {
        return eAB_error;
    }
    return eAB_success;
}

void AudioBackend_UnInit(void) {
    ma_engine_uninit(&engine);
}

void AudioBackend_UnInitCDA(void) {
}

tAudioBackend_error_code AudioBackend_StopCDA(void) {
    if (!cda_sound_initialized) {
        return eAB_success;
    }
    if (ma_sound_is_playing(&cda_sound)) {
        ma_sound_stop(&cda_sound);
    }
    ma_sound_uninit(&cda_sound);
    cda_sound_initialized = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    char path[256];
    ma_result result;

    sprintf(path, "MUSIC/Track0%d.ogg", track);

    if (access(path, F_OK) == -1) {
        return eAB_error;
    }

    // ensure we are not still playing a track
    AudioBackend_StopCDA();

    result = ma_sound_init_from_file(&engine, path, 0, NULL, NULL, &cda_sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }
    cda_sound_initialized = 1;
    result = ma_sound_start(&cda_sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }
    return eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    if (!cda_sound_initialized) {
        return 0;
    }
    return ma_sound_is_playing(&cda_sound);
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    if (!cda_sound_initialized) {
        return eAB_error;
    }
    ma_sound_set_volume(&cda_sound, volume / 255.0f);
    return eAB_success;
}

void* AudioBackend_AllocateSampleTypeStruct(void) {
    tMiniaudio_sample* sample_struct;
    sample_struct = BrMemAllocate(sizeof(tMiniaudio_sample), kMem_S3_DOS_SOS_channel);
    if (sample_struct == NULL) {
        return 0;
    }
    memset(sample_struct, 0, sizeof(tMiniaudio_sample));
    return sample_struct;
}

tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int loop) {
    tMiniaudio_sample* miniaudio;
    ma_result result;
    ma_int32 flags;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    result = ma_audio_buffer_ref_init(ma_format_u8, channels, data, size / channels, &miniaudio->buffer_ref);
    miniaudio->buffer_ref.sampleRate = rate;
    if (result != MA_SUCCESS) {
        return eAB_error;
    }

    flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_init_from_data_source(&engine, &miniaudio->buffer_ref, flags, NULL, &miniaudio->sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }
    miniaudio->initialized = 1;

    if (miniaudio->init_volume > 0) {
        AudioBackend_SetVolume(type_struct_sample, miniaudio->init_volume);
        AudioBackend_SetPan(type_struct_sample, miniaudio->init_pan);
        AudioBackend_SetFrequency(type_struct_sample, rate, miniaudio->init_new_rate);
    }

    ma_sound_set_looping(&miniaudio->sound, loop);
    ma_sound_start(&miniaudio->sound);
    return eAB_success;
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (ma_sound_is_playing(&miniaudio->sound)) {
        return 1;
    }
    return 0;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tMiniaudio_sample* miniaudio;
    float linear_volume;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_volume = volume;
        return eAB_success;
    }

    linear_volume = volume / 510.0f;
    ma_sound_set_volume(&miniaudio->sound, linear_volume);
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_pan = pan;
        return eAB_success;
    }

    // convert from directsound -10000 - 10000 pan scale
    ma_sound_set_pan(&miniaudio->sound, pan / 10000.0f);
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_new_rate = new_rate;
        return eAB_success;
    }

    // convert from directsound frequency to linear pitch scale
    ma_sound_set_pitch(&miniaudio->sound, (new_rate / (float)original_rate));
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (miniaudio->initialized) {
        ma_sound_stop(&miniaudio->sound);
        ma_sound_uninit(&miniaudio->sound);
        ma_audio_buffer_ref_uninit(&miniaudio->buffer_ref);
        miniaudio->initialized = 0;
    }
    return eAB_success;
}

tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tMiniaudio_stream* new;
    ma_data_converter_config data_converter_config;

    new = malloc(sizeof(tMiniaudio_stream));
    new->sample_rate = sample_rate;
    ma_format format;
    switch (bit_depth) {
    case 8:
        format = ma_format_u8;
        new->frame_size_in_bytes = 1 * channels;
        break;
    case 16:
        format = ma_format_s16;
        new->frame_size_in_bytes = 2 * channels;
        break;
    case 24:
        format = ma_format_s24;
        new->frame_size_in_bytes = 3 * channels;
        break;
    case 32:
        format = ma_format_s32;
        new->frame_size_in_bytes = 4 * channels;
        break;
    default:
        goto failed;
    }

    if ((new->frame_size_in_bytes == 0) || (ma_paged_audio_buffer_data_init(format, channels, &new->paged_audio_buffer_data) != MA_SUCCESS)) {
        LOG_WARN("Failed to create paged audio buffer data");
        goto failed;
    }

    ma_paged_audio_buffer_config paged_audio_buffer_config = ma_paged_audio_buffer_config_init(&new->paged_audio_buffer_data);
    if (ma_paged_audio_buffer_init(&paged_audio_buffer_config, &new->paged_audio_buffer) != MA_SUCCESS) {
        LOG_WARN("Failed to create paged audio buffer for smacker audio stream");
        goto failed;
    }

    if (ma_sound_init_from_data_source(&engine, &new->paged_audio_buffer, MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &new->sound) != MA_SUCCESS) {
        LOG_WARN("Failed to create sound from data source");
        goto failed;
    }

    // allocate and initialize data converter if miniaudio engine and Smack file soundtrack sample rates differ
    if (ma_engine_get_sample_rate(&engine) != sample_rate) {
        new->needs_converting = 1;
        data_converter_config = ma_data_converter_config_init(format, format, channels, channels, sample_rate, ma_engine_get_sample_rate(&engine));
        if (ma_data_converter_init(&data_converter_config, NULL, &new->data_converter) != MA_SUCCESS) {
            LOG_WARN("Failed to create sound data converter");
            goto failed;
        }
    }
    return new;

failed:
    free(new);
    return NULL;
}

tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size) {
    tMiniaudio_stream* stream = stream_handle;
    ma_uint64 nb_frames_in;
    ma_uint64 nb_frames_out;
    ma_uint64 current_pos;
    ma_paged_audio_buffer_page* new_page;

    if (ma_paged_audio_buffer_get_length_in_pcm_frames(&stream->paged_audio_buffer, &current_pos) != MA_SUCCESS) {
        LOG_WARN("ma_paged_audio_buffer_get_length_in_pcm_frames failed");
        return eAB_error;
    }

    // do we need to convert the sample frequency?
    if (stream->needs_converting) {
        nb_frames_in = size / stream->frame_size_in_bytes;
        nb_frames_out = nb_frames_in * ma_engine_get_sample_rate(&engine) / stream->sample_rate;

        if (ma_paged_audio_buffer_data_allocate_page(&stream->paged_audio_buffer_data, nb_frames_out, NULL, NULL, &new_page) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_allocate_page failed");
            return eAB_error;
        }
        if (ma_data_converter_process_pcm_frames(&stream->data_converter, data, &nb_frames_in, new_page->pAudioData, &nb_frames_out) != MA_SUCCESS) {
            LOG_WARN("ma_data_converter_process_pcm_frames failed");
            return eAB_error;
        }
        if (ma_paged_audio_buffer_data_append_page(&stream->paged_audio_buffer_data, new_page) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_append_page failed");
            return eAB_error;
        }
    } else { // no sampling frequency conversion needed
        if (ma_paged_audio_buffer_data_allocate_and_append_page(&stream->paged_audio_buffer_data, (ma_uint32)(size / (ma_uint64)stream->frame_size_in_bytes), data, NULL) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_allocate_and_append_page failed");
            return eAB_error;
        }
    }

    if (!ma_sound_is_playing(&stream->sound)) {
        // seek either at start, or where the accumulated value hasn't played yet
        if (ma_sound_seek_to_pcm_frame(&stream->sound, current_pos) != MA_SUCCESS) {
            LOG_WARN("ma_sound_seek_to_pcm_frame failed");
        }
        if (ma_sound_start(&stream->sound) != MA_SUCCESS) {
            LOG_WARN("ma_sound_start failed");
        }
    }
    if (ma_sound_at_end(&stream->sound)) {
        LOG_WARN("video not playing fast enough: sound starved!");
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tMiniaudio_stream* stream = stream_handle;
    if (stream->needs_converting) {
        ma_data_converter_uninit(&stream->data_converter, NULL);
    }
    ma_sound_stop(&stream->sound);
    ma_sound_uninit(&stream->sound);
    ma_paged_audio_buffer_uninit(&stream->paged_audio_buffer);
    ma_paged_audio_buffer_data_uninit(&stream->paged_audio_buffer_data, NULL);

    free(stream);
    return eAB_success;
}
