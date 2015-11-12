#include "sound.h"
#include "SDL.h"
#include "assets.h"
#include "stb_vorbis.c"
#include "types.h"

extern SDL_Window* main_window;

struct Buffer {
    stb_vorbis* vorbis;
    byte* bytes;
    size_t size;
    size_t pos;
};

void initialize_sound() {
    // Uhhh maybe we dont' need this
}

void audio_callback(void* userdata, byte* byte_stream, int len) {
    struct Buffer* oggdata = (struct Buffer*)userdata;

    // Output from stb_vorbis.
    int channels;
    float** output;
    int samples;

    // Our format is float32, right? So we want to write floats,
    // and the stream length will be 1/4th of itself in bytes.
    // Right?
    float* stream = byte_stream;
    assert(len % sizeof(float) == 0); // I'm assuming this should be true since we ask for float.
    int stream_size = len / sizeof(float);

    // We already have the whole ogg file in memory, so we just
    // get the section of it that stb_vorbis is after.
    byte* datablock = oggdata->bytes + oggdata->pos;
    int datablock_size = oggdata->size - oggdata->pos;

    int bytes_used = stb_vorbis_decode_frame_pushdata(
        oggdata->vorbis,
        datablock, datablock_size,
        &channels, &output, &samples
    );

    if (bytes_used == 0 && samples == 0) {
        // Need more data
        printf("0 and 0? bye");
        exit(0);
    }
    else if (bytes_used != 0 && samples == 0) {
        // Resynching, go again?
        // TODO wtf to do here?
        printf("resyncing? bye");
        exit(0);
    }
    else if (bytes_used != 0 && samples != 0) {
        // Got a frame!
        // So, if samples < stream_size, then we gotta keep asking stb_vorbis for
        // more. If stream_size < samples, then we gotta hang onto the rest of output
        // for next callback.

        if (samples < stream_size) {
            printf("We're fucked bye");
            exit(0);
        }

        // TODO TODO TODO TODO TODO TODO

        for (int i = 0; i < samples; i += 2) {
            stream[i] = output[0][i];
            stream[i + 1] = output[1][i];
        }
    }
    else {
        // WTF
        assert(false);
    }
}

void open_and_play_music() {
    // Load the file and get header info
    AssetFile oggfile = load_asset(ASSET_MUSIC_ARENA_OGG);

    int header_byte_count, error;
    stb_vorbis* vorbis = stb_vorbis_open_pushdata(
        oggfile.bytes, oggfile.size,
        &header_byte_count, &error, 0
    );

    assert(vorbis != NULL);

    // Actually try to play audio?
    stb_vorbis_info info = stb_vorbis_get_info(&vorbis);

    SDL_AudioSpec want;
    SDL_AudioSpec got;
    memset(&want, 0, sizeof(SDL_AudioSpec));
    memset(&got, 0, sizeof(SDL_AudioSpec));

    want.freq = info.sample_rate;
    want.channels = info.channels;
    want.format = AUDIO_F32;
    want.samples = 4096;
    want.callback = audio_callback;
    // TODO we'll just leak this for now.
    struct Buffer* oggdata = malloc(sizeof(struct Buffer));
    // So remember, we'll have one vorbis per ogg file being played.
    oggdata->vorbis = vorbis;
    oggdata->bytes  = oggfile.bytes + header_byte_count;
    oggdata->size   = oggfile.size - header_byte_count;
    oggdata->pos    = 0;
    want.userdata = oggdata;

    if (SDL_OpenAudio(&want, &got) < 0) {
        printf("SDL audio no good :( %s\n", SDL_GetError());
    }
    else if (got.format != want.format) {
        printf("Couldn't get float32 format!!!\n");
    }
    else {
        SDL_PauseAudio(0);
    }
}
