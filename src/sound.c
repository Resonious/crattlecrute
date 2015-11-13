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

    memset(byte_stream, 0, len);
    float* mix = malloc(len);

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
    int stream_pos = 0;

    while (stream_pos < stream_size) {
        // We already have the whole ogg file in memory, so we just
        // get the section of it that stb_vorbis is after.
        byte* datablock = oggdata->bytes + oggdata->pos;
        int datablock_size = oggdata->size - oggdata->pos;
        if (datablock_size <= 0)
            break;

        int bytes_used = stb_vorbis_decode_frame_pushdata(
            oggdata->vorbis,
            datablock, datablock_size,
            &channels, &output, &samples
            );
        oggdata->pos += bytes_used;

        if (bytes_used == 0 && samples == 0) {
            // Need more data
            printf("0 and 0? bye");
            exit(0);
        }
        else if (bytes_used != 0 && samples == 0) {
            // Resynching, go again?
        }
        else if (bytes_used != 0 && samples != 0) {
            // TODO !!!!!!!!!!!! THIS IS WHY IT SOUNDS CHOPPY AND BAD:
            // We reach the end of the buffer and still have output from
            // stb_vorbis. We gotta hold onto that leftover output!!!

            float* stream_chunk = stream + stream_pos;
            int num_samples = min(samples, (stream_size - stream_pos) / 2);

            int floats_wrote = 0;
            for (int i = 0; i < num_samples; i += 1) {
                // This kinda assumes 2 channels
                mix[floats_wrote++] = output[0][i];
                mix[floats_wrote++] = output[1][i];
            }

            SDL_MixAudio(stream_chunk, mix, floats_wrote * sizeof(float), SDL_MIX_MAXVOLUME / 2);
            stream_pos += floats_wrote;
        }
        else {
            // WTF
            assert(false);
        }
    }
    // TODO Let's not allocate and free like this in the end okay?
    free(mix);
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
    SDL_AudioSpec want;
    SDL_AudioSpec got;
    memset(&want, 0, sizeof(SDL_AudioSpec));
    memset(&got, 0, sizeof(SDL_AudioSpec));

    want.freq = vorbis->sample_rate;
    want.channels = vorbis->channels;
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
        if (got.freq != want.freq)
            printf("Couldn't get frequency %i instead got %i", want.freq, got.freq);
        SDL_PauseAudio(0);
    }
}
