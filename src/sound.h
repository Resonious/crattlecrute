#ifndef SOUND_H
#define SOUND_H

#include "types.h"
#define AUDIO_ONESHOT_CHANNELS 25
#define AUDIO_LOOPED_CHANNELS 10

typedef struct {
    int samples_size;
    int samples_pos;
    byte* samples; // This is what gets mixed onto the stream.
} AudioWave;

// TODO we probably at some point want some lock on this guy in case we try
// to remove waves while they're being copied into the audio buffer.
typedef struct {
    AudioWave* oneshot_waves[AUDIO_ONESHOT_CHANNELS];
    AudioWave* looped_waves[AUDIO_LOOPED_CHANNELS];
} AudioQueue;

void initialize_sound();
AudioWave* open_and_play_music();
AudioWave decode_ogg(int asset);
// Used for free function for cached audio wave assets.
void free_malloced_audio_wave(void* audio_wave);

#endif // SOUND_H