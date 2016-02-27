#include "game.h"
#include "assets.h"
#include "scene.h"
#include "coords.h"
#include <stdlib.h>

void controls_pre_update(Controls* controls) {
    memcpy(controls->last_frame, controls->this_frame, sizeof(controls->last_frame));
}

void switch_scene(Game* game, int to_scene) {
    SDL_assert(to_scene >= 0);
    SDL_assert(to_scene < sizeof(SCENES) / sizeof(Scene));

    game->current_scene->cleanup(game->current_scene_data, game);
    memset(game->current_scene_data, 0, SCENE_DATA_SIZE);

    game->current_scene = &SCENES[to_scene];
    game->current_scene->initialize(game->current_scene_data, game);
    // TODO do an update here? (this is the only case where a render can happen WITHOUT an update preceding..)
}

void start_editing_text(Game* game, char* text_to_edit, int buffer_size, SDL_Rect* input_rect) {
    game->text_edit.text = text_to_edit;
    game->text_edit.text_buf_size = buffer_size;
    game->text_edit.cursor = strlen(text_to_edit);
    game->text_edit.selection_length = 0;
    SDL_SetTextInputRect(input_rect);
    SDL_StartTextInput();
}

void stop_editing_text(Game* game) {
    game->text_edit.canceled = true;
    game->text_edit.text = NULL;
    SDL_StopTextInput();
}

void draw_text_ex_caret(Game* game, int x, int y, char* text, int padding, float scale, int caret) {
    const int original_x = x;
    int i = 0;

    while (*text) {
        // font image is 320x448
        // characters are 20x28
        if (*text == '\n') {
            x = original_x;
            y -= 28 * scale + padding;
        }
        else {
            int char_index = *text;
            SDL_Rect glyph = { (char_index % 16) * 20, (char_index / 16) * 28, 20, 28 };
            SDL_Rect drawto = { x, game->window_height - y, 20, 28 };
            drawto.w = (int)roundf((float)drawto.w * scale);
            drawto.h = (int)roundf((float)drawto.h * scale);
            SDL_RenderCopy(game->renderer, game->font, &glyph, &drawto);
            if (i == caret) {
                SDL_Rect caret_rect = drawto;
                caret_rect.w = 3;
                SDL_RenderFillRect(game->renderer, &caret_rect);
            }

            x += 20.0f * scale + padding;
        }
        text++;
        i++;
    }
    // TODO this is a copy/paste of the code up there :( might wanna move the
    // positioning code out into its own function.
    if (i == caret) {
        SDL_Rect caret_rect = { x, game->window_height - y, 3, 28 };
        caret_rect.w = (int)roundf((float)caret_rect.w * scale);
        caret_rect.h = (int)roundf((float)caret_rect.h * scale);
        SDL_RenderFillRect(game->renderer, &caret_rect);
    }
}

void draw_text_ex(Game* game, int x, int y, char* text, int padding, float scale) {
    draw_text_ex_caret(game, x, y, text, padding, scale, -1);
}
void draw_text_caret(Game* game, int x, int y, char* text, int caret) {
    draw_text_ex_caret(game, x, y, text, -1, 1.0f, caret);
}
void draw_text(Game* game, int x, int y, char* text) {
    draw_text_ex(game, x, y, text, -1, 1.0f);
}

void input_text(Game* game, char* text) {
    if (game->text_edit.text) {
        int input_size = strlen(text);
        char* current_spot = game->text_edit.text + game->text_edit.cursor;

        int len_from_current_spot = strlen(current_spot);
        if (input_size + game->text_edit.cursor + len_from_current_spot < game->text_edit.text_buf_size) {
            // "current_spot" should be where the next character will go. If that is zero, then it is
            // the end of the string and we need not make room in the middle.
            if (*current_spot != 0)
                memmove(current_spot + input_size, current_spot, len_from_current_spot);
            memmove(current_spot, text, input_size);
            game->text_edit.cursor += input_size;
        }
    }
}

void handle_key_during_text_edit(Game* game, SDL_Event* event) {
    switch (event->key.keysym.scancode) {
    case SDL_SCANCODE_ESCAPE:
        stop_editing_text(game);
        break;
    case SDL_SCANCODE_RETURN:
        game->text_edit.enter_pressed = true;
        break;
    case SDL_SCANCODE_LEFT:
        if (game->text_edit.cursor >= 0)
            game->text_edit.cursor -= 1;
        break;
    case SDL_SCANCODE_RIGHT:
        if (game->text_edit.cursor < strlen(game->text_edit.text))
            game->text_edit.cursor += 1;
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (game->text_edit.cursor > 0) {
            char* current_spot = game->text_edit.text + game->text_edit.cursor;
            int len_from_current_spot = strlen(current_spot);
            int buf_size_from_current_spot = game->text_edit.text_buf_size - game->text_edit.cursor;

            int amount = 1;
            memmove(current_spot - amount, current_spot, min(len_from_current_spot, buf_size_from_current_spot));
            current_spot[len_from_current_spot - amount] = 0;
            game->text_edit.cursor -= amount;
        }
        break;
    case SDL_SCANCODE_DELETE:
        if (game->text_edit.cursor < strlen(game->text_edit.text)) {
            char* current_spot = game->text_edit.text + game->text_edit.cursor;
            int len_from_current_spot = strlen(current_spot);
            int buf_size_from_current_spot = game->text_edit.text_buf_size - game->text_edit.cursor - 1;

            memmove(current_spot, current_spot + 1, min(len_from_current_spot, buf_size_from_current_spot));
            current_spot[len_from_current_spot - 1] = 0;
        }
      break;
    case SDL_SCANCODE_V:
        if (event->key.keysym.mod & KMOD_CTRL) {
            char* clipboard = SDL_GetClipboardText();
            if (clipboard)
                input_text(game, clipboard);
        }
        break;
    }
}
