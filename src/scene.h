#ifndef SCENE_H
#define SCENE_H

#include "game.h"

typedef struct Scene {
    int id;
    void(*initialize)(void* data, Game* game);
    void(*update)(void* data, Game* game);
    void(*render)(void* data, Game* game);
    void(*cleanup)(void* data, Game* game);
} Scene;

#define SCENE_TEST 0
void scene_test_initialize(void* data, Game* game);
void scene_test_update(void* data, Game* game);
void scene_test_render(void* data, Game* game);
void scene_test_cleanup(void* data, Game* game);

#define SCENE_OFFSET_VIEWER 1
void scene_offset_viewer_initialize(void* data, Game* game);
void scene_offset_viewer_update(void* data, Game* game);
void scene_offset_viewer_render(void* data, Game* game);
void scene_offset_viewer_cleanup(void* data, Game* game);

const static Scene SCENES[] = {
    {
        SCENE_TEST,
    scene_test_initialize,
    scene_test_update,
    scene_test_render,
    scene_test_cleanup
    },
    {
        SCENE_OFFSET_VIEWER,
    scene_offset_viewer_initialize,
    scene_offset_viewer_update,
    scene_offset_viewer_render,
    scene_offset_viewer_cleanup
    }
};

#endif