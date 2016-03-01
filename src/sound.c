#include "sound.h"
#ifdef __FreeBSD__
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif
#include "assets.h"
#ifndef __APPLE__
#include "stb_vorbis.c"
#endif

extern SDL_Window* main_window;

const static int FREQUENCY = 44100;
const static int CHANNELS = 2;

void audio_callback(AudioQueue* queue, byte* byte_stream, int byte_stream_size) {
    // First just set the stream to silence.
    // TODO do this maybe after trying to mix? I'm sure it doesn't really matter.
    memset(byte_stream, 0, byte_stream_size);

    // Mix in oneshots
    for (int i = 0; i < AUDIO_ONESHOT_CHANNELS; i++) {
        AudioWave* wave = queue->oneshot_waves[i];
        if (wave == NULL) continue;

        int bytes_remaining_in_wave = wave->samples_size - wave->samples_pos;

        int bytes_to_mix_in = min(bytes_remaining_in_wave, byte_stream_size);
        SDL_assert(bytes_to_mix_in > 0);

        SDL_MixAudio(
            byte_stream, wave->samples + wave->samples_pos,
            bytes_to_mix_in,
            SDL_MIX_MAXVOLUME
        );

        wave->samples_pos += bytes_to_mix_in;
        // Take it out of the queue if it's done playing. Hope to god
        // it gets freed. (It should be managed elsewhere I think.)
        if (wave->samples_pos >= wave->samples_size) {
            wave->samples_pos = 0;
            queue->oneshot_waves[i] = NULL;
        }
    }

    // Mix in loops
    for (int i = 0; i < AUDIO_LOOPED_CHANNELS; i++) {
        AudioWave* wave = queue->looped_waves[i];
        if (wave == NULL) continue;

        int bytes_mixed_in = 0;
        do {
            int bytes_remaining_in_wave = wave->samples_size - wave->samples_pos;
            int bytes_to_mix_in = min(bytes_remaining_in_wave, byte_stream_size - bytes_mixed_in);

            SDL_MixAudio(
                byte_stream + bytes_mixed_in, wave->samples + wave->samples_pos,
                bytes_to_mix_in,
                SDL_MIX_MAXVOLUME
            );
            wave->samples_pos += bytes_to_mix_in;
            bytes_mixed_in += bytes_to_mix_in;

            if (wave->samples_pos >= wave->samples_size)
                wave->samples_pos = 0;
        } while (bytes_mixed_in < byte_stream_size);
    }
}

void initialize_sound(AudioQueue* queue) {
    SDL_AudioSpec want;
    SDL_AudioSpec got;
    memset(&want, 0, sizeof(SDL_AudioSpec));
    memset(&got, 0, sizeof(SDL_AudioSpec));
    // Clear out the queue. If for some reason you do this while audio is
    // actually playing then this might break.
    for (int i = 0; i < AUDIO_ONESHOT_CHANNELS; i++)
        queue->oneshot_waves[i] = NULL;
    for (int i = 0; i < AUDIO_LOOPED_CHANNELS; i++)
        queue->looped_waves[i] = NULL;

    want.freq = FREQUENCY;
    want.channels = CHANNELS;
    want.format = AUDIO_S16;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = queue;

    if (SDL_OpenAudio(&want, &got) < 0) {
        printf("SDL audio no good :( %s\n", SDL_GetError());
    }
    else if (got.format != want.format) {
        printf("Couldn't get wanted format!!!\n");
    }
    if (got.freq != want.freq)
        printf("Couldn't get frequency %i instead got %i", want.freq, got.freq);

    SDL_PauseAudio(0);
}

AudioWave decode_ogg(int asset) {
    AssetFile oggfile = load_asset(asset);

    int oggfile_channels, oggfile_frequency;
    AudioWave wave;
    wave.samples_pos = 0;
    int num_samples = stb_vorbis_decode_memory(
        oggfile.bytes, (int)oggfile.size,
        &oggfile_channels, &oggfile_frequency,
        (short**)&wave.samples
    );
    wave.samples_size = num_samples * CHANNELS * sizeof(short);

    SDL_assert(oggfile_frequency == FREQUENCY);
    if (oggfile_channels == 1) {
        // If we only have one channel, we gotta make the output suitable
        // for our assumed stereo audio format.
        short* mono_samples = (short*)wave.samples;
        short* stereo_samples = malloc(wave.samples_size);
        for (int i = 0, j = 0; i < num_samples; i++) {
            stereo_samples[j++] = mono_samples[i];
            stereo_samples[j++] = mono_samples[i];
        }
        free(wave.samples);
        wave.samples = (byte*)stereo_samples;
    }

    return wave;
}

AudioWave* open_and_play_music(AudioQueue* queue) {
    // Load the file (LEAK FOR NOW) (JK WE FREE IT IN MAIN())
    AudioWave* wave = malloc(sizeof(AudioWave));
    *wave = decode_ogg(ASSET_MUSIC_ARENA_OGG);

    // Play on looped channel 0
    queue->looped_waves[0] = wave;

    return wave;
}

void free_malloced_audio_wave(void* audio_wave_v) {
    AudioWave* audio_wave = (AudioWave*)audio_wave_v;
    free(audio_wave->samples);
    free(audio_wave);
}
