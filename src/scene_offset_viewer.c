#include "scene.h"

typedef struct {
    SDL_Texture* crattlecrute_textures[3];
    int scale;
} OffsetViewer;

void scene_offset_viewer_initialize(OffsetViewer* data, Game* game) {
    data->crattlecrute_textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->crattlecrute_textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->crattlecrute_textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    data->scale = 5;
}
void scene_offset_viewer_update(OffsetViewer* s, Game* game) {
    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_TEST);
}
void scene_offset_viewer_render(OffsetViewer* s, Game* game) {
    SDL_Rect src = { 0 * 90, 0, 90, 90 };
    SDL_Rect dest = { 0, 0, 90 * 4, 90 * 4 };
    dest.x = (game->window_width - dest.w) / 2;
    dest.y = (game->window_height - dest.h) / 2;

    for (int i = 0; i < 3; i++)
        SDL_RenderCopy(game->renderer, s->crattlecrute_textures[i], &src, &dest);
}
void scene_offset_viewer_cleanup(OffsetViewer* data, Game* game) {
    SDL_DestroyTexture(data->crattlecrute_textures[0]);
    SDL_DestroyTexture(data->crattlecrute_textures[1]);
    SDL_DestroyTexture(data->crattlecrute_textures[2]);
}
