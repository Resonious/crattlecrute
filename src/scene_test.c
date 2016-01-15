#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#define WSAGetLastError() (-1)
#define SOCKET_ERROR (-1)
#define SOCKET int
#define closesocket close
#endif

#include "scene.h"
#include "game.h"
#include "character.h"
#include "assets.h"
#include "coords.h"
#include <stdlib.h>

#ifdef _DEBUG
extern bool debug_pause;
/*
extern int b1_tilespace_x, b1_tilespace_y, l1_tilespace_x, l1_tilespace_y, r1_tilespace_x, r1_tilespace_y;
extern int b2_tilespace_x, b2_tilespace_y, l2_tilespace_x, l2_tilespace_y, r2_tilespace_x, r2_tilespace_y;
*/
#endif

// 5 seconds
#define RECORDED_FRAME_COUNT 60 * 5
#define EDITABLE_TEXT_BUFFER_SIZE 255
#define PACKET_SIZE 500

typedef struct {
    Controls dummy_controls;
    float gravity;
    float drag;
    Character guy;
    Character guy2;
    CharacterView guy_view;
    AudioWave* music;
    AudioWave* test_sound;
    Map* map;
    int recording_frame;
    int playback_frame;
    bool recorded_controls[RECORDED_FRAME_COUNT][NUM_CONTROLS];
    char editable_text[EDITABLE_TEXT_BUFFER_SIZE];

    struct {
        SDL_atomic_t status_message_locked;
        char status_message[512];
        char textinput_ip_address[EDITABLE_TEXT_BUFFER_SIZE];
        char textinput_port[EDITABLE_TEXT_BUFFER_SIZE];
        enum { HOSTING, JOINING, NOT_CONNECTED } status;
        bool connected;

        SOCKET local_socket;
        struct sockaddr_in my_address;
        struct sockaddr_in other_address;
        SDL_atomic_t buffer_locked;
        byte* buffer;
    } net;
} TestScene;

void wait_for_then_use_lock(SDL_atomic_t* lock) {
    while (SDL_AtomicGet(lock)) {}
    SDL_AtomicSet(lock, true);
}

#define SET_LOCKED_STRING(var, str) \
    wait_for_then_use_lock(&var##_locked); \
    strcpy(var, str); \
    SDL_AtomicSet(&var##_locked, false);
#define SET_LOCKED_STRING_F(var, str, ...) \
    wait_for_then_use_lock(&var##_locked); \
    SDL_snprintf(var, 512, str, __VA_ARGS__); \
    SDL_AtomicSet(&var##_locked, false);

#define NETOP_UPDATE_POSITION 10

void netop_update_position(TestScene* scene) {
    memcpy(scene->guy2.position.x, scene->net.buffer + 1, sizeof(scene->guy2.position.x));
    memcpy(&scene->guy2.flip, scene->net.buffer + 1 + sizeof(scene->guy2.position.x), sizeof(scene->guy2.flip));
}

void netwrite_guy_position(TestScene* scene) {
    memset(scene->net.buffer, 0, PACKET_SIZE);
    scene->net.buffer[0] = NETOP_UPDATE_POSITION;
    memcpy(scene->net.buffer + 1, scene->guy.position.x, sizeof(scene->guy.position.x));
    memcpy(scene->net.buffer + 1 + sizeof(scene->guy.position.x), &scene->guy.flip, sizeof(scene->guy.flip));
}

int network_server_loop(void* vdata) {
    int r = 0;
    TestScene* scene = (TestScene*)vdata;
    scene->net.buffer = malloc(PACKET_SIZE);

    SET_LOCKED_STRING(scene->net.status_message, "Server started!");

    memset((char *) &scene->net.my_address, 0, sizeof(scene->net.my_address));
    scene->net.my_address.sin_family = AF_INET;
    scene->net.my_address.sin_addr.s_addr = INADDR_ANY;
    scene->net.my_address.sin_port = htons(atoi(scene->net.textinput_port));
    if(bind(scene->net.local_socket, (struct sockaddr *)&scene->net.my_address , sizeof(scene->net.my_address)) == SOCKET_ERROR)
    {
        SET_LOCKED_STRING_F(scene->net.status_message, "Failed to bind: %i", WSAGetLastError());
        return 1;
    }

    // NOTE do not exit this scene after initializing network lol
    while (true) {
        memset(scene->net.buffer, 0, PACKET_SIZE);

        int other_addr_len = sizeof(scene->net.other_address);
        int recv_len = recvfrom(
            scene->net.local_socket,
            scene->net.buffer,
            PACKET_SIZE,
            0,
            (struct sockaddr*)&scene->net.other_address,
            &other_addr_len
        );

        if (recv_len == SOCKET_ERROR) {
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to receive: %i", WSAGetLastError());
            scene->net.status = NOT_CONNECTED;
            r = 2; goto Done;
        }

        switch (scene->net.buffer[0]) {
        case NETOP_UPDATE_POSITION:
            netop_update_position(scene);
            break;
        default:
            SET_LOCKED_STRING_F(
                scene->net.status_message,
                "Data from %s:%d -> %i",
                inet_ntoa(scene->net.other_address.sin_addr),
                ntohs(scene->net.other_address.sin_port),
                (int)scene->net.buffer[0]
            );
            break;
        }

        // Now use the buffer to send to the client
        netwrite_guy_position(scene);
        sendto(
            scene->net.local_socket,
            scene->net.buffer,
            sizeof(scene->guy.position.x) + 1,
            0,
            (struct sockaddr*)&scene->net.other_address,
            sizeof(scene->net.other_address)
        );
    }

    Done:
    closesocket(scene->net.local_socket);
    scene->net.local_socket = 0;
    return r;
}

int network_client_loop(void* vdata) {
    TestScene* scene = (TestScene*)vdata;
    scene->net.buffer = malloc(PACKET_SIZE);

    SET_LOCKED_STRING(scene->net.status_message, "Connecting to server!");

    memset((char *) &scene->net.other_address, 0, sizeof(scene->net.other_address));
    scene->net.other_address.sin_family = AF_INET;
    scene->net.other_address.sin_addr.s_addr = inet_addr(scene->net.textinput_ip_address);
    scene->net.other_address.sin_port = htons(atoi(scene->net.textinput_port));
    connect(scene->net.local_socket, (struct sockaddr*)&scene->net.other_address, sizeof(scene->net.other_address));

    while (true) {
        // Send character position (TODO this is currently a copy/paste)
        netwrite_guy_position(scene);
        int send_result = send(
            scene->net.local_socket,
            scene->net.buffer,
            sizeof(scene->guy.position.x) + 1,
            0
        );

        if (send_result == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to send: %i", error_code);
            scene->net.status = NOT_CONNECTED;
            return 1;
        }


        int other_addr_len = sizeof(scene->net.other_address);
        int recv_len = recvfrom(
            scene->net.local_socket,
            scene->net.buffer,
            PACKET_SIZE,
            0,
            (struct sockaddr*)&scene->net.other_address,
            &other_addr_len
        );

        switch (scene->net.buffer[0]) {
        case NETOP_UPDATE_POSITION:
            netop_update_position(scene);
            break;
        default:
            SET_LOCKED_STRING_F(
                scene->net.status_message,
                "Data from %s:%d -> %i",
                inet_ntoa(scene->net.other_address.sin_addr),
                ntohs(scene->net.other_address.sin_port),
                (int)scene->net.buffer[0]
            );
            break;
        }
    }

    return 0;
}

SDL_Rect text_box_rect = { 200, 200, 400, 40 };

void scene_test_initialize(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    SDL_memset(data->editable_text, 0, sizeof(data->editable_text));
    SDL_strlcat(data->editable_text, "hey", EDITABLE_TEXT_BUFFER_SIZE);

    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->drag = 0.025f; // Again p/s^2

    SDL_memset(&data->dummy_controls, 0, sizeof(Controls));

    data->recording_frame = -1;
    data->playback_frame = -1;

    // NETWORKING TIME
    {
        data->net.status = NOT_CONNECTED;
        SDL_AtomicSet(&data->net.buffer_locked, false);
        SDL_AtomicSet(&data->net.status_message_locked, false);

        SDL_memset(data->net.textinput_ip_address, 0, sizeof(data->net.textinput_ip_address));
        SDL_strlcat(data->net.textinput_ip_address, "127.0.0.1", EDITABLE_TEXT_BUFFER_SIZE);
        SDL_memset(data->net.textinput_port, 0, sizeof(data->net.textinput_port));
        SDL_strlcat(data->net.textinput_port, "2997", EDITABLE_TEXT_BUFFER_SIZE);

        data->net.buffer = NULL;
        data->net.local_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        SDL_assert(data->net.local_socket != SOCKET_ERROR);
    }

    BENCH_START(loading_crattle1);
    default_character_animations(game, &data->guy_view);

    default_character(&data->guy);
    data->guy.position.x[X] = 150.0f;
    data->guy.position.x[Y] = 170.0f;
    data->guy.position.x[2] = 0.0f;
    data->guy.position.x[3] = 0.0f;
    BENCH_END(loading_crattle1);

    BENCH_START(loading_crattle2)
    default_character(&data->guy2);
    data->guy2.position.x[X] = 250.0f;
    data->guy2.position.x[Y] = 170.0f;
    data->guy2.position.x[2] = 0.0f;
    data->guy2.position.x[3] = 0.0f;
    data->guy2.body_color.r = 255;
    data->guy2.body_color.g = 20;
    data->guy2.body_color.b = 251;
    BENCH_END(loading_crattle2)

    BENCH_START(loading_tiles)
    data->map = cached_map(game, ASSET_MAPS_TEST3_CM);
    BENCH_END(loading_tiles);

    BENCH_START(loading_sound);
    data->music = cached_sound(game, ASSET_MUSIC_ARENA_OGG);
    game->audio.looped_waves[0] = data->music;

    data->test_sound = cached_sound(game, ASSET_SOUNDS_JUMP_OGG);
    data->guy_view.jump_sound = data->test_sound;
    BENCH_END(loading_sound);
}

#define PERCENT_CHANCE(percent) (rand() < RAND_MAX / (100 / percent))

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;

    controls_pre_update(&s->dummy_controls);
    // Stupid AI
    /*
    if (game->frame_count % 10 == 0) {
        bool* c = s->dummy_controls.this_frame;
        if (PERCENT_CHANCE(50)) {
            int dir = PERCENT_CHANCE(65) ? C_RIGHT : C_LEFT;
            int other_dir = dir == C_RIGHT ? C_LEFT : C_RIGHT;

            c[dir] = !c[dir];
            if (c[dir] && c[other_dir]) {
                c[PERCENT_CHANCE(50) ? dir : other_dir] = false;
            }
        }

        if (s->guy2.left_hit && c[C_LEFT]) {
            c[C_LEFT] = false;
            c[C_RIGHT] = true;
        }
        else if (s->guy2.right_hit && c[C_RIGHT]) {
            c[C_RIGHT] = false;
            c[C_LEFT] = true;
        }

        c[C_UP] = s->guy2.jumped || PERCENT_CHANCE(20);
    }
    */
    // Have guy2 playback recorded controls
    if (s->playback_frame >= 0) {
        if (s->playback_frame >= RECORDED_FRAME_COUNT)
            s->playback_frame = -1;
        else {
            SDL_memcpy(s->dummy_controls.this_frame, s->recorded_controls[s->playback_frame], sizeof(s->dummy_controls.this_frame));
            s->playback_frame += 1;
        }
    }

    // Update characters
    apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);
    update_character_animation(&s->guy);

    if (s->net.status == NOT_CONNECTED) {
        apply_character_physics(game, &s->guy2, &s->dummy_controls, s->gravity, s->drag);
        collide_character(&s->guy2, &s->map->tile_collision);
        slide_character(s->gravity, &s->guy2);
    }
    update_character_animation(&s->guy2);


    // Follow player with camera
    game->camera_target.x[X] = s->guy.position.x[X] - game->window_width / 2.0f;
    if (s->guy.grounded) {
        game->camera_target.x[Y] = s->guy.position.x[Y] - game->window_height * 0.35f;
        game->follow_cam_y = false;
    }
    else {
        if (s->guy.position.x[Y] - game->camera_target.x[Y] < 1.5f)
            game->follow_cam_y = true;
        if (game->follow_cam_y)
            game->camera_target.x[Y] = s->guy.position.x[Y] - game->window_height * 0.5f;
    }
    // move cam position towards cam target
    game->camera.simd = _mm_add_ps(game->camera.simd, _mm_mul_ps(_mm_sub_ps(game->camera_target.simd, game->camera.simd), _mm_set_ps(0, 0, 0.1f, 0.1f)));
    // Snap to cam target after awhile to stop a certain amount of jerkiness.
    const float cam_alpha = 0.01f;
    vec4 cam_dist_from_target;
    cam_dist_from_target.simd = _mm_sub_ps(game->camera_target.simd, game->camera.simd);
    if (fabsf(cam_dist_from_target.x[X]) < cam_alpha)
        game->camera.x[X] = game->camera_target.x[X];
    if (fabsf(cam_dist_from_target.x[Y]) < cam_alpha)
        game->camera.x[Y] = game->camera_target.x[Y];

    // Record controls! Unsure if this is the way to go for network
    if (just_pressed(&game->controls, C_W)) {
        s->recording_frame = -1;
        s->playback_frame  = -1;
    }
    if (s->recording_frame >= 0) {
        if (s->recording_frame >= RECORDED_FRAME_COUNT) {
            // Set last frame to all zeros so the dummy stops (might not wanna do this for networking)
            SDL_memset(s->recorded_controls[s->recording_frame - 1], 0, sizeof(game->controls));
            // -60 so that we can display a message for 60 frames
            // indicating that we are finished recording
            s->recording_frame = -60;
        }
        else {
            SDL_memcpy(s->recorded_controls[s->recording_frame], &game->controls, sizeof(game->controls));
            s->recording_frame += 1;
        }
    }
    else if (s->recording_frame < -1) {
        s->recording_frame += 1;
    }
    else if (s->playback_frame == -1) {
        if (just_pressed(&game->controls, C_A))
            s->recording_frame = 0;
        else if (just_pressed(&game->controls, C_S))
            s->playback_frame = 0;
        else if (just_pressed(&game->controls, C_D))
            // TODO teleportation should be considered a special case if we decide to use multi-pass collision.
            s->guy.position.simd = s->guy2.position.simd;
    }

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    character_post_update(&s->guy);
    character_post_update(&s->guy2);

    // Swap to offset viewer on F1 press
    // if (just_pressed(&game->controls, C_F1))
        // switch_scene(game, SCENE_OFFSET_VIEWER);

    if (s->net.status == NOT_CONNECTED) {
        if (just_pressed(&game->controls, C_F1)) {
            start_editing_text(game, s->net.textinput_ip_address, EDITABLE_TEXT_BUFFER_SIZE, &text_box_rect);
            s->net.status = HOSTING;
        }
        else if (just_pressed(&game->controls, C_F2)) {
            start_editing_text(game, s->net.textinput_ip_address, EDITABLE_TEXT_BUFFER_SIZE, &text_box_rect);
            s->net.status = JOINING;
        }
    }
    else {
        if (game->text_edit.canceled) {
            // This seems maybe unreliable lol... or a little too reliable
            s->net.status == NOT_CONNECTED;
        }
        if (game->text_edit.enter_pressed) {
            stop_editing_text(game);
            switch (s->net.status) {
            case HOSTING:
                s->net.connected = true;
                SDL_CreateThread(network_server_loop, "Network server loop", s);
                break;
            case JOINING:
                s->net.connected = true;
                SDL_CreateThread(network_client_loop, "Network client loop", s);
                break;
            default:
                SDL_assert(false);
                break;
            }
        }
    }
}

void draw_text_box(struct Game* game, SDL_Rect* text_box_rect, char* text) {
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &g, &b, &a);
    SDL_SetRenderDrawColor(game->renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game->renderer, text_box_rect);

    set_text_color(game, 0, 0, 0);
    SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
    int caret = game->frame_count % 30 < (30 / 2) ? game->text_edit.cursor : -1;
    draw_text_caret(game, text_box_rect->x + 4, (game->window_height - text_box_rect->y) - 4, text, caret);
    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
}

void scene_test_render(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
    /* == Keeping this around in case I want it ==
#ifdef _DEBUG
    if (debug_pause) {
        // DEBUG: b-sensor 1 tile index
        int sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[0];
        int sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[1];
        b1_tilespace_x = sense_x / 32;
        b1_tilespace_y = sense_y / 32;
        // DEBUG: b-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[3];
        b2_tilespace_x = sense_x / 32;
        b2_tilespace_y = sense_y / 32;
        // DEBUG: l-sensor 1 tile index
        sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[0];
        sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[1];
        l1_tilespace_x = sense_x / 32;
        l1_tilespace_y = sense_y / 32;
        // DEBUG: l-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[3];
        l2_tilespace_x = sense_x / 32;
        l2_tilespace_y = sense_y / 32;
        // DEBUG: r-sensor 1 tile index
        sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[0];
        sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[1];
        r1_tilespace_x = sense_x / 32;
        r1_tilespace_y = sense_y / 32;
        // DEBUG: r-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[3];
        r2_tilespace_x = sense_x / 32;
        r2_tilespace_y = sense_y / 32;
    }
#endif
    */

    // Draw WHOLE MAP
    draw_map(game, s->map);

    // Draw guys
    {
        draw_character(game, &s->guy, &s->guy_view);
        draw_character(game, &s->guy2, &s->guy_view);
    }

    // Recording indicator
    {
        char* text = NULL;
        int num = 0;
        if (s->recording_frame >= 0) {
            set_text_color(game, 255, 0, 0);
            text = "Recording (%i)";
            num = RECORDED_FRAME_COUNT - s->recording_frame;
        }
        else if (s->recording_frame < -1) {
            set_text_color(game, 50, 255, 50);
            text = "Done!";
        }
        else if (s->playback_frame >= 0) {
            set_text_color(game, 100, 0, 255);
            text = "Playback (%i)";
            num = RECORDED_FRAME_COUNT - s->playback_frame;
        }
        if (text)
            draw_text_f(game, game->window_width / 2 - 128, game->window_height - 35, text, num);
    }

    // Draw editable text box!!
    if (game->text_edit.text == s->editable_text) {
        draw_text_box(game, &text_box_rect, s->editable_text);
    }
    else if (game->text_edit.text == s->net.textinput_ip_address) {
        set_text_color(game, 255, 255, 255);
        if (s->net.status == HOSTING)
            draw_text(game, text_box_rect.x - 130, 200, "So you want to be server");
        else
            draw_text(game, text_box_rect.x - 100, 200, "So you want to be client");

        draw_text_box(game, &text_box_rect, s->net.textinput_ip_address);
    }

    if (s->net.connected) {
        wait_for_then_use_lock(&s->net.status_message_locked);
        set_text_color(game, 100, 50, 255);
        draw_text(game, 10, game->window_height - 50, s->net.status_message);
        SDL_AtomicSet(&s->net.status_message_locked, false);
    }
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;

    closesocket(data->net.local_socket);
    if (data->net.buffer)
        free(data->net.buffer);
}
