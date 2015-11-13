#include "sound.h"
#include "SDL.h"
#include "assets.h"
#include "stb_vorbis.c"
#include "types.h"

extern SDL_Window* main_window;

const static int FREQUENCY = 44100;
const static int CHANNELS = 2;

typedef struct {
    byte* samples; // This is what gets mixed onto the stream.
    int samples_size;
    int samples_pos;
} AudioWave;

void initialize_sound() {
    // Uhhh maybe we dont' need this
}

void audio_callback(AudioWave* wave, byte* byte_stream, int byte_stream_size) {
    int bytes_remaining_in_wave = wave->samples_size - wave->samples_pos;
    memset(byte_stream, 0, byte_stream_size);
    int bytes_to_mix_in = min(bytes_remaining_in_wave, byte_stream_size);
    if (bytes_to_mix_in <= 0)
        return;

    SDL_MixAudio(
        byte_stream, wave->samples + wave->samples_pos,
        bytes_to_mix_in,
        SDL_MIX_MAXVOLUME
    );

    wave->samples_pos += bytes_to_mix_in;
}

AudioWave decode_ogg(AssetId asset) {
    AssetFile oggfile = load_asset(asset);

    int oggfile_channels, oggfile_frequency;
    AudioWave wave;
    wave.samples_pos = 0;
    int num_samples = stb_vorbis_decode_memory(
        oggfile.bytes, oggfile.size,
        &oggfile_channels, &oggfile_frequency,
        &wave.samples
    );
    wave.samples_size = num_samples * CHANNELS * sizeof(short);

    assert(oggfile_frequency == FREQUENCY);
    assert(oggfile_channels == CHANNELS);

    return wave;
}

void open_and_play_music() {
    // Load the file (LEAK FOR NOW)
    AudioWave* wave = malloc(sizeof(AudioWave));
    *wave = decode_ogg(ASSET_MUSIC_ARENA_OGG);

    // Actually try to play audio?
    SDL_AudioSpec want;
    SDL_AudioSpec got;
    memset(&want, 0, sizeof(SDL_AudioSpec));
    memset(&got, 0, sizeof(SDL_AudioSpec));

    want.freq = FREQUENCY;
    want.channels = CHANNELS;
    want.format = AUDIO_S16;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = wave;

    if (SDL_OpenAudio(&want, &got) < 0) {
        printf("SDL audio no good :( %s\n", SDL_GetError());
    }
    else if (got.format != want.format) {
        printf("Couldn't get wanted format!!!\n");
    }
    else {
        if (got.freq != want.freq)
            printf("Couldn't get frequency %i instead got %i", want.freq, got.freq);
        SDL_PauseAudio(0);
    }
}
