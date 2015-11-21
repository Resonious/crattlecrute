#ifndef SCENE_H
#define SCENE_H

#include "game.h"

typedef struct {
    void(*initialize)(void* data, Game* game);
    void(*update)(void* data, Game* game);
    void(*render)(void* data, Game* game);
    void(*cleanup)(void* data, Game* game);
} Scene;

const static int SCENE_TEST = 0;
void scene_test_initialize(void* data, Game* game);
void scene_test_update(void* data, Game* game);
void scene_test_render(void* data, Game* game);
void scene_test_cleanup(void* data, Game* game);
const static Scene SCENES[] = {
    {
    scene_test_initialize,
    scene_test_update,
    scene_test_render,
    scene_test_cleanup
    }
};

#endif