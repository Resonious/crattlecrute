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

#define SCENE_WORLD 0
void scene_world_initialize(void* data, Game* game);
void scene_world_update(void* data, Game* game);
void scene_world_render(void* data, Game* game);
void scene_world_cleanup(void* data, Game* game);

#define SCENE_OFFSET_VIEWER 1
void scene_offset_viewer_initialize(void* data, Game* game);
void scene_offset_viewer_update(void* data, Game* game);
void scene_offset_viewer_render(void* data, Game* game);
void scene_offset_viewer_cleanup(void* data, Game* game);

static Scene SCENES[] = {
    {
        SCENE_WORLD,
    scene_world_initialize,
    scene_world_update,
    scene_world_render,
    scene_world_cleanup
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
