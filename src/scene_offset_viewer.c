#include "scene.h"

typedef struct {
    Character crattlecrute;
    int scale;
} OffsetViewer;

void scene_offset_viewer_initialize(OffsetViewer* data, Game* game) {
    data->crattlecrute.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->crattlecrute.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->crattlecrute.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    int* sensor = data->crattlecrute.bottom_sensors.x;
    sensor[0] = 0;  sensor[1] = 0;
    sensor[2] = 90; sensor[3] = 0;

    data->scale = 5;
}
void scene_offset_viewer_update(OffsetViewer* s, Game* game) {
    if (just_pressed(&game->controls, C_UP))
        s->crattlecrute.bottom_sensors.x[3] += 1;
    if (just_pressed(&game->controls, C_DOWN))
        s->crattlecrute.bottom_sensors.x[3] -= 1;
    if (just_pressed(&game->controls, C_LEFT))
        s->crattlecrute.bottom_sensors.x[2] -= 1;
    if (just_pressed(&game->controls, C_RIGHT))
        s->crattlecrute.bottom_sensors.x[2] += 1;

    if (just_pressed(&game->controls, C_W))
        s->crattlecrute.bottom_sensors.x[1] += 1;
    if (just_pressed(&game->controls, C_S))
        s->crattlecrute.bottom_sensors.x[1] -= 1;
    if (just_pressed(&game->controls, C_A))
        s->crattlecrute.bottom_sensors.x[0] -= 1;
    if (just_pressed(&game->controls, C_D))
        s->crattlecrute.bottom_sensors.x[0] += 1;

    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_TEST);
}
void scene_offset_viewer_render(OffsetViewer* s, Game* game) {
    int scale = s->scale;

    SDL_Rect src = { 0 * 90, 0, 90, 90 };
    SDL_Rect dest = { 0, 0, 90 * scale, 90 * scale };
    dest.x = (game->window_width - dest.w) / 2;
    dest.y = (game->window_height - dest.h) / 2;

    for (int i = 0; i < 3; i++)
        SDL_RenderCopy(game->renderer, s->crattlecrute.textures[i], &src, &dest);

    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);

    // Draw bounding box
    SDL_RenderDrawRect(game->renderer, &dest);

    SDL_Rect offset = { 0, 0, scale, scale };

    // Draw bottom sensor 1
    offset.x = dest.x + s->crattlecrute.bottom_sensors.x[0] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.bottom_sensors.x[1] * scale);
    SDL_SetRenderDrawColor(game->renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(game->renderer, &offset);

    // Draw bottom sensor 2
    offset.x = dest.x + s->crattlecrute.bottom_sensors.x[2] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.bottom_sensors.x[3] * scale);
    SDL_SetRenderDrawColor(game->renderer, 100, 100, 255, 255);
    SDL_RenderFillRect(game->renderer, &offset);

    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
}
void scene_offset_viewer_cleanup(OffsetViewer* data, Game* game) {
    SDL_DestroyTexture(data->crattlecrute.textures[0]);
    SDL_DestroyTexture(data->crattlecrute.textures[1]);
    SDL_DestroyTexture(data->crattlecrute.textures[2]);
}
