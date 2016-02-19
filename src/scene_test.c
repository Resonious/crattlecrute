#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
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
#include <string.h>
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
#define CONTROL_BLOCK_END NUM_CONTROLS
#define MAX_PLAYERS 20
#define FRAMES_BETWEEN_POSITION_SYNCS 30

typedef struct ControlsBuffer {
    SDL_mutex* locked;
    int current_frame;
    int size;
    int pos;
    byte bytes[CONTROLS_BUFFER_SIZE];
} ControlsBuffer;

typedef struct ControlsBufferSpot {
    int pos;
    int frame;
} ControlsBufferSpot;

typedef struct RemotePlayer {
    int id;
    Controls controls;
    int number_of_physics_updates_this_frame;
    Character guy;
    ControlsBufferSpot local_stream_spot;
    // NOTE this IS indexed by player ID - scene.net.players is not.
    ControlsBufferSpot stream_spots_of[MAX_PLAYERS];
    struct sockaddr_in address;
    SDL_atomic_t poke;

    // Countdown until we SEND our position
    SDL_atomic_t position_sync_countdown;
    // Position we have RECEIVED and plan to set (if we so choose)
    vec4 sync_position, sync_old_position;
    SDL_atomic_t sync_on_frame;

    int last_frames_playback_pos;
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
        struct sockaddr_in server_address;
        SDL_atomic_t buffer_locked;
        byte* buffer;
        // Ping is in frames
        int ping, ping_counter;

        // Array of malloced RemotePlayers
        int number_of_players;
        RemotePlayer* players[MAX_PLAYERS];
    } net;
} TestScene;

void net_character_post_update(RemotePlayer* plr) {
    character_post_update(&plr->guy);
    plr->last_frames_playback_pos = plr->controls_playback.pos;
}

void wait_for_then_use_lock(SDL_mutex* mutex) {
    /*
    while (SDL_AtomicGet(lock)) {}
    SDL_AtomicSet(lock, true);
    */
    if (SDL_LockMutex(mutex) != 0)
        exit(1);
}

void sync_player_frame_if_should(int status, RemotePlayer* plr) {
    if (status == JOINING) {
        int sync_on_frame = SDL_AtomicGet(&plr->sync_on_frame);
        if (plr->controls_playback.current_frame == sync_on_frame) {
            plr->guy.position.simd = plr->sync_position.simd;
            plr->guy.old_position  = plr->sync_old_position;

            SDL_AtomicSet(&plr->sync_on_frame, -1);
        }
    }
}

