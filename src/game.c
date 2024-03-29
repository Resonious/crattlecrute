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

void draw_text_box(struct Game* game, SDL_Rect* text_box_rect, char* text) {
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &g, &b, &a);
    SDL_SetRenderDrawColor(game->renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game->renderer, text_box_rect);

    set_text_color(game, 0, 0, 0);
    SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
    size_t caret = game->frame_count % 30 < (30 / 2) ? game->text_edit.cursor : -1;
    draw_text_caret(game, text_box_rect->x + 4, ((int)game->window_height - text_box_rect->y) - 4, text, caret);
    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
}

void start_editing_text(Game* game, char* text_to_edit, int buffer_size, SDL_Rect* input_rect) {
    SDL_memset(game->controls.this_frame, 0, sizeof(game->controls.this_frame));
    game->text_edit.text = text_to_edit;
    game->text_edit.text_buf_size = buffer_size;
    game->text_edit.cursor = (int)strlen(text_to_edit);
    game->text_edit.selection_length = 0;
    SDL_SetTextInputRect(input_rect);
    SDL_StartTextInput();
}

void stop_editing_text(Game* game) {
    game->text_edit.canceled = true;
    game->text_edit.text = NULL;
    SDL_StopTextInput();
}

void draw_text_ex_caret(Game* game, int x, int y, char* text, int padding, float scale, size_t caret) {
    const int original_x = x;
    size_t i = 0;

    while (*text) {
        // font image is 320x448
        // characters are 20x28
        if (*text == '\n') {
            x = original_x;
            y -= (int)(28 * scale) + padding;
        }
        else {
            int char_index = *text;
            SDL_Rect glyph = { (char_index % 16) * 20, (char_index / 16) * 28, 20, 28 };
            SDL_Rect drawto = { x, (int)game->window_height - y, 20, 28 };
            drawto.w = (int)roundf((float)drawto.w * scale);
            drawto.h = (int)roundf((float)drawto.h * scale);
            SDL_RenderCopy(game->renderer, game->font, &glyph, &drawto);
            if (i == caret) {
                SDL_Rect caret_rect = drawto;
                caret_rect.w = 3;
                SDL_RenderFillRect(game->renderer, &caret_rect);
            }

            x += (int)(20.0f * scale) + padding;
        }
        text++;
        i++;
    }
    // TODO this is a copy/paste of the code up there :( might wanna move the
    // positioning code out into its own function.
    if (i == caret) {
        SDL_Rect caret_rect = { x, (int)game->window_height - y, 3, 28 };
        caret_rect.w = (int)roundf((float)caret_rect.w * scale);
        caret_rect.h = (int)roundf((float)caret_rect.h * scale);
        SDL_RenderFillRect(game->renderer, &caret_rect);
    }
}

void draw_text_ex(Game* game, int x, int y, char* text, int padding, float scale) {
    draw_text_ex_caret(game, x, y, text, padding, scale, -1);
}
void draw_text_caret(Game* game, int x, int y, char* text, size_t caret) {
    draw_text_ex_caret(game, x, y, text, -1, 1.0f, caret);
}
void draw_text(Game* game, int x, int y, char* text) {
    draw_text_ex(game, x, y, text, -1, 1.0f);
}

