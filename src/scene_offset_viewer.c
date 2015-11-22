#include "scene.h"

typedef struct {
    Character crattlecrute;
    int scale;
    SDL_Texture* font_tex;
    int font_padding;
} OffsetViewer;

void scene_offset_viewer_initialize(OffsetViewer* data, Game* game) {
    data->crattlecrute.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->crattlecrute.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->crattlecrute.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    int* sensor = data->crattlecrute.bottom_sensors.x;
    sensor[0] = 0;  sensor[1] = 0;
    sensor[2] = 90; sensor[3] = 0;

    data->scale = 5;
// font image is 320x448
    data->font_tex = load_texture(game->renderer, ASSET_FONT_FONT_PNG);
    data->font_padding = -1;
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
static void drawtext(Game* game, OffsetViewer* s, int x, int y, char* text, float scale) {
    while (*text) {
        // font image is 320x448
        int char_index = *text;
        SDL_Rect glyph = { (char_index % 16) * 20, (char_index / 16) * 28, 20, 28 };
        SDL_Rect drawto = { x, game->window_height - y, 20, 28 };
        drawto.w = (int)roundf((float)drawto.w * scale);
        drawto.h = (int)roundf((float)drawto.h * scale);
        SDL_RenderCopy(game->renderer, s->font_tex, &glyph, &drawto);

        x += (int)((float)(20 + s->font_padding) * scale);
        text++;
    }
}
void scene_offset_viewer_render(OffsetViewer* s, Game* game) {
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
    offset.x = dest.x + s->crattlecrute.bottom_sensors.x[0] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.bottom_sensors.x[1] * scale);
    SDL_SetRenderDrawColor(game->renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(game->renderer, &offset);
    SDL_SetTextureColorMod(s->font_tex, 255, 100, 100);
    char str[32];
    SDL_snprintf(str, 32, "(%i,%i)", s->crattlecrute.bottom_sensors.x[0], s->crattlecrute.bottom_sensors.x[1]);
    drawtext(game, s, 50, 50, str, 0.8f);

    // Draw bottom sensor 2
    offset.x = dest.x + s->crattlecrute.bottom_sensors.x[2] * scale;
    offset.y = game->window_height - (dest.y + s->crattlecrute.bottom_sensors.x[3] * scale);
    SDL_SetRenderDrawColor(game->renderer, 100, 100, 255, 255);
    SDL_RenderFillRect(game->renderer, &offset);
    SDL_SetTextureColorMod(s->font_tex, 100, 100, 255);
    SDL_snprintf(str, 32, "(%i,%i)", s->crattlecrute.bottom_sensors.x[2], s->crattlecrute.bottom_sensors.x[3]);
    drawtext(game, s, game->window_width - 150, 50, str, 0.8f);

    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);

    float theta = (float)game->frame_count / 10.0f;
    int color_mod = SDL_SetTextureColorMod(s->font_tex, 255, 127 + (Uint8)(sin(theta) * 128), 0);
    SDL_assert(color_mod == 0);
    SDL_snprintf(str, 32, "%c Very nice TEXT..!", (char)25);
    drawtext(game, s, 10, game->window_height - 20, str, 1.5f);
}
void scene_offset_viewer_cleanup(OffsetViewer* data, Game* game) {
    SDL_DestroyTexture(data->crattlecrute.textures[0]);
    SDL_DestroyTexture(data->crattlecrute.textures[1]);
    SDL_DestroyTexture(data->crattlecrute.textures[2]);
}