#define SET_LOCKED_STRING(var, str) \
    while (SDL_AtomicGet(&var##_locked)) {} \
    SDL_AtomicSet(&var##_locked, true); \
    strcpy(var, str); \
    SDL_AtomicSet(&var##_locked, false);
#define SET_LOCKED_STRING_F(var, str, ...) \
    while (SDL_AtomicGet(&var##_locked)) {} \
    SDL_AtomicSet(&var##_locked, true); \
    SDL_snprintf(var, 512, str, __VA_ARGS__); \
    SDL_AtomicSet(&var##_locked, false);

#define NETOP_HERES_YOUR_ID 8
#define NETOP_WANT_ID 9
#define NETOP_INITIALIZE_PLAYER 10
#define NETOP_UPDATE_CONTROLS 11

void write_to_buffer(byte* buffer, void* src, int* pos, int size) {
    SDL_assert(size >= 0);
    memcpy(buffer + *pos, src, size);
    *pos += size;
}

void read_from_buffer(byte* buffer, void* dest, int* pos, int size) {
    memcpy(dest, buffer + *pos, size);
    *pos += size;
}

int simple_truncate_controls_buffer(ControlsBuffer* buffer, int to_frame, int to_pos) {
    SDL_assert(to_pos > 0 /*0 is reserved for # frames identifier :(*/);
    int frames_remaining = buffer->bytes[0] - buffer->current_frame;
    SDL_assert(frames_remaining >= 0);
    SDL_assert(to_frame <= frames_remaining);
    int bytes_remaining = buffer->size - buffer->pos;
    if (bytes_remaining <= 0)
        return 0;
    SDL_assert(to_pos <= bytes_remaining);

    memmove(buffer->bytes + to_pos, buffer->bytes + buffer->pos, bytes_remaining);

    buffer->size = bytes_remaining + to_pos;
    buffer->bytes[0] = frames_remaining;
    buffer->current_frame = to_frame;
    buffer->pos = to_pos;

    return buffer->size;
}

struct BufferChanges {
    int change_in_position, change_in_frame, change_in_size, change_in_bytes0;
} truncate_controls_buffer(ControlsBuffer* buffer, int zero_frame, int zero_pos) {
    if (zero_pos < 1)
        zero_pos = 1;
    else if (zero_pos > buffer->size)
        zero_pos = buffer->size;

    if (zero_frame < 0)
        zero_frame = 0;
    else if (zero_frame > buffer->bytes[0])
        zero_frame = buffer->bytes[0];

    int frame_difference  = buffer->current_frame - zero_frame;
    int size_difference   = buffer->size - zero_pos;
    int bytes0_difference = buffer->bytes[0] - zero_frame;
    int pos_difference    = buffer->pos - zero_pos;

    int original_bytes0   = buffer->bytes[0];
    int original_position = buffer->pos;
    int original_size     = buffer->size;
    int original_frame    = buffer->current_frame;

    SDL_assert(frame_difference >= 0);
    SDL_assert(pos_difference >= 0);

    memmove(buffer->bytes + 1, buffer->bytes + zero_pos, size_difference);

    buffer->bytes[0]      = bytes0_difference;
    buffer->size          = size_difference + 1;

    // SDL_assert(buffer->bytes[buffer->pos - 1] == CONTROL_BLOCK_END);

    buffer->current_frame = frame_difference;
    buffer->pos           = pos_difference + 1;

    struct BufferChanges change = {
        original_position - buffer->pos,
        original_frame - buffer->current_frame,
        original_size - buffer->size,
        original_bytes0 - buffer->bytes[0]
    };
    return change;
}

RemotePlayer* allocate_new_player(int id, struct sockaddr_in* addr) {
    RemotePlayer* new_player = aligned_malloc(sizeof(RemotePlayer));
    memset(new_player, 0, sizeof(RemotePlayer));

    new_player->id = id;
    new_player->address = *addr;

    default_character(&new_player->guy);
    new_player->guy.position.x[X] = 0.0f;
    new_player->guy.position.x[Y] = 0.0f;
    new_player->guy.position.x[2] = 0.0f;
    new_player->guy.position.x[3] = 0.0f;
    SDL_memset(&new_player->controls, 0, sizeof(Controls));
    SDL_memset(new_player->stream_spots_of, 0, sizeof(new_player->stream_spots_of));

    // PLAYBACK_SHIT
    memset(new_player->controls_playback.bytes, 0, sizeof(new_player->controls_playback.bytes));
    new_player->controls_playback.locked = SDL_CreateMutex();
    if (!new_player->controls_playback.locked) {
        printf("BAD MUTEX\n");
    }
    new_player->controls_playback.current_frame = -1;
    new_player->controls_playback.pos = 0;
    new_player->controls_playback.size = 0;
    new_player->last_frames_playback_pos = 1;

    new_player->local_stream_spot.pos = -1;
    new_player->local_stream_spot.frame = -1;

    SDL_AtomicSet(&new_player->position_sync_countdown, 0);
    SDL_AtomicSet(&new_player->sync_on_frame, -1);

    return new_player;
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

void read_guy_info_from_buffer(byte* buffer, Character* guy, int* pos) {
    read_from_buffer(buffer, guy->position.x, pos, sizeof(guy->position.x));
    memcpy(guy->old_position.x, guy->position.x, sizeof(guy->position.x));
    read_from_buffer(buffer, &guy->flip, pos, sizeof(guy->flip));
    read_from_buffer(buffer, &guy->body_color, pos, sizeof(SDL_Color) * 3);
}

void write_guy_info_to_buffer(byte* buffer, Character* guy, int* pos) {
    write_to_buffer(buffer, guy->position.x, pos, sizeof(guy->position.x));
    write_to_buffer(buffer, &guy->flip, pos, sizeof(guy->flip));
    write_to_buffer(buffer, &guy->body_color, pos, sizeof(guy->body_color) * 3);
}

// balls
RemotePlayer* netop_initialize_player(TestScene* scene, struct sockaddr_in* addr) {
    int pos = 1;

    int player_count;
    read_from_buffer(scene->net.buffer, &player_count, &pos, sizeof(player_count));
    SDL_assert(player_count >= 0);

    RemotePlayer* first_player = NULL;

    for (int i = 0; i < player_count; i++) {
        int player_id = (int)scene->net.buffer[pos++];
        RemotePlayer* player = player_of_id(scene, player_id, addr);

        if (player == NULL) {
            if (addr == NULL) {
                SDL_assert(scene->net.status == JOINING);
                player = allocate_new_player(player_id, &scene->net.server_address);
                scene->net.players[scene->net.number_of_players++] = player;
            }
            else {
                SET_LOCKED_STRING_F(scene->net.status_message, "No player of id %i", player_id);
                return NULL;
            }
        }
        if (i == 0)
            first_player = player;

        read_guy_info_from_buffer(scene->net.buffer, &player->guy, &pos);
    }

    return first_player;
}

RemotePlayer* netop_update_controls(TestScene* scene, struct sockaddr_in* addr, int bufsize) {
    int pos = 1; // [0] is opcode, which we know by now
    int player_id = (int)scene->net.buffer[pos++];

    RemotePlayer* player = player_of_id(scene, player_id, addr);
    if (player == NULL)
        return NULL;

    int new_playback_size;
    read_from_buffer(scene->net.buffer, &new_playback_size, &pos, sizeof(new_playback_size));
    SDL_assert(new_playback_size >= 0);

    // At this point, pos should be at the first byte of the playback buffer (# frames)
    // (also new_playback_size would be 0 but keeping it like this for now)
    if (scene->net.buffer[pos] == 0)
        return player;

    wait_for_then_use_lock(player->controls_playback.locked);
    byte* new_playback_buffer = scene->net.buffer + pos;

    if (
        player->controls_playback.current_frame >= 0 &&
        player->controls_playback.pos < player->controls_playback.size &&
        player->controls_playback.current_frame < player->controls_playback.bytes[0]
    ) {
        // Append new buffer data to current playback buffer
        int old_buffer_size = player->controls_playback.size;

        if ((int)new_playback_buffer[0] + (int)player->controls_playback.bytes[0] >= 255) {
            // Truncate old data from playback buffer
            if (scene->net.status == HOSTING) {
                int lowest_frame = player->controls_playback.current_frame;
                int lowest_pos   = player->controls_playback.pos;
                int id_of_lowest = -1;
                for (int i = 0; i < scene->net.number_of_players; i++) {
                    RemotePlayer* player_of_spot = scene->net.players[i];
                    if (player_of_spot != NULL && player_of_spot->id != player->id) {
                        ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[player->id];
                        if (spot->frame < lowest_frame && spot->frame >= 0) {
                            lowest_frame = spot->frame;
                            lowest_pos   = spot->pos;
                            id_of_lowest = player_of_spot->id;
                        }
                    }
                }

                struct BufferChanges change = truncate_controls_buffer(&player->controls_playback, lowest_frame, lowest_pos);

                old_buffer_size = player->controls_playback.size;
                SDL_assert(old_buffer_size >= 0);

                for (int i = 0; i < scene->net.number_of_players; i++) {
                    RemotePlayer* player_of_spot = scene->net.players[i];
                    if (player_of_spot != NULL && player_of_spot->id != player->id) {
                        ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[player->id];
                        if (spot->frame >= 0) {
                            spot->frame -= change.change_in_bytes0;
                            spot->pos -= change.change_in_size;
                            SDL_assert(spot->frame >= 0);
                            SDL_assert(spot->pos > 0);
                            printf("actually truncated %i's position in %i's buffer. LOWEST: %i\n", player_of_spot->id, player->id, id_of_lowest);
                        }
                        else {
                            printf("WANTED TO TRUNC %i's position in %i's buffer. LOWEST: %i\n", player_of_spot->id, player->id, id_of_lowest);
                        }
                    }
                }
            }
            else {
                old_buffer_size = simple_truncate_controls_buffer(&player->controls_playback, 0, 1);
            }

            // If this assertion fails, everything's fucked.
            SDL_assert(player->controls_playback.size > player->controls_playback.bytes[0]);
            scene->dbg_last_action = "Truncate";
        }
        else
            scene->dbg_last_action = "Append";
        // Proceed with append
        player->controls_playback.size += new_playback_size - 1;
        player->controls_playback.bytes[0] += new_playback_buffer[0];
        // We keep offsetting new_playback_size by -1 because the first element is the number of frames recorded.
        memcpy(player->controls_playback.bytes + old_buffer_size, new_playback_buffer + 1, new_playback_size - 1);
    }
    else {
        // Set playback buffer and start from the beginning
        int original_size = player->controls_playback.size;
        int original_bytes0 = player->controls_playback.bytes[0];

        player->controls_playback.current_frame = 0;
        player->controls_playback.pos = 1;
        player->controls_playback.size = new_playback_size;
        memcpy(player->controls_playback.bytes, new_playback_buffer, new_playback_size);

        int change_in_size   = original_size - player->controls_playback.size;
        int change_in_bytes0 = original_bytes0 - player->controls_playback.bytes[0];

        if (scene->net.status == HOSTING) {
            for (int i = 0; i < scene->net.number_of_players; i++) {
                RemotePlayer* player_of_spot = scene->net.players[i];
                if (player_of_spot != NULL && player_of_spot->id != player->id) {
                    ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[player->id];
                    if (spot->frame >= 0) {
                        spot->frame -= change_in_bytes0;
                        spot->pos   -= change_in_size;
                        SDL_assert(spot->frame >= 0);
                        SDL_assert(spot->pos > 0);
                    }
                    else {
                    }
                }
            }
        }

        scene->dbg_last_action = "Reset";
    }

    pos += new_playback_size;
    if (pos < bufsize) {
        byte frames_down_from_bytes0 = scene->net.buffer[pos++];
        int frame_of_position = (int)(player->controls_playback.bytes[0] - frames_down_from_bytes0);
        SDL_assert(frame_of_position <= player->controls_playback.bytes[0]);

        read_from_buffer(scene->net.buffer, player->sync_position.x, &pos, sizeof(vec4));
        read_from_buffer(scene->net.buffer, player->sync_old_position.x, &pos, sizeof(vec4));
        SDL_AtomicSet(&player->sync_on_frame, frame_of_position);

        sync_player_frame_if_should(scene->net.status, player);
    }

    SDL_UnlockMutex(player->controls_playback.locked);

    return player;
}

#define netwrite_guy_controls(buffer, player_id, controls_stream, spot, already_locked) \
    netwrite_guy_controls_and_position(buffer, player_id, controls_stream, spot, already_locked, NULL, NULL)

int netwrite_guy_controls_and_position(
    byte*               buffer,
    int                 player_id,
    ControlsBuffer*     controls_stream,
    ControlsBufferSpot* spot,
    bool                already_locked,
    vec4* position, vec4* old_position
) {
    memset(buffer, 0, PACKET_SIZE);
    int pos = 0;
    buffer[pos++] = NETOP_UPDATE_CONTROLS;
    buffer[pos++] = player_id;

    if (controls_stream->current_frame < 0) {
        int zero = 0;
        write_to_buffer(buffer, &zero, &pos, sizeof(zero));
        buffer[pos++] = 0;
        return pos;
    }

    if (!already_locked)
        wait_for_then_use_lock(controls_stream->locked);

    byte* buffer_to_copy_over;
    int buffer_to_copy_over_size;

    if (spot != NULL) {
        buffer_to_copy_over      = controls_stream->bytes + spot->pos;
        buffer_to_copy_over_size = controls_stream->size  - spot->pos;
        int actual_size = buffer_to_copy_over_size + 1;

        SDL_assert(buffer_to_copy_over_size >= 0);
        SDL_assert(buffer_to_copy_over_size < PACKET_SIZE);

        write_to_buffer(buffer, &actual_size, &pos, sizeof(actual_size));
        int original_pos = pos;
        pos += 1;
        write_to_buffer(buffer, buffer_to_copy_over, &pos, buffer_to_copy_over_size);
        buffer[original_pos] = controls_stream->bytes[0] - spot->frame;
        SDL_assert(buffer[original_pos] >= 0);

        spot->pos   = controls_stream->size;
        spot->frame = controls_stream->bytes[0];
    }
    else {
        // Then it's the controls stream (where current_pos == size and such)
        buffer_to_copy_over      = controls_stream->bytes;
        buffer_to_copy_over[0]   = controls_stream->current_frame;
        buffer_to_copy_over_size = controls_stream->pos;

        SDL_assert(buffer_to_copy_over_size < PACKET_SIZE);

        controls_stream->pos = 1;
        controls_stream->current_frame = 0;

        write_to_buffer(buffer, &buffer_to_copy_over_size, &pos, sizeof(buffer_to_copy_over_size));
        write_to_buffer(buffer, buffer_to_copy_over, &pos, buffer_to_copy_over_size);
    }

    if (position && old_position) {
        buffer[pos++] = controls_stream->bytes[0] - (byte)controls_stream->current_frame;
        write_to_buffer(buffer, position->x, &pos, sizeof(vec4));
        write_to_buffer(buffer, old_position->x, &pos, sizeof(vec4));
    }

    if (!already_locked)
        SDL_UnlockMutex(controls_stream->locked);

    return pos;
}

int netwrite_guy_position(TestScene* scene) {
    SDL_assert(scene->net.remote_id >= 0);
    SDL_assert(scene->net.remote_id < 255);

    memset(scene->net.buffer, 0, PACKET_SIZE);
    int pos = 0;
    scene->net.buffer[pos++] = NETOP_INITIALIZE_PLAYER;

    int number_of_players = 1;
    write_to_buffer(scene->net.buffer, &number_of_players, &pos, sizeof(number_of_players));

    scene->net.buffer[pos++] = (byte)scene->net.remote_id;
    write_guy_info_to_buffer(scene->net.buffer, &scene->guy, &pos);

    return pos;
}

int netwrite_player_positions(TestScene* scene, RemotePlayer* target_player) {
    // Only server retains authoritative copies of all player positions.
    SDL_assert(scene->net.status == HOSTING);

    int pos = 0;
    scene->net.buffer[pos++] = NETOP_INITIALIZE_PLAYER;
    // number_of_players - 1 (excluding target) + 1 (including local server player)
    write_to_buffer(scene->net.buffer, &scene->net.number_of_players, &pos, sizeof(scene->net.number_of_players));

    scene->net.buffer[pos++] = (byte)scene->net.remote_id;
    write_guy_info_to_buffer(scene->net.buffer, &scene->guy, &pos);

    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* player = scene->net.players[i];
        if (player == NULL || player->id == target_player->id)
            continue;

        scene->net.buffer[pos++] = (byte)player->id;
        write_guy_info_to_buffer(scene->net.buffer, &player->guy, &pos);
    }

    return pos;
}

int server_sendto(TestScene* scene, int bytes_wrote, struct sockaddr_in* other_address) {
    if (bytes_wrote > 0) {
        sendto(
            scene->net.local_socket,
            scene->net.buffer,
            bytes_wrote,
            0,
            (struct sockaddr*)other_address,
            sizeof(struct sockaddr_in)
        );
    }
}

int network_server_loop(void* vdata) {
    int r = 0;
    TestScene* scene = (TestScene*)vdata;
    scene->net.buffer = aligned_malloc(PACKET_SIZE);

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

    wait_for_then_use_lock(scene->controls_stream.locked);
    scene->controls_stream.current_frame = 0;
    scene->controls_stream.pos = 1;
    SDL_UnlockMutex(scene->controls_stream.locked);

    // NOTE do not exit this scene after initializing network lol
    while (true) {
        memset(scene->net.buffer, 0, PACKET_SIZE);

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

            int pos = 0;
            scene->net.buffer[pos++] = NETOP_HERES_YOUR_ID;
            scene->net.buffer[pos++] = (byte)new_id;
            for (int i = 0; i < scene->net.number_of_players; i++)
                scene->net.buffer[pos++] = (byte)scene->net.players[i]->id;

            server_sendto(scene, pos, &other_address);
            scene->net.players[scene->net.number_of_players++] = player;
        } break;

        case NETOP_INITIALIZE_PLAYER: {
            /*
            for (int i = 0; i < s->net.number_of_players; i++) {
                RemotePlayer* plr = s->net.players[i];
                */
            scene->net.connected = true;

            RemotePlayer* new_player = netop_initialize_player(scene, &other_address);
            if (new_player) {
                wait_for_then_use_lock(scene->controls_stream.locked);
                new_player->local_stream_spot.pos   = scene->controls_stream.pos;
                new_player->local_stream_spot.frame = scene->controls_stream.current_frame;
                SDL_UnlockMutex(scene->controls_stream.locked);

                server_sendto(scene, netwrite_player_positions(scene, new_player), &other_address);

                // Gotta tell those other players that this guy is joining... And set stream positions.
                for (int i = 0; i < scene->net.number_of_players; i++) {
                    RemotePlayer* other_player = scene->net.players[i];
                    if (other_player == NULL || other_player->id == new_player->id)
                        continue;

                    other_player->stream_spots_of[new_player->id].frame = 0;
                    other_player->stream_spots_of[new_player->id].pos   = 1;

                    wait_for_then_use_lock(other_player->controls_playback.locked);
                    new_player->stream_spots_of[other_player->id].frame = other_player->controls_playback.bytes[0];
                    new_player->stream_spots_of[other_player->id].pos   = other_player->controls_playback.size;
                    SDL_UnlockMutex(other_player->controls_playback.locked);

                    server_sendto(scene, netwrite_player_positions(scene, other_player), &other_player->address);
                }
            }
        } break;

        case NETOP_UPDATE_CONTROLS: {
            RemotePlayer* player = netop_update_controls(scene, &other_address, recv_len);
            if (player) {
                SDL_AtomicSet(&player->poke, true);
                server_sendto(
                    scene,
                    netwrite_guy_controls(
                        scene->net.buffer,
                        scene->net.remote_id,
                        &scene->controls_stream,
                        &player->local_stream_spot,
                        false
                    ),
                    &other_address
                );
                // If there's more than just this guy playing with us, we need to send them the controls
                // of all other players..
                if (scene->net.number_of_players > 1) {
                    for (int i = 0; i < scene->net.number_of_players; i++) {
                        RemotePlayer* other_player = scene->net.players[i];
                        if (other_player->id == player->id)
                            continue;

                        ControlsBufferSpot* players_spot_of_other_player = &player->stream_spots_of[other_player->id];
                        wait_for_then_use_lock(other_player->controls_playback.locked);
                        if (
                            other_player->controls_playback.size > 0 &&
                            players_spot_of_other_player->frame >= 0 &&
                            players_spot_of_other_player->pos < other_player->controls_playback.size &&
                            players_spot_of_other_player->frame < other_player->controls_playback.bytes[0]
                        ) {
                            vec4* position = NULL,* old_position = NULL;
                            if (SDL_AtomicGet(&other_player->position_sync_countdown) <= 0) {
                                position     = &other_player->guy.position;
                                old_position = &other_player->guy.old_position;
                                SDL_AtomicSet(&other_player->position_sync_countdown, FRAMES_BETWEEN_POSITION_SYNCS);
                            }

                            server_sendto(
                                scene,
                                netwrite_guy_controls_and_position(
                                    scene->net.buffer,
                                    other_player->id,
                                    &other_player->controls_playback,
                                    players_spot_of_other_player,
                                    true,
                                    position, old_position
                                ),
                                &other_address
                            );
                        }
                        else {
                            // printf("didn't end up sending %i's controls to %i\n", other_player->id, player->id);
                        }
                        SDL_UnlockMutex(other_player->controls_playback.locked);
                    }
                }
            }
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
    scene->net.buffer = aligned_malloc(PACKET_SIZE);

    SET_LOCKED_STRING(scene->net.status_message, "Connecting to server!");

    struct sockaddr_in other_address;

    memset((char *) &other_address, 0, sizeof(other_address));
    other_address.sin_family = AF_INET;
    other_address.sin_addr.s_addr = inet_addr(scene->net.textinput_ip_address);
    other_address.sin_port = htons(atoi(scene->net.textinput_port));
    connect(scene->net.local_socket, (struct sockaddr*)&other_address, sizeof(other_address));

    scene->net.server_address = other_address;

    scene->net.remote_id = -1;
    scene->net.next_id = -1;
    bool first_send = true;

    while (true) {
        int send_result;

        // CLIENT
        if (scene->net.remote_id == -1) {
            // Start updating controls, then send over the ID request.
            wait_for_then_use_lock(scene->controls_stream.locked);
            scene->controls_stream.current_frame = 0;
            scene->controls_stream.pos = 1;
            SDL_UnlockMutex(scene->controls_stream.locked);

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
            int size = netwrite_guy_controls(
                scene->net.buffer,
                scene->net.remote_id,
                &scene->controls_stream,
                NULL, false
            );
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


        Receive:
        {
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
            case NETOP_HERES_YOUR_ID: {
                scene->net.remote_id = scene->net.buffer[1];
                // Allocate players for when update_position comes in
                scene->net.players[scene->net.number_of_players++] = allocate_new_player(0, &other_address);
                for (int i = 2; i < recv_len; i++) {
                    // NOTE as client I guess all `other_address`es will just be the server's address..
                    scene->net.players[scene->net.number_of_players++] = allocate_new_player(scene->net.buffer[i], &other_address);
                }
                scene->net.connected = true;
                SET_LOCKED_STRING_F(scene->net.status_message, "Client ID: %i", scene->net.remote_id);
            } break;

            case NETOP_INITIALIZE_PLAYER:
                netop_initialize_player(scene, NULL);
                break;

            case NETOP_UPDATE_CONTROLS: {
                RemotePlayer* player = netop_update_controls(scene, NULL, recv_len);
                if (player == NULL) {
                    printf("UPDATING FORF BULLSHIT PLAYER\n");
                }
                else {
                    SDL_AtomicSet(&player->poke, true);
                    if (player->id != 0) {
                        // Don't need to bounce back for updating non-server player's controls
                        goto Receive;
                    }
                }
            } break;

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

    data->controls_stream.current_frame = -1;
    data->controls_stream.pos = 1;

    // NETWORKING TIME
    {
        data->net.remote_id = -1;
        data->net.number_of_players = 0;
        memset(data->net.players, 0, sizeof(data->net.players));

        data->net.connected = false;
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
    data->controls_stream.locked = SDL_CreateMutex();
    if (!data->controls_stream.locked) {
        printf("BAD CTRLS STREAM MUTEX\n");
    }

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

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;

    // I'm only gonna count ping for first guy I don't really care right now
    {
        s->net.ping_counter += 1;
        RemotePlayer* player_of_ping = s->net.players[0];
        if (player_of_ping != NULL) {
            if (SDL_AtomicGet(&player_of_ping->poke)) {
                s->net.ping = s->net.ping_counter;
                s->net.ping_counter = 0;
                SDL_AtomicSet(&player_of_ping->poke, false);
            }
        }
    }
    // Decrement counters
    {
        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player != NULL)
                SDL_AtomicAdd(&player->position_sync_countdown, -1);
        }
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
    // Have net players playback controls
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* plr = s->net.players[i];
        if (plr == NULL)
            continue;

        // TODO skip to the next guy and revisit if locked!
        wait_for_then_use_lock(plr->controls_playback.locked);
        plr->number_of_physics_updates_this_frame = 0;

        if (
            plr->controls_playback.current_frame >= 0                          &&
            plr->controls_playback.pos           < plr->controls_playback.size &&
            plr->controls_playback.current_frame < plr->controls_playback.bytes[0]
        ) {
            // Do 2 frames at a time if we're so many frames behind
            int frames_behind = (int)plr->controls_playback.bytes[0] - plr->controls_playback.current_frame;
            // TODO ping for each guy >_>
            const int total_ping = s->net.ping * s->net.number_of_players;
            const int frames_behind_threshold = total_ping + total_ping / 2;

            if (frames_behind > frames_behind_threshold) {
                printf("Network control sync behind by %i frames\n", frames_behind);
                plr->number_of_physics_updates_this_frame = 2;
            }
            else
                plr->number_of_physics_updates_this_frame = 1;

            for (int i = 0; i < plr->number_of_physics_updates_this_frame; i++) {
                if (i > 0) // After the first update, we're doing consecutive ones and we need to "end" the frame.
                    net_character_post_update(plr);

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
                // pos is now on that last CONTROL_BLOCK_END
                plr->controls_playback.pos += 1;
                plr->controls_playback.current_frame += 1;

                SDL_assert(plr->number_of_physics_updates_this_frame == 1 || plr->number_of_physics_updates_this_frame == 2);

                apply_character_physics(game, &plr->guy, &plr->controls, s->gravity, s->drag);
                collide_character(&plr->guy, &s->map->tile_collision);
                slide_character(s->gravity, &plr->guy);
                update_character_animation(&plr->guy);

                sync_player_frame_if_should(s->net.status, plr);

                if (plr->controls_playback.current_frame >= plr->controls_playback.bytes[0]) {
                    // TODO TODO TODO THIS GETS TRIPPED SOMETIMES!!!!!!!!!!!!
                    // SDL_assert(plr->controls_playback.pos == plr->controls_playback.size);
                    if (plr->controls_playback.pos != plr->controls_playback.size) {
                        // uhhhh
                        int original_size = plr->controls_playback.size;
                        plr->controls_playback.size = plr->controls_playback.pos;
                        printf("ADJUSTED %i's PLAYBACK SIZE\n", plr->id);

                        if (s->net.status == HOSTING) {
                            int change_in_size = original_size - plr->controls_playback.size;

                            for (int i = 0; i < s->net.number_of_players; i++) {
                                RemotePlayer* player_of_spot = s->net.players[i];
                                if (player_of_spot != NULL && player_of_spot->id != plr->id) {
                                    ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[plr->id];
                                    if (spot->frame >= 0) {
                                        spot->pos   -= change_in_size;
                                        SDL_assert(spot->frame >= 0);
                                        SDL_assert(spot->pos > 0);
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
        SDL_UnlockMutex(plr->controls_playback.locked);
    }

    // Update local player
    apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);
    update_character_animation(&s->guy);

    // Follow local player with camera
    {
        Character* guy = &s->guy; 

        game->camera_target.x[X] = guy->position.x[X] - game->window_width / 2.0f;
        if (guy->grounded) {
            game->camera_target.x[Y] = guy->position.x[Y] - game->window_height * 0.35f;
            game->follow_cam_y = false;
        }
        else {
            if (guy->position.x[Y] - game->camera_target.x[Y] < 1.5f)
                game->follow_cam_y = true;
            if (game->follow_cam_y)
                game->camera_target.x[Y] = guy->position.x[Y] - game->window_height * 0.5f;
        }
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
    wait_for_then_use_lock(s->controls_stream.locked);

    if (s->controls_stream.current_frame >= 0) {
        // record
        for (enum Control ctrl = 0; ctrl < NUM_CONTROLS; ctrl++) {
            if (game->controls.this_frame[ctrl])
                s->controls_stream.bytes[s->controls_stream.pos++] = ctrl;
        }
        s->controls_stream.bytes[s->controls_stream.pos++] = CONTROL_BLOCK_END;
        SDL_assert(s->controls_stream.pos < CONTROLS_BUFFER_SIZE);

        s->controls_stream.current_frame += 1;

        s->controls_stream.size = s->controls_stream.pos;
        s->controls_stream.bytes[0] = s->controls_stream.current_frame;

        if (s->controls_stream.current_frame >= 255) {
            SDL_assert(s->net.status == HOSTING); // TODO OTHERWISE THIS MEANS SERVER CRASHED!

            if (s->net.number_of_players == 0) {
                s->controls_stream.current_frame = 0;
                s->controls_stream.pos = 1;
            }
            else {
                // ==== Truncate the controls stream such that the lowest remote-player position is 0 ====
                int lowest_frame = 255;
                int lowest_pos = PACKET_SIZE;
                for (int i = 0; i < s->net.number_of_players; i++) {
                    RemotePlayer* player = s->net.players[i];
                    if (player != NULL) {
                        if (player->local_stream_spot.frame < lowest_frame && player->local_stream_spot.frame >= 0) {
                            lowest_frame = player->local_stream_spot.frame;
                            lowest_pos   = player->local_stream_spot.pos;
                        }
                    }
                }

                struct BufferChanges change = truncate_controls_buffer(&s->controls_stream, lowest_frame, lowest_pos);

                for (int i = 0; i < s->net.number_of_players; i++) {
                    RemotePlayer* player = s->net.players[i];
                    if (player != NULL) {
                        if (player->local_stream_spot.frame >= 0) {
                            player->local_stream_spot.frame -= change.change_in_bytes0;
                            player->local_stream_spot.pos   -= change.change_in_size;
                            SDL_assert(player->local_stream_spot.frame >= 0);
                            SDL_assert(player->local_stream_spot.pos > 0);
                        }
                    }
                }
            }
        }
    }
    else if (s->controls_stream.current_frame < -1) {
        // NOTE this was from displaying the old "Recording complete!" message.
        // Can remove probably.
        s->controls_stream.current_frame += 1;
    }
    SDL_UnlockMutex(s->controls_stream.locked);

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    character_post_update(&s->guy);

    // Perform last update on all net guys
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* plr = s->net.players[i];
        if (plr != NULL && plr->number_of_physics_updates_this_frame > 0)
            net_character_post_update(plr);
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
        set_text_color(game, 100, 50, 255);
        draw_text(game, 10, game->window_height - 50, s->net.status_message);
    }

    draw_text_ex_f(game, game->window_width - 150, game->window_height - 40, -1, 0.7f, "Ping: %i", s->net.ping);
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;

    closesocket(data->net.local_socket);
    if (data->net.buffer)
        aligned_free(data->net.buffer);
}