void input_text(Game* game, char* text) {
    if (game->text_edit.text) {
        size_t input_size = strlen(text);
        char* current_spot = game->text_edit.text + game->text_edit.cursor;

        size_t len_from_current_spot = strlen(current_spot);
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
            size_t len_from_current_spot = strlen(current_spot);
            size_t buf_size_from_current_spot = game->text_edit.text_buf_size - game->text_edit.cursor;

            int amount = 1;
            memmove(current_spot - amount, current_spot, min(len_from_current_spot, buf_size_from_current_spot));
            current_spot[len_from_current_spot - amount] = 0;
            game->text_edit.cursor -= amount;
        }
        break;
    case SDL_SCANCODE_DELETE:
        if (game->text_edit.cursor < strlen(game->text_edit.text)) {
            char* current_spot = game->text_edit.text + game->text_edit.cursor;
            size_t len_from_current_spot = strlen(current_spot);
            size_t buf_size_from_current_spot = game->text_edit.text_buf_size - game->text_edit.cursor - 1;

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

int map_asset_for_area(int area_id) {
    switch (area_id) {
    case AREA_TESTZONE_ONE: return ASSET_MAPS_TEST3_CM;
    case AREA_TESTZONE_TWO: return ASSET_MAPS_TEST4_CM;
    case AREA_NET_ZONE:     return ASSET_MAPS_TRANSITION_CM;
    case AREA_GARDEN:       return ASSET_MAPS_GARDEN_CM;
    default:
        SDL_assert(!"UNKNOWN AREA!");
        return ASSET_MAPS_TEST3_CM;
    }
}

bool area_is_garden(int area_id) {
    return area_id == AREA_GARDEN;
}

void set_data_chunk_cap(DataChunk* chunk, int capacity) {
    if (chunk->bytes == NULL) {
        chunk->bytes = malloc(capacity);
        chunk->capacity = capacity;
    }
    else {
        if (capacity > chunk->capacity) {
            chunk->bytes = realloc(chunk->bytes, capacity);
            chunk->capacity = capacity;
        }
    }
}

int write_game_data_thread(void* vgame) {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    Game* game = (Game*)vgame;

    while (true) {
        while (!SDL_AtomicGet(&game->data.write_wanted))
            SDL_Delay(1000);

        if (game->data.character >= 0) {
            FILE* game_data;
            errno_t err;

            if (err = fopen_s(&game_data, game->gamedata_file_path, "wb")) {
                printf("Failed to write gamedata (errno: %i)", err);
            }
            else {
                write_game_data(&game->data, game_data);
                fclose(game_data);
#ifdef _DEBUG
                printf("Wrote game data.\n");
#endif
            }
        }
        SDL_AtomicSet(&game->data.write_wanted, false);
    }
}

void write_data_chunk(DataChunk* chunk, FILE* file) {
    fwrite(&chunk->size, sizeof(int), 1, file);
    if (chunk->size > 0) {
        fwrite(chunk->bytes, 1, chunk->size, file);
    }
}

void read_data_chunk(DataChunk* chunk, FILE* file) {
    fread(&chunk->size, sizeof(int), 1, file);
    if (chunk->size > 0) {
        set_data_chunk_cap(chunk, chunk->size);
        fread(chunk->bytes, 1, chunk->size, file);
    }
}

typedef size_t(*FileIoFunc)(void*, size_t, size_t, FILE*);
typedef void(*DataChunkFunc)(DataChunk*, FILE*);
static FileIoFunc ftransfer_func[] = { (FileIoFunc)fread, (FileIoFunc)fwrite };
static DataChunkFunc transfer_data_chunk_func[] = { read_data_chunk, write_data_chunk };

#define CC_DATA_CURRENT_VERSION 1

void transfer_game_data(GameData* data, byte rw, FILE* file) {
    wait_for_then_use_lock(data->locked);
    FileIoFunc ftransfer = ftransfer_func[rw];
    DataChunkFunc transfer_data_chunk = transfer_data_chunk_func[rw];

    // ============= FILE VERSION ================
    int cc_data_version = rw == ABD_WRITE ? CC_DATA_CURRENT_VERSION : -1;
    ftransfer(&cc_data_version, sizeof(int), 1, file);

    if (rw == ABD_READ && cc_data_version != CC_DATA_CURRENT_VERSION) {
        printf("WARNING: wrong data file version (%i) - can't load game data.\n", cc_data_version);
        return;
    }

    // ============= Current Area ==============
    ftransfer(&data->area, sizeof(int), 1, file);

    // ============== Position =================
    transfer_data_chunk(&data->character_physics_state, file);

    // ========== Character Attributes =========
    ftransfer(&data->character_count, sizeof(int), 1, file);
    ftransfer(&data->character, sizeof(int), 1, file);
    for (int i = 0; i < data->character_count; i++)
        transfer_data_chunk(&data->characters[i], file);

    // ========== NUMBER OF AREAS ==============
    int number_of_areas = rw == ABD_WRITE ? NUMBER_OF_AREAS : -1;
    ftransfer(&number_of_areas, sizeof(int), 1, file);
    for (int i = 0; i < number_of_areas; i++)
        transfer_data_chunk(&data->maps[i], file);

    SDL_UnlockMutex(data->locked);
}

#define BUF_FROM_CHUNK(chunk) (AbdBuffer){.pos=0, .capacity=(chunk).size, .bytes=(chunk).bytes}
void inspect_game_data(GameData* data, FILE* f) {
    fprintf(f, "Area: %i\n", data->area);
    fprintf(f, "Current character #: %i\n", data->character);
    fprintf(f, "---Character physics state:\n");
    abd_inspect((AbdBuffer*)&data->character_physics_state, f);

    for (int i = 0; i < data->character_count; i++) {
        fprintf(f, "\n======= CHARACTER %i of %i: =======\n", i, data->character_count - 1);
        AbdBuffer buf = BUF_FROM_CHUNK(data->characters[i]);
        abd_inspect(&buf, f);
    }

    for (int i = 0; i < NUMBER_OF_AREAS; i++) {
        DataChunk* chunk = &data->maps[i];
        if (chunk->size <= 0)
            continue;

        AbdBuffer buf = BUF_FROM_CHUNK(data->maps[i]);
        fprintf(f, "\n======= AREA %i =======\n", i);
        abd_inspect(&buf, f);
    }
}

bool save_game(Game* game) {
    SDL_AtomicSet(&game->data.write_wanted, true);
    return !!game->gamedata_file_path;
}
