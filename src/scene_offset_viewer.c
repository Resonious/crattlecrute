#include "scene.h"
#include "assets.h"
#include "coords.h"

typedef struct {
    Character crattlecrute;
    int scale;
    SDL_Texture* font_tex;
    int font_padding;
} OffsetViewer;

void scene_offset_viewer_initialize(void* vdata, Game* game) {
    OffsetViewer* data = (OffsetViewer*)vdata;
    data->crattlecrute.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->crattlecrute.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->crattlecrute.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    default_character(&data->crattlecrute);

    data->scale = 5;
// font image is 320x448
    data->font_tex = load_texture(game->renderer, ASSET_FONT_FONT_PNG);
    data->font_padding = -1;
}
void scene_offset_viewer_update(void* vs, Game* game) {
    OffsetViewer* s = (OffsetViewer*)vs;
    if (just_pressed(&game->controls, C_UP))
        s->crattlecrute.left_sensors.x[3] += 1;
    if (just_pressed(&game->controls, C_DOWN))
        s->crattlecrute.left_sensors.x[3] -= 1;
    if (just_pressed(&game->controls, C_LEFT))
        s->crattlecrute.left_sensors.x[2] -= 1;
    if (just_pressed(&game->controls, C_RIGHT))
        s->crattlecrute.left_sensors.x[2] += 1;

    if (just_pressed(&game->controls, C_W))
        s->crattlecrute.left_sensors.x[1] += 1;
    if (just_pressed(&game->controls, C_S))
        s->crattlecrute.left_sensors.x[1] -= 1;
    if (just_pressed(&game->controls, C_A))
        s->crattlecrute.left_sensors.x[0] -= 1;
    if (just_pressed(&game->controls, C_D))
        s->crattlecrute.left_sensors.x[0] += 1;

    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_TEST);
}
void scene_offset_viewer_render(void* vs, Game* game) {
    OffsetViewer* s = (OffsetViewer*)vs;
    int scale = s->scale;

    SDL_Rect src = { 0 * 90, 0, 90, 90 };
    SDL_Rect dest = { 0, 0, 90 * scale, 90 * scale };
    dest.x = (game->window_width - dest.w) / 2;
    dest.y = (game->window_height - dest.h) / 2;

    for (int i = 0; i < 3; i++)
        SDL_RenderCopyEx(game->renderer, s->crattlecrute.textures[i], &src, &dest, 0, 0,
            game->controls.this_frame[C_SPACE] ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);

    // Draw bounding box
    SDL_RenderDrawRect(game->renderer, &dest);

    SDL_Rect offset = { 0, 0, scale, scale };

    // Draw bottom sensor 1
    offset.x = dest.x + s->crattlecrute.left_sensors.x[0] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.left_sensors.x[1] * scale);
    SDL_SetRenderDrawColor(game->renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(game->renderer, &offset);
    set_text_color(game, 255, 100, 100);
    draw_text_ex_f(
        game, 50, 50, -1, 0.8f, "(%i,%i)",
        s->crattlecrute.left_sensors.x[0], s->crattlecrute.left_sensors.x[1]
    );

    // Draw bottom sensor 2
    offset.x = dest.x + s->crattlecrute.left_sensors.x[2] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.left_sensors.x[3] * scale);
    SDL_SetRenderDrawColor(game->renderer, 100, 100, 255, 255);
    SDL_RenderFillRect(game->renderer, &offset);
    set_text_color(game, 100, 100, 255);
    draw_text_ex_f(
        game, game->window_width - 150, 50, -1, 0.8f, "(%i,%i)",
        s->crattlecrute.left_sensors.x[2], s->crattlecrute.left_sensors.x[3]
    );

    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);

    float theta = (float)game->frame_count / 10.0f;
    set_text_color(game, 255, 127 + (Uint8)(sin(theta) * 128), 0);
    draw_text_ex_f(game, 10, game->window_height - 20, -3, 1.5f, "%c Very nice TEXT..!", (char)26);
}
void scene_offset_viewer_cleanup(void* vdata, Game* game) {
    OffsetViewer* data = (OffsetViewer*)vdata;
    SDL_DestroyTexture(data->crattlecrute.textures[0]);
    SDL_DestroyTexture(data->crattlecrute.textures[1]);
    SDL_DestroyTexture(data->crattlecrute.textures[2]);
}
