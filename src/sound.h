#include "types.h"
#define AUDIO_ONESHOT_CHANNELS 25
#define AUDIO_LOOPED_CHANNELS 10

typedef struct {
    byte* samples; // This is what gets mixed onto the stream.
    int samples_size;
    int samples_pos;
} AudioWave;

// TODO we probably at some point want some lock on this guy in case we try
// to remove waves while they're being copied into the audio buffer.
typedef struct {
    AudioWave* oneshot_waves[AUDIO_ONESHOT_CHANNELS];
    AudioWave* looped_waves[AUDIO_LOOPED_CHANNELS];
} AudioQueue;

void initialize_sound();
AudioWave* open_and_play_music();
AudioWave decode_ogg(AssetId asset);