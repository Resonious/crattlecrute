#ifndef SCENE_H
#define SCENE_H

#include "game.h"

typedef struct {
    void(*initialize)(void* data, Game* game);
    void(*update)(void* data, Game* game);
    void(*cleanup)(void* data, Game* game);
} Scene;

void scene_test_initialize(void* data, Game* game);
void scene_test_update(void* data, Game* game);
void scene_test_cleanup(void* data, Game* game);
const Scene scene_test = {
    scene_test_initialize,
    scene_test_update,
    scene_test_cleanup
};

#endif