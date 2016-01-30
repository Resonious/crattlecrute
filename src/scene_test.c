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

#define EDITABLE_TEXT_BUFFER_SIZE 255
#define PACKET_SIZE 500
#define CONTROLS_BUFFER_SIZE (255 * NUM_CONTROLS)

typedef struct ControlsBuffer {
    SDL_atomic_t locked;
    int current_frame;
    int size;
    int pos;
    byte bytes[CONTROLS_BUFFER_SIZE];
} ControlsBuffer;

typedef struct RemotePlayer {
    int id;
    Controls controls;
    Character guy;
    int number_of_physics_updates_this_frame;
    int position_in_local_controls_stream;
    int frame_in_local_controls_stream;
    struct sockaddr_in address;

    ControlsBuffer controls_playback;
} RemotePlayer;

typedef struct TestScene {
    float gravity;
    float drag;
    Character guy;
    CharacterView guy_view;
    AudioWave* music;
    AudioWave* test_sound;
    Map* map;
    char editable_text[EDITABLE_TEXT_BUFFER_SIZE];

    ControlsBuffer controls_stream;
    const char* dbg_last_action;

    struct NetInfo {
        int remote_id;
        int next_id;
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
        // Ping is in frames
        int ping, ping_counter;
        SDL_atomic_t network_poke;

        // Array of malloced RemotePlayers
        int number_of_players;
        RemotePlayer* players[20];
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

#define NETOP_HERES_YOUR_ID 8
#define NETOP_WANT_ID 9
#define NETOP_UPDATE_PLAYER 10
#define NETOP_UPDATE_CONTROLS 11

void write_to_buffer(byte* buffer, void* src, int* pos, int size) {
    memcpy(buffer + *pos, src, size);
    *pos += size;
}

void read_from_buffer(byte* buffer, void* dest, int* pos, int size) {
    memcpy(dest, buffer + *pos, size);
    *pos += size;
}

int truncate_controls_buffer(ControlsBuffer* buffer, int to_frame, int to_pos) {
    SDL_assert(to_pos != 0 /*0 is reserved for # frames identifier :(*/);
    int frames_remaining = buffer->bytes[0] - buffer->current_frame;
    SDL_assert(frames_remaining > 0);
    SDL_assert(to_frame < frames_remaining);
    int bytes_remaining = buffer->size - buffer->pos;
    SDL_assert(bytes_remaining > 0);
    SDL_assert(to_pos < bytes_remaining);

    memmove(buffer->bytes + to_pos, buffer->bytes + buffer->pos, bytes_remaining);
    int old_buffer_size = buffer->size = bytes_remaining + to_pos;

    // SDL_assert((int)new_playback_buffer[0] + (int)frames_remaining < 255);
    buffer->bytes[0] = frames_remaining;
    buffer->current_frame = to_frame;
    buffer->pos = to_pos;

    return old_buffer_size;
}

RemotePlayer* player_of_id(TestScene* scene, int id, struct sockaddr_in* addr) {
    // TODO at some point we should have the player id be an index into this array
    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* plr = scene->net.players[i];
        if (plr != NULL && plr->id == id) {
            if (addr == NULL || (plr->address.sin_addr.s_addr == addr->sin_addr.s_addr && plr->address.sin_port == addr->sin_port))
                return plr;
            else // Wrong address - fuck you
                return NULL;
        }
    }
    return NULL;
}

RemotePlayer* netop_update_player(TestScene* scene, struct sockaddr_in* addr) {
    RemotePlayer* player = player_of_id(scene, scene->net.buffer[1], addr);
    if (player == NULL) {
        SET_LOCKED_STRING_F(scene->net.status_message, "No player of id %i", (int)scene->net.buffer[1]);
        return NULL;
    }

    int pos = 2;
    read_from_buffer(scene->net.buffer, player->guy.position.x, &pos, sizeof(player->guy.position.x));
    memcpy(player->guy.old_position.x, player->guy.position.x, sizeof(player->guy.position.x));
    read_from_buffer(scene->net.buffer, &player->guy.flip, &pos, sizeof(player->guy.flip));
    read_from_buffer(scene->net.buffer, &player->guy.body_color, &pos, sizeof(SDL_Color) * 3);

    return player;
}

RemotePlayer* netop_update_controls(TestScene* scene, struct sockaddr_in* addr) {
    int pos = 1; // [0] is opcode, which we know by now
    int player_id = (int)scene->net.buffer[pos++];

    RemotePlayer* player = player_of_id(scene, player_id, addr);
    if (player == NULL)
        return NULL;

    int new_playback_size;
    read_from_buffer(scene->net.buffer, &new_playback_size, &pos, sizeof(new_playback_size));

    // At this point, pos should be at the first byte of the playback buffer (# frames)
    // (also new_playback_size would be 0 but keeping it like this for now)
    if (scene->net.buffer[pos] == 0)
        return player;

    wait_for_then_use_lock(&player->controls_playback.locked);
    byte* new_playback_buffer = scene->net.buffer + pos;

    if (player->controls_playback.current_frame >= 0) {
        // Append new buffer data to current playback buffer
        int old_buffer_size = player->controls_playback.size;

        if ((int)new_playback_buffer[0] + (int)player->controls_playback.bytes[0] >= 255) {
            // Truncate old data from playback buffer
            old_buffer_size = truncate_controls_buffer(&player->controls_playback, 0, 1);

            SDL_assert(player->controls_playback.size > player->controls_playback.bytes[0]);
        }
        // Proceed with append
        player->controls_playback.size += new_playback_size - 1;
        player->controls_playback.bytes[0] += new_playback_buffer[0];
        // We keep offsetting new_playback_size by -1 because the first element is the number of frames recorded.
        memcpy(player->controls_playback.bytes + old_buffer_size, new_playback_buffer + 1, new_playback_size - 1);

        scene->dbg_last_action = "Append";
    }
    else {
        // Set playback buffer and start from the beginning
        player->controls_playback.current_frame = 0;
        player->controls_playback.pos = 1;
        player->controls_playback.size = new_playback_size;
        memcpy(player->controls_playback.bytes, new_playback_buffer, new_playback_size);

        scene->dbg_last_action = "Reset";
    }

    SDL_AtomicSet(&player->controls_playback.locked, false);

    return player;
}

// Target player is for server for whose position_in_local_controls_stream to use
int netwrite_guy_controls(TestScene* scene, RemotePlayer* target_player) {
    memset(scene->net.buffer, 0, PACKET_SIZE);
    int pos = 0;
    scene->net.buffer[pos++] = NETOP_UPDATE_CONTROLS;
    scene->net.buffer[pos++] = scene->net.remote_id;

    if (scene->controls_stream.current_frame < 0) {
        int zero = 0;
        write_to_buffer(scene->net.buffer, &zero, &pos, sizeof(zero));
        scene->net.buffer[pos++] = 0;
        return pos;
    }

    wait_for_then_use_lock(&scene->controls_stream.locked);

    byte* buffer_to_copy_over;
    int buffer_to_copy_over_size;

    if (target_player) {
        buffer_to_copy_over      = scene->controls_stream.bytes + target_player->position_in_local_controls_stream;
        buffer_to_copy_over_size = scene->controls_stream.pos - target_player->position_in_local_controls_stream;
        int actual_size = buffer_to_copy_over_size + 1;

        SDL_assert(buffer_to_copy_over_size < PACKET_SIZE);

        write_to_buffer(scene->net.buffer, &actual_size, &pos, sizeof(actual_size));
        int original_pos = pos;
        pos += 1;
        write_to_buffer(scene->net.buffer, &buffer_to_copy_over_size, &pos, sizeof(buffer_to_copy_over_size));
        scene->net.buffer[original_pos] = target_player->frame_in_local_controls_stream;

        target_player->position_in_local_controls_stream = scene->controls_stream.pos;
        target_player->frame_in_local_controls_stream    = scene->controls_stream.current_frame;
    }
    else {
        buffer_to_copy_over      = scene->controls_stream.bytes;
        buffer_to_copy_over[0]   = scene->controls_stream.current_frame;
        buffer_to_copy_over_size = scene->controls_stream.pos;

        SDL_assert(buffer_to_copy_over_size < PACKET_SIZE);

        scene->controls_stream.pos = 1;
        scene->controls_stream.current_frame = 0;

        write_to_buffer(scene->net.buffer, &buffer_to_copy_over_size, &pos, sizeof(buffer_to_copy_over_size));
        write_to_buffer(scene->net.buffer, buffer_to_copy_over, &pos, buffer_to_copy_over_size);
    }

    SDL_AtomicSet(&scene->controls_stream.locked, false);

    return pos;
}

int netwrite_guy_position(TestScene* scene) {
    SDL_assert(scene->net.remote_id >= 0);
    SDL_assert(scene->net.remote_id < 255);

    memset(scene->net.buffer, 0, PACKET_SIZE);
    int pos = 0;
    scene->net.buffer[pos++] = NETOP_UPDATE_PLAYER;
    scene->net.buffer[pos++] = (byte)scene->net.remote_id;

    write_to_buffer(scene->net.buffer, scene->guy.position.x, &pos, sizeof(scene->guy.position.x));
    write_to_buffer(scene->net.buffer, &scene->guy.flip, &pos, sizeof(scene->guy.flip));
    write_to_buffer(scene->net.buffer, &scene->guy.body_color, &pos, sizeof(scene->guy.body_color) * 3);
    return pos;
}

RemotePlayer* allocate_new_player(int id, struct sockaddr_in* addr) {
    RemotePlayer* new_player = malloc(sizeof(RemotePlayer));
    memset(new_player, 0, sizeof(RemotePlayer));

    new_player->id = id;
    new_player->address = *addr;

    default_character(&new_player->guy);
    new_player->guy.position.x[X] = 0.0f;
    new_player->guy.position.x[Y] = 0.0f;
    new_player->guy.position.x[2] = 0.0f;
    new_player->guy.position.x[3] = 0.0f;
    SDL_memset(&new_player->controls, 0, sizeof(Controls));

    // PLAYBACK_SHIT
    new_player->controls_playback.pos = 0;
    new_player->controls_playback.size = 0;
    memset(new_player->controls_playback.bytes, 0, sizeof(new_player->controls_playback.bytes));
    SDL_AtomicSet(&new_player->controls_playback.locked, false);
    new_player->controls_playback.current_frame = -1;
    new_player->controls_playback.pos = 1;

    new_player->position_in_local_controls_stream = -1;
    new_player->frame_in_local_controls_stream = -1;

    return new_player;
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
    // Server gets id = 0
    scene->net.remote_id = 0;
    scene->net.next_id = 1;

    struct sockaddr_in other_address;

    // NOTE do not exit this scene after initializing network lol
    while (true) {
        memset(scene->net.buffer, 0, PACKET_SIZE);

        SDL_AtomicSet(&scene->net.network_poke, true);

        int other_addr_len = sizeof(other_address);
        int recv_len = recvfrom(
            scene->net.local_socket,
            scene->net.buffer,
            PACKET_SIZE,
            0,
            (struct sockaddr*)&other_address,
            &other_addr_len
        );

        if (recv_len == SOCKET_ERROR) {
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to receive: %i", WSAGetLastError());
            scene->net.status = NOT_CONNECTED;
            r = 2; goto Done;
        }

        int bytes_wrote = 0;
        // SERVER
        switch (scene->net.buffer[0]) {
        case NETOP_HERES_YOUR_ID:
            SDL_assert(!"A client just tried to send us an ID. How dare they.");
            break;
        case NETOP_WANT_ID: {
            SDL_assert(scene->net.next_id < 255);
            int new_id = scene->net.next_id++;
            RemotePlayer* player = allocate_new_player(new_id, &other_address);
            scene->net.players[scene->net.number_of_players++] = player;
            scene->net.buffer[0] = NETOP_HERES_YOUR_ID;
            scene->net.buffer[1] = (byte)new_id;
            bytes_wrote = 2;
        } break;
        case NETOP_UPDATE_PLAYER:
            /*
            for (int i = 0; i < s->net.number_of_players; i++) {
                RemotePlayer* plr = s->net.players[i];
                */
            scene->net.connected = true;

            // TODO move this shit outside!!! NETOP_UPDATE_PLAYER will show up multiple times!
        wait_for_then_use_lock(&scene->controls_stream.locked);
        scene->controls_stream.current_frame = 0;
        scene->controls_stream.pos = 1;
        SDL_AtomicSet(&scene->controls_stream.locked, false);

            RemotePlayer* player = netop_update_player(scene, &other_address);
            player->position_in_local_controls_stream = scene->controls_stream.pos;
            player->frame_in_local_controls_stream    = scene->controls_stream.current_frame;
            bytes_wrote = netwrite_guy_position(scene);
            break;
        case NETOP_UPDATE_CONTROLS: {
            RemotePlayer* player = netop_update_controls(scene, &other_address);
            // TODO pass player if it's not NULL here - keeping like this to ensure I factor out the truncation shit
            bytes_wrote = netwrite_guy_controls(scene, NULL);
        } break;
        default:
            SDL_assert(!"Hey what's this? Unknown packet.");
            break;
        }

        // Now use the buffer to send to the client
        if (bytes_wrote > 0) {
            sendto(
                scene->net.local_socket,
                scene->net.buffer,
                bytes_wrote,
                0,
                (struct sockaddr*)&other_address,
                sizeof(other_address)
            );
        }
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

    struct sockaddr_in other_address;

    memset((char *) &other_address, 0, sizeof(other_address));
    other_address.sin_family = AF_INET;
    other_address.sin_addr.s_addr = inet_addr(scene->net.textinput_ip_address);
    other_address.sin_port = htons(atoi(scene->net.textinput_port));
    connect(scene->net.local_socket, (struct sockaddr*)&other_address, sizeof(other_address));

    scene->net.remote_id = -1;
    scene->net.next_id = -1;
    bool first_send = true;

    while (true) {
        int send_result;

        // CLIENT
        if (scene->net.remote_id == -1) {
            // Start updating controls, then send over the ID request.
            wait_for_then_use_lock(&scene->controls_stream.locked);
            scene->controls_stream.current_frame = 0;
            scene->controls_stream.pos = 1;
            SDL_AtomicSet(&scene->controls_stream.locked, false);

            scene->net.buffer[0] = NETOP_WANT_ID;
            send_result = send(
                scene->net.local_socket,
                scene->net.buffer,
                1,
                0
            );
        }
        else if (first_send) {
            int size = netwrite_guy_position(scene);
            send_result = send(
                scene->net.local_socket,
                scene->net.buffer,
                size,
                0
            );
            first_send = false;
        }
        else {
            int size = netwrite_guy_controls(scene, NULL);
            send_result = send(
                scene->net.local_socket,
                scene->net.buffer,
                size,
                0
            );
        }

        if (send_result == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to send: %i", error_code);
            scene->net.status = NOT_CONNECTED;
            return 1;
        }


        int other_addr_len = sizeof(other_address);
        int recv_len = recvfrom(
            scene->net.local_socket,
            scene->net.buffer,
            PACKET_SIZE,
            0,
            (struct sockaddr*)&other_address,
            &other_addr_len
        );
        if (recv_len == SOCKET_ERROR) {
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to receive: %i", WSAGetLastError());
            scene->net.status = NOT_CONNECTED;
            return 2;
        }

        // CLIENT
        switch (scene->net.buffer[0]) {
        case NETOP_HERES_YOUR_ID:
            scene->net.remote_id = scene->net.buffer[1];
            scene->net.connected = true;
            // Allocate server player for when netop_update_player comes in.
            scene->net.players[scene->net.number_of_players++] = allocate_new_player(0, &other_address);
            SET_LOCKED_STRING_F(scene->net.status_message, "Client ID: %i", scene->net.remote_id);
            break;
        case NETOP_UPDATE_PLAYER:
            netop_update_player(scene, NULL);
            break;
        case NETOP_UPDATE_CONTROLS:
            netop_update_controls(scene, NULL);
            break;
        default:
            SET_LOCKED_STRING_F(
                scene->net.status_message,
                "Data from %s:%d -> %i",
                inet_ntoa(other_address.sin_addr),
                ntohs(other_address.sin_port),
                (int)scene->net.buffer[0]
            );
            break;
        }

        SDL_AtomicSet(&scene->net.network_poke, true);
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

    data->controls_stream.current_frame = -1;
    data->controls_stream.pos = 1;

    // NETWORKING TIME
    {
        data->net.remote_id = -1;
        data->net.number_of_players = 0;
        memset(data->net.players, NULL, sizeof(data->net.players));

        data->net.connected = false;
        SDL_AtomicSet(&data->net.network_poke, false);
        data->net.ping_counter = 0;
        data->net.ping = 0;
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

    data->controls_stream.pos = 0;
    memset(data->controls_stream.bytes, 0, sizeof(data->controls_stream.bytes));
    SDL_AtomicSet(&data->controls_stream.locked, false);

    BENCH_START(loading_crattle1);
    default_character_animations(game, &data->guy_view);

    default_character(&data->guy);
    data->guy.position.x[X] = 150.0f;
    data->guy.position.x[Y] = 170.0f;
    data->guy.position.x[2] = 0.0f;
    data->guy.position.x[3] = 0.0f;
    data->guy.body_color.r = rand() % 255;
    data->guy.body_color.g = rand() % 255;
    data->guy.body_color.b = rand() % 255;
    data->guy.left_foot_color.r = rand() % 255;
    data->guy.left_foot_color.g = rand() % 255;
    data->guy.left_foot_color.b = rand() % 255;
    data->guy.right_foot_color = data->guy.left_foot_color;
    BENCH_END(loading_crattle1);

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

#define CONTROL_BLOCK_END NUM_CONTROLS

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;

    s->net.ping_counter += 1;
    if (SDL_AtomicGet(&s->net.network_poke)) {
        s->net.ping = s->net.ping_counter;
        s->net.ping_counter = 0;
        SDL_AtomicSet(&s->net.network_poke, false);
    }

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
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* plr = s->net.players[i];
        if (plr == NULL)
            continue;

        int number_of_netguy_physics_updates = 0;
        // TODO skip to the next guy and revisit if locked!
        wait_for_then_use_lock(&plr->controls_playback.locked);

        if (plr->controls_playback.current_frame >= 0) {
            if (plr->controls_playback.current_frame < plr->controls_playback.bytes[0]) {
                // Do 2 frames at a time if we're so many frames behind
                int frames_behind = (int)plr->controls_playback.bytes[0] - plr->controls_playback.current_frame;
                const int frames_behind_threshold = s->net.ping * 2;

                if (frames_behind > frames_behind_threshold) {
                    printf("Control sync behind by %i frames\n", frames_behind);
                    number_of_netguy_physics_updates = 2;
                }
                else
                    number_of_netguy_physics_updates = 1;
            }
            else
                SDL_assert(false); // This shouldn't happen (we check and set to -1 in that for loop)

            for (int i = 0; i < number_of_netguy_physics_updates; i++) {
                if (i > 0) // After the first update, we're doing consecutive ones and we need to "end" the frame.
                    character_post_update(&plr->guy);

                controls_pre_update(&plr->controls);
                memset(plr->controls.this_frame, 0, sizeof(plr->controls.this_frame));

                while (plr->controls_playback.bytes[plr->controls_playback.pos] != CONTROL_BLOCK_END) {
                    SDL_assert(plr->controls_playback.pos < plr->controls_playback.size);
                    SDL_assert(plr->controls_playback.bytes[plr->controls_playback.pos] < NUM_CONTROLS);

                    bool* this_frame = plr->controls.this_frame;
                    int control = plr->controls_playback.bytes[plr->controls_playback.pos];
                    this_frame[control] = true;

                    plr->controls_playback.pos += 1;
                }
                plr->controls_playback.pos += 1;
                plr->controls_playback.current_frame += 1;

                apply_character_physics(game, &plr->guy, &plr->controls, s->gravity, s->drag);
                collide_character(&plr->guy, &s->map->tile_collision);
                slide_character(s->gravity, &plr->guy);

                if (plr->controls_playback.current_frame >= plr->controls_playback.bytes[0]) {
                    plr->controls_playback.current_frame = -1;
                    break;
                }
            }
        }
        SDL_AtomicSet(&plr->controls_playback.locked, false);

        for (int i = 0; i < max(1, number_of_netguy_physics_updates); i++)
            update_character_animation(&plr->guy);
    }

    // Update local player
    apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);
    update_character_animation(&s->guy);

    // Follow local player with camera
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

    // Record controls! FOR NETWORK
    wait_for_then_use_lock(&s->controls_stream.locked);

    /*
    if (just_pressed(&game->controls, C_W)) {
        s->controls_stream.current_frame = -1;
        s->playback_frame  = -1;
    }
    */
    if (s->controls_stream.current_frame >= 0) {
        // record
        for (enum Control ctrl = 0; ctrl < NUM_CONTROLS; ctrl++) {
            if (game->controls.this_frame[ctrl])
                s->controls_stream.bytes[s->controls_stream.pos++] = ctrl;
        }
        s->controls_stream.bytes[s->controls_stream.pos++] = CONTROL_BLOCK_END;
        SDL_assert(s->controls_stream.pos < CONTROLS_BUFFER_SIZE);

        s->controls_stream.current_frame += 1;
    }
    else if (s->controls_stream.current_frame < -1) {
        s->controls_stream.current_frame += 1;
    }
    SDL_AtomicSet(&s->controls_stream.locked, false);

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    character_post_update(&s->guy);
    for (int i = 0; i < s->net.number_of_players; i++) {
        character_post_update(&s->net.players[i]->guy);
    }

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
            s->net.status = NOT_CONNECTED;
        }
        if (game->text_edit.enter_pressed) {
            stop_editing_text(game);
            switch (s->net.status) {
            case HOSTING:
                SDL_CreateThread(network_server_loop, "Network server loop", s);
                break;
            case JOINING:
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
        for (int i = 0; i < s->net.number_of_players; i++) {
            draw_character(game, &s->net.players[i]->guy, &s->guy_view);
        }
    }

    // Recording indicator
    {
        char* text = NULL;
        int num = 0;
        if (s->controls_stream.current_frame >= 0) {
            set_text_color(game, 255, 0, 0);
            text = "Recording (%i)";
            num = s->controls_stream.current_frame;
        }
        else if (s->controls_stream.current_frame < -1) {
            set_text_color(game, 50, 255, 50);
            text = "Done!";
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

    if (s->net.status != NOT_CONNECTED) {
        wait_for_then_use_lock(&s->net.status_message_locked);
        set_text_color(game, 100, 50, 255);
        draw_text(game, 10, game->window_height - 50, s->net.status_message);
        SDL_AtomicSet(&s->net.status_message_locked, false);
    }

    draw_text_ex_f(game, game->window_width - 150, game->window_height - 40, -1, 0.7f, "Ping: %i", s->net.ping);
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;

    closesocket(data->net.local_socket);
    if (data->net.buffer)
        free(data->net.buffer);
}
