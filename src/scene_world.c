#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <arpa/inet.h>
#define WSAGetLastError() (-1)
#define SOCKET_ERROR (-1)
#define SOCKET int
#define closesocket close
#endif

#include "script.h"
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

extern SDL_Window* main_window;

#define EDITABLE_TEXT_BUFFER_SIZE 255
#define PACKET_SIZE 2000
#define CONTROLS_BUFFER_SIZE (255 * NUM_CONTROLS)
#define CONTROL_BLOCK_END NUM_CONTROLS
#define MAX_PLAYERS 20
#define MOB_EVENT_BUFFER_SIZE (1024 * 3)

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

typedef struct PhysicsState {
    vec4 position;
    vec4 old_position;
    int flip;
    float dy;
    float ground_speed;
} PhysicsState;

typedef struct RemotePlayer {
    int id;
    Controls controls;
    int number_of_physics_updates_this_frame;
    SDL_atomic_t area_id;
    SDL_atomic_t just_switched_maps;
    Character guy;
    ControlsBufferSpot local_stream_spot;
    // NOTE this IS indexed by player ID - scene.net.players is not.
    ControlsBufferSpot stream_spots_of[MAX_PLAYERS];
    struct sockaddr_in address;
    SOCKET socket;
    int ping, ping_counter;
    SDL_atomic_t poke;

    // Countdown until we SEND this guy's position (as server) (ALSO INDEXED BY ID)
    SDL_atomic_t countdown_until_i_get_position_of[MAX_PLAYERS];
    // Position we have RECEIVED for this guy and plan to set (if we so choose) (i.e. if we are client)
    PhysicsState sync;
    SDL_atomic_t frames_until_sync;

    SDL_mutex* mob_event_buffer_locked;
    byte mob_event_buffer[MOB_EVENT_BUFFER_SIZE];
    int mob_event_buffer_pos;
    int mob_event_count;

    int last_frames_playback_pos;
    ControlsBuffer controls_playback;
} RemotePlayer;

typedef struct WorldScene {
    Game* game;
    float gravity;
    float drag;
    Character guy;
    CharacterView guy_view;
    AudioWave* music;
    AudioWave* test_sound;
    int current_area;
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

        // Array of malloced RemotePlayers
        int number_of_players;
        RemotePlayer* players[MAX_PLAYERS];
    } net;
} WorldScene;

typedef struct ServerLoop {
    SOCKET socket;
    struct sockaddr_in address;
    WorldScene* scene;
} ServerLoop;

void net_character_post_update(RemotePlayer* plr) {
    character_post_update(&plr->guy);
    plr->last_frames_playback_pos = plr->controls_playback.pos;
}

int frames_between_position_syncs(RemotePlayer* plr) {
    return max(60, plr->ping + plr->ping / 2);
}

void sync_player_frame_if_should(int status, RemotePlayer* plr) {
    if (status != JOINING)
        return;
    if (SDL_AtomicGet(&plr->frames_until_sync) < 0)
        return;

    if (SDL_AtomicGet(&plr->frames_until_sync) == 0) {
        plr->guy.position.simd = plr->sync.position.simd;
        plr->guy.old_position  = plr->sync.old_position;
        plr->guy.flip          = plr->sync.flip;
        plr->guy.dy            = plr->sync.dy;
        plr->guy.ground_speed  = plr->sync.ground_speed;

        // printf("SYNCED POSITION OF %i\n", plr->id);
    }
    SDL_AtomicAdd(&plr->frames_until_sync, -1);
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
    SDL_AtomicSet(&new_player->area_id, -1);
    SDL_AtomicSet(&new_player->just_switched_maps, false);

    default_character(&new_player->guy);
    new_player->guy.player_id = id;
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

    new_player->mob_event_buffer_pos = 0;
    new_player->mob_event_count = 0;
    new_player->mob_event_buffer_locked = SDL_CreateMutex();
    if (!new_player->mob_event_buffer_locked) {
        printf("BAD MUTEX\n");
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
        SDL_AtomicSet(&new_player->countdown_until_i_get_position_of[i], 0);
    SDL_AtomicSet(&new_player->frames_until_sync, -1);

    return new_player;
}

RemotePlayer* player_of_id(WorldScene* scene, int id, struct sockaddr_in* addr) {
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

void read_guy_info_from_buffer(byte* buffer, Character* guy, int* area_id, int* pos) {
    read_from_buffer(buffer, guy->position.x, pos,  sizeof(guy->position.x));
    memcpy(guy->old_position.x, guy->position.x,    sizeof(guy->position.x));
    read_from_buffer(buffer, &guy->flip, pos,       sizeof(guy->flip));
    read_from_buffer(buffer, &guy->body_color, pos, sizeof(SDL_Color) * 3);
    read_from_buffer(buffer, &guy->eye_type,   pos, sizeof(guy->eye_type));
    read_from_buffer(buffer, &guy->eye_color,  pos, sizeof(SDL_Color));
    read_from_buffer(buffer, area_id,          pos, sizeof(int));
}

void write_guy_info_to_buffer(byte* buffer, Character* guy, int area_id, int* pos) {
    write_to_buffer(buffer, guy->position.x,  pos, sizeof(guy->position.x));
    write_to_buffer(buffer, &guy->flip,       pos, sizeof(guy->flip));
    write_to_buffer(buffer, &guy->body_color, pos, sizeof(guy->body_color) * 3);
    write_to_buffer(buffer, &guy->eye_type,   pos, sizeof(guy->eye_type));
    write_to_buffer(buffer, &guy->eye_color,  pos, sizeof(guy->eye_color));
    write_to_buffer(buffer, &area_id,         pos, sizeof(int));
}

// balls
RemotePlayer* netop_initialize_state(WorldScene* scene, byte* buffer, struct sockaddr_in* addr) {
    int pos = 1;

    int player_count;
    read_from_buffer(buffer, &player_count, &pos, sizeof(player_count));
    SDL_assert(player_count >= 0);
    int map_count;
    read_from_buffer(buffer, &map_count, &pos, sizeof(map_count));
    SDL_assert(map_count == 0 || scene->net.status == JOINING);

    RemotePlayer* first_player = NULL;

    // Read players
    for (int i = 0; i < player_count; i++) {
        int player_id = (int)buffer[pos++];
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

        int area_id = SDL_AtomicGet(&player->area_id);
        read_guy_info_from_buffer(buffer, &player->guy, &area_id, &pos);
        SDL_AtomicSet(&player->area_id, area_id);
    }

    // Read maps
    for (int i = 0; i < map_count; i++) {
        int area_id;
        read_from_buffer(buffer, &area_id, &pos, sizeof(area_id));
        SDL_assert(area_id >= 0);
        int map_asset = map_asset_for_area(area_id);
        SDL_assert(map_asset >= 0);
        Map* map = cached_map(scene->game, map_asset);
        map->area_id = area_id;

        clear_map_state(map);
        read_map_state(map, buffer, &pos);
    }

    return first_player;
}

int truncate_buffer_to_lowest_spot(WorldScene* scene, RemotePlayer* player) {
    int lowest_frame = player->controls_playback.current_frame;
    int lowest_pos = player->controls_playback.pos;
    int id_of_lowest = -1;
    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* player_of_spot = scene->net.players[i];
        if (player_of_spot != NULL && player_of_spot->id != player->id) {
            ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[player->id];
            if (spot->frame < lowest_frame && spot->frame >= 0) {
                lowest_frame = spot->frame;
                lowest_pos = spot->pos;
                id_of_lowest = player_of_spot->id;
            }
        }
    }

    struct BufferChanges change = truncate_controls_buffer(&player->controls_playback, lowest_frame, lowest_pos);

    SDL_assert(player->controls_playback.size >= 0);

    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* player_of_spot = scene->net.players[i];
        if (player_of_spot != NULL && player_of_spot->id != player->id) {
            ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[player->id];
            if (spot->frame >= 0) {
                spot->frame -= change.change_in_bytes0;
                spot->pos -= change.change_in_size;
                SDL_assert(spot->frame >= 0);
                SDL_assert(spot->pos >= 0);
                // printf("actually truncated %i's position in %i's buffer. LOWEST: %i\n", player_of_spot->id, player->id, id_of_lowest);
            }
            else {
                printf("WANTED TO TRUNC %i's position in %i's buffer. LOWEST: %i\n", player_of_spot->id, player->id, id_of_lowest);
            }
        }
    }

    return player->controls_playback.size;
}

// If multiple of these flags comes in an update, their content must be in this order.
#define NETF_CONTROLS (1 << 0)
#define NETF_AREA     (1 << 1)
#define NETF_POSITION (1 << 2)
#define NETF_MAPSTATE (1 << 3)
#define NETF_MOBEVENT (1 << 4)

#define NETF_MOBEVENT_SPAWN 0
#define NETF_MOBEVENT_UPDATE 1

RemotePlayer* netop_update_controls(WorldScene* scene, byte* buffer, struct sockaddr_in* addr, int bufsize) {
    int pos = 1; // [0] is opcode, which we know by now

    RemotePlayer* first_player = NULL;
    while (pos < bufsize) {
        int player_id = (int)buffer[pos++];

        RemotePlayer* player = player_of_id(scene, player_id, addr);
        if (player == NULL) {
            printf("WARNING: Just got an update op with an unknown player %i\n", player_id);
            return first_player;
        }
        else if (first_player == NULL)
            first_player = player;

        byte flags = buffer[pos++];

        wait_for_then_use_lock(player->controls_playback.locked);

        if (flags & NETF_CONTROLS) {
            int new_playback_size;
            read_from_buffer(buffer, &new_playback_size, &pos, sizeof(new_playback_size));
            SDL_assert(new_playback_size >= 0);

            // At this point, pos should be at the first byte of the playback buffer (# frames), which should also == 0.
            if (new_playback_size <= 0)
                goto DoneWithControls;

            byte* new_playback_buffer = buffer + pos;

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
                        old_buffer_size = truncate_buffer_to_lowest_spot(scene, player);
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
                // Set playback buffer and start from the beginning (or lowest spot)
                if (scene->net.status == HOSTING && player->controls_playback.current_frame >= 0) {
                    int truncated_size = truncate_buffer_to_lowest_spot(scene, player);

                    player->controls_playback.size += new_playback_size - 1;
                    player->controls_playback.bytes[0] += new_playback_buffer[0];
                    // We keep offsetting new_playback_size by -1 because the first element is the number of frames recorded.
                    memcpy(player->controls_playback.bytes + truncated_size, new_playback_buffer + 1, new_playback_size - 1);
                }
                else {
                    player->controls_playback.current_frame = 0;
                    player->controls_playback.pos = 1;
                    player->controls_playback.size = new_playback_size;
                    memcpy(player->controls_playback.bytes, new_playback_buffer, new_playback_size);
                }

                scene->dbg_last_action = "Reset";
            }

            pos += new_playback_size;
        }
    DoneWithControls:;

        if (flags & NETF_AREA) {
            int area_id;
            read_from_buffer(buffer, &area_id, &pos, sizeof(int));
            SDL_AtomicSet(&player->area_id, area_id);
        }

        if (flags & NETF_POSITION) {
            signed char frames_down_from_bytes0 = (signed char)buffer[pos++];
            int frame_of_position;
            if (frames_down_from_bytes0 >= 0)
                frame_of_position = (int)(player->controls_playback.bytes[0] - frames_down_from_bytes0);
            else
                frame_of_position = -1;

            SDL_assert(frame_of_position <= player->controls_playback.bytes[0]);

            read_from_buffer(buffer, player->sync.position.x, &pos, sizeof(vec4));
            read_from_buffer(buffer, player->sync.old_position.x, &pos, sizeof(vec4));
            read_from_buffer(buffer, &player->sync.flip, &pos, sizeof(int));
            read_from_buffer(buffer, &player->sync.dy, &pos, sizeof(float));
            read_from_buffer(buffer, &player->sync.ground_speed, &pos, sizeof(int));
            if (frame_of_position == -1)
                SDL_AtomicSet(&player->frames_until_sync, 0);
            else
                SDL_AtomicSet(&player->frames_until_sync, frame_of_position - player->controls_playback.current_frame);

            sync_player_frame_if_should(scene->net.status, player);
        }

        SDL_UnlockMutex(player->controls_playback.locked);

        if (flags & NETF_MAPSTATE) {
            wait_for_then_use_lock(scene->map->locked);
            int area_id;
            read_from_buffer(buffer, &area_id, &pos, sizeof(int));

            SDL_assert(area_id == scene->current_area);
            SDL_assert(area_id == scene->map->area_id);
            clear_map_state(scene->map);
            read_map_state(scene->map, buffer, &pos);

            SDL_UnlockMutex(scene->map->locked);
            printf("PROCESSED MAP STATE EVENT\n");
        }

        if (flags & NETF_MOBEVENT) {
            wait_for_then_use_lock(scene->map->locked);

            int number_of_events;
            read_from_buffer(buffer, &number_of_events, &pos, sizeof(int));
            int total_size_of_events;
            read_from_buffer(buffer, &total_size_of_events, &pos, sizeof(int));
            int area_id;
            read_from_buffer(buffer, &area_id, &pos, sizeof(int));

            if (area_id == scene->current_area) {
                for (int i = 0; i < number_of_events; i++) {
                    byte event_type = buffer[pos++];
                    switch (event_type) {
                    case NETF_MOBEVENT_SPAWN: {
                        int mob_type;
                        read_from_buffer(buffer, &mob_type, &pos, sizeof(int));

                        vec2 position;
                        read_from_buffer(buffer, &position, &pos, sizeof(vec2));

                        int m_id;
                        read_from_buffer(buffer, &m_id, &pos, sizeof(int));

                        MobCommon* mob = mob_from_id(scene->map, m_id);
                        MobType*   reg = &mob_registry[mob_type];
                        mob->mob_type_id = mob_type;
                        mob->index = index_from_mob_id(m_id);
                        reg->initialize(mob, scene->game, scene->map, position);
                        reg->load(mob, scene->map, buffer, &pos);
                    } break;

                    case NETF_MOBEVENT_UPDATE: {
                        int m_id;
                        read_from_buffer(buffer, &m_id, &pos, sizeof(int));

                        MobCommon* mob = mob_from_id(scene->map, m_id);
                        if (mob->mob_type_id == -1) {
                            printf("WARNING: Got a bad mob update event (mob not registered.) mobid=%i\n", m_id);
                        }
                        MobType* reg = &mob_registry[mob->mob_type_id];
                        reg->sync_receive(mob, scene->map, buffer, &pos);
                    } break;

                    default:
                        printf("WARNING: UNKNOWN MOB EVENT %i\n", (int)event_type);
                    }
                }
            }
            else {
                printf("Got mob events for the wrong area woops!!!! Draining buffer...\n");
                buffer += total_size_of_events;
            }

            SDL_UnlockMutex(scene->map->locked);
        }
    }//while (pos < bufsize)

    return first_player;
}

int netwrite_guy_controls(
    byte*               buffer,
    ControlsBuffer*     controls_stream,
    ControlsBufferSpot* spot,
    int*  pos,
    bool  already_locked
) {
    if (controls_stream->current_frame < 0 || controls_stream->size <= 0) {
        int zero = 0;
        write_to_buffer(buffer, &zero, pos, sizeof(zero));
        return *pos;
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

        write_to_buffer(buffer, &actual_size, pos, sizeof(actual_size));
        int original_pos = *pos;
        *pos += 1;
        write_to_buffer(buffer, buffer_to_copy_over, pos, buffer_to_copy_over_size);
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

        write_to_buffer(buffer, &buffer_to_copy_over_size, pos, sizeof(buffer_to_copy_over_size));
        write_to_buffer(buffer, buffer_to_copy_over, pos, buffer_to_copy_over_size);
    }

    if (!already_locked)
        SDL_UnlockMutex(controls_stream->locked);

    return *pos;
}

int netwrite_guy_position(byte* buffer, ControlsBuffer* controls_stream, Character* guy, int* pos) {
    if (controls_stream)
        buffer[*pos] = controls_stream->bytes[0] - (byte)controls_stream->current_frame;
    else
        buffer[*pos] = (signed char)-1;
    *pos += 1;
    write_to_buffer(buffer, guy->position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, guy->old_position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, &guy->flip, pos, sizeof(int));
    write_to_buffer(buffer, &guy->dy, pos, sizeof(float));
    write_to_buffer(buffer, &guy->ground_speed, pos, sizeof(float));
    return *pos;
}

int netwrite_guy_area(byte* buffer, int area_id, int* pos) {
    write_to_buffer(buffer, &area_id, pos, sizeof(int));
    return *pos;
}

int netwrite_guy_mobevents(byte* buffer, RemotePlayer* player, int* pos) {
    int area_id = SDL_AtomicGet(&player->area_id);

    SDL_assert(player->mob_event_buffer_pos <= MOB_EVENT_BUFFER_SIZE && player->mob_event_buffer_pos >= 0);

    write_to_buffer(buffer, &player->mob_event_count, pos, sizeof(int));
    write_to_buffer(buffer, &player->mob_event_buffer_pos, pos, sizeof(int));
    write_to_buffer(buffer, &area_id, pos, sizeof(int));
    write_to_buffer(buffer, player->mob_event_buffer, pos, player->mob_event_buffer_pos);

    player->mob_event_buffer_pos = 0;
    player->mob_event_count = 0;

    return *pos;
}

int netwrite_guy_mapstate(byte* buffer, Map* map, int* pos) {
    wait_for_then_use_lock(map->locked);

    write_to_buffer(buffer, &map->area_id, pos, sizeof(int));
    write_map_state(map, buffer, pos);

    SDL_UnlockMutex(map->locked);
}

int netwrite_guy_initialization(WorldScene* scene, byte* buffer) {
    SDL_assert(scene->net.remote_id >= 0);
    SDL_assert(scene->net.remote_id < 255);

    memset(buffer, 0, PACKET_SIZE);
    int pos = 0;
    buffer[pos++] = NETOP_INITIALIZE_PLAYER;

    int number_of_players = 1;
    write_to_buffer(buffer, &number_of_players, &pos, sizeof(number_of_players));
    int zero = 0; // 0 maps being sent
    write_to_buffer(buffer, &zero, &pos, sizeof(zero));

    buffer[pos++] = (byte)scene->net.remote_id;
    write_guy_info_to_buffer(buffer, &scene->guy, scene->current_area, &pos);

    return pos;
}

#define netwrite_player_state(scene, buffer, target_player) netwrite_state(scene, buffer, target_player, 0, NULL)
int netwrite_state(WorldScene* scene, byte* buffer, RemotePlayer* target_player, int map_count, Map** maps) {
    // Only server retains authoritative copies of all player positions.
    SDL_assert(scene->net.status == HOSTING);

    int pos = 0;
    buffer[pos++] = NETOP_INITIALIZE_PLAYER;
    // number_of_players - 1 (excluding target) + 1 (including local server player)
    write_to_buffer(buffer, &scene->net.number_of_players, &pos, sizeof(scene->net.number_of_players));
    // number of maps we're syncing
    write_to_buffer(buffer, &map_count, &pos, sizeof(map_count));

    buffer[pos++] = (byte)scene->net.remote_id;
    write_guy_info_to_buffer(buffer, &scene->guy, scene->current_area, &pos);

    // Write players
    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* player = scene->net.players[i];
        if (player == NULL || player->id == target_player->id)
            continue;

        buffer[pos++] = (byte)player->id;
        write_guy_info_to_buffer(buffer, &player->guy, SDL_AtomicGet(&player->area_id), &pos);
    }

    // Write maps
    for (int i = 0; i < map_count; i++) {
        Map* map = maps[i];

        wait_for_then_use_lock(map->locked);
        write_to_buffer(buffer, &map->area_id, &pos, sizeof(map->area_id));
        write_map_state(map, buffer, &pos);
        SDL_UnlockMutex(map->locked);
    }

    return pos;
}

int network_server_loop(void* vdata) {
    int r = 0;
    ServerLoop* loop = (ServerLoop*)vdata;
    WorldScene* scene = loop->scene;
    byte* buffer = aligned_malloc(PACKET_SIZE);

    while (true) {
        memset(buffer, 0, PACKET_SIZE);

        int other_addr_len = sizeof(loop->address);
        int recv_len = recv(
            loop->socket,
            buffer,
            PACKET_SIZE,
            0
        );

        if (recv_len == SOCKET_ERROR) {
            SET_LOCKED_STRING_F(scene->net.status_message, "Failed to receive: %i", WSAGetLastError());
            scene->net.status = NOT_CONNECTED;
            r = 2; goto Done;
        }
        else if (recv_len == 0) {
            // TODO actual cleanup or something
            SET_LOCKED_STRING(scene->net.status_message, "Connection closed.");
            scene->net.status = NOT_CONNECTED;
            goto Done;
        }

        int bytes_wrote = 0;
        // SERVER
        switch (buffer[0]) {
        case NETOP_HERES_YOUR_ID:
            SDL_assert(!"A client just tried to send us an ID. How dare they.");
            break;

        case NETOP_WANT_ID: {
            SDL_assert(scene->net.next_id < 255);
            int new_id = scene->net.next_id++;
            RemotePlayer* player = allocate_new_player(new_id, &loop->address);
            SDL_AtomicSet(&player->just_switched_maps, true);

            int pos = 0;
            buffer[pos++] = NETOP_HERES_YOUR_ID;
            buffer[pos++] = (byte)new_id;
            for (int i = 0; i < scene->net.number_of_players; i++)
                buffer[pos++] = (byte)scene->net.players[i]->id;

            send(loop->socket, buffer, pos, 0);
            scene->net.players[scene->net.number_of_players++] = player;
        } break;

        case NETOP_INITIALIZE_PLAYER: {
            scene->net.connected = true;

            RemotePlayer* new_player = netop_initialize_state(scene, buffer, &loop->address);
            if (new_player) {
                // Initialize local controls stream positions
                wait_for_then_use_lock(scene->controls_stream.locked);
                new_player->local_stream_spot.pos   = scene->controls_stream.pos;
                new_player->local_stream_spot.frame = scene->controls_stream.current_frame;
                SDL_UnlockMutex(scene->controls_stream.locked);

                // Load their map to send it to them.
                int their_area_id = SDL_AtomicGet(&new_player->area_id);
                Map* their_map = cached_map(scene->game, map_asset_for_area(their_area_id));
                their_map->area_id = their_area_id;

                new_player->socket = loop->socket;
                send(loop->socket, buffer, netwrite_state(scene, buffer, new_player, 1, &their_map), 0);

                SDL_AtomicSet(&new_player->just_switched_maps, false);

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

                    // HAHA! TCP
                    send(other_player->socket, buffer, netwrite_player_state(scene, buffer, other_player), 0);
                }
            }
        } break;

        // SERVER
        case NETOP_UPDATE_CONTROLS: {
            RemotePlayer* player = netop_update_controls(scene, buffer, &loop->address, recv_len);
            if (player) {
                SDL_AtomicSet(&player->poke, true);

                int pos = 0;

                memset(buffer, 0, PACKET_SIZE);
                buffer[pos++] = NETOP_UPDATE_CONTROLS;
                buffer[pos++] = scene->net.remote_id;
                byte* flags = &buffer[pos++];
                *flags = 0;

                int player_area_id = SDL_AtomicGet(&player->area_id);
                if (scene->current_area == player_area_id) {
                    bool send_physics_state = false;

                    *flags |= NETF_CONTROLS;

                    SDL_atomic_t* sync_countdown = &player->countdown_until_i_get_position_of[0];
                    if (SDL_AtomicGet(sync_countdown) <= 0) {
                        send_physics_state = true;
                        SDL_AtomicSet(sync_countdown, frames_between_position_syncs(player));
                        *flags |= NETF_AREA;
                        *flags |= NETF_POSITION;
                        // printf("Sending %i my position!\n", player->id);
                    }
   
                    netwrite_guy_controls(
                        buffer,
                        &scene->controls_stream,
                        &player->local_stream_spot,
                        &pos,
                        false
                    );
                    if (send_physics_state) {
                        netwrite_guy_area(buffer, scene->current_area, &pos);
                        netwrite_guy_position(buffer, &scene->controls_stream, &scene->guy, &pos);
                    }
                }
                else {
                    *flags |= NETF_AREA;
                    *flags |= NETF_POSITION;
                    netwrite_guy_area(buffer, scene->current_area, &pos);
                    netwrite_guy_position(buffer, &scene->controls_stream, &scene->guy, &pos);

                    player->local_stream_spot.frame = scene->controls_stream.current_frame;
                    player->local_stream_spot.pos   = scene->controls_stream.pos;
                }

                bool mob_events_locked = false;
                if (SDL_AtomicGet(&player->just_switched_maps)) {
                    *flags |= NETF_MAPSTATE;

                    int area_id = SDL_AtomicGet(&player->area_id);
                    Map* map = cached_map(scene->game, map_asset_for_area(area_id));
                    map->area_id = area_id;
                    netwrite_guy_mapstate(buffer, map, &pos);

                    SDL_AtomicSet(&player->just_switched_maps, false);
                    wait_for_then_use_lock(player->mob_event_buffer_locked);
                    mob_events_locked = true;

                    player->mob_event_buffer_pos = 0;
                    player->mob_event_count = 0;
                }

                if (player->mob_event_count > 0) {
                    *flags |= NETF_MOBEVENT;
                    // This will never deadlock with above, as mob_event_count will be 0 if it's locked from there.
                    wait_for_then_use_lock(player->mob_event_buffer_locked);
                    mob_events_locked = true;

                    netwrite_guy_mobevents(buffer, player, &pos);
                }

                if (mob_events_locked)
                    SDL_UnlockMutex(player->mob_event_buffer_locked);

                // If there's more than just this guy playing with us, we need to send them the controls
                // of all other players..
                if (scene->net.number_of_players > 1) {
                    for (int i = 0; i < scene->net.number_of_players; i++) {
                        RemotePlayer* other_player = scene->net.players[i];
                        if (other_player->id == player->id)
                            continue;

                        ControlsBufferSpot* players_spot_of_other_player = &player->stream_spots_of[other_player->id];
                        wait_for_then_use_lock(other_player->controls_playback.locked);

                        int other_player_area = SDL_AtomicGet(&other_player->area_id);
                        if (other_player_area != SDL_AtomicGet(&player->area_id)) {
                            if (other_player_area < 0)
                                goto UnlockAndContinue;

                            // Set spots so that they don't "fall behind"
                            players_spot_of_other_player->pos   = other_player->controls_playback.size;
                            players_spot_of_other_player->frame = other_player->controls_playback.bytes[0];

                            // We also need to inform player that other_player is not in their map...
                            buffer[pos++] = other_player->id;
                            buffer[pos++] = NETF_AREA | NETF_POSITION;
                            netwrite_guy_area(buffer, other_player_area, &pos);
                            netwrite_guy_position(buffer, &other_player->controls_playback, &other_player->guy, &pos);

                            goto UnlockAndContinue;
                        }

                        if (
                            other_player->controls_playback.size > 0 &&
                            players_spot_of_other_player->frame >= 0 &&
                            players_spot_of_other_player->pos < other_player->controls_playback.size &&
                            players_spot_of_other_player->frame < other_player->controls_playback.bytes[0]
                        ) {
                            buffer[pos++] = other_player->id;
                            byte* flags = &buffer[pos++];
                            *flags = 0;

                            vec4* position = NULL,* old_position = NULL;
                            SDL_atomic_t* sync_countdown = &player->countdown_until_i_get_position_of[other_player->id];

                            if (SDL_AtomicGet(sync_countdown) <= 0) {
                                position     = &other_player->guy.position;
                                old_position = &other_player->guy.old_position;
                                SDL_AtomicSet(sync_countdown, frames_between_position_syncs(player));
                                *flags |= NETF_AREA;
                                *flags |= NETF_POSITION;
                            }

                            *flags |= NETF_CONTROLS;
                            netwrite_guy_controls(
                                buffer,
                                &other_player->controls_playback,
                                players_spot_of_other_player,
                                &pos,
                                true
                                /*
                                position, old_position,
                                other_player->guy.flip
                                */
                            );
                            if (position && old_position) {
                                netwrite_guy_area(
                                    buffer,
                                    SDL_AtomicGet(&other_player->area_id),
                                    &pos
                                );
                                netwrite_guy_position(
                                    buffer,
                                    &other_player->controls_playback,
                                    &other_player->guy,
                                    &pos
                                );
                            }
                        }
                        else {
                            // printf("didn't end up sending %i's controls to %i\n", other_player->id, player->id);
                        }
                        UnlockAndContinue:
                        SDL_UnlockMutex(other_player->controls_playback.locked);
                    }
                }
                send(loop->socket, buffer, pos, 0);
            }
        } break;

        default:
            SDL_assert(!"Hey what's this? Unknown packet.");
            break;
        }

        // Now use the buffer to send to the client
        if (bytes_wrote > 0) {
            send(
                loop->socket,
                buffer,
                bytes_wrote,
                0
            );
        }
    }

    Done:
    aligned_free(loop);
    closesocket(loop->socket);
    return r;
}

int network_server_listen(void* vdata) {
    WorldScene* scene = (WorldScene*)vdata;

    SET_LOCKED_STRING(scene->net.status_message, "Server started!");

    memset((char *) &scene->net.my_address, 0, sizeof(scene->net.my_address));
    scene->net.my_address.sin_family = AF_INET;
    scene->net.my_address.sin_addr.s_addr = INADDR_ANY;
    scene->net.my_address.sin_port = htons(atoi(scene->net.textinput_port));
    if(bind(scene->net.local_socket, (struct sockaddr *)&scene->net.my_address , sizeof(scene->net.my_address)) == SOCKET_ERROR)
    {
        SET_LOCKED_STRING_F(scene->net.status_message, "Failed to bind: %i", WSAGetLastError());
        scene->net.status = NOT_CONNECTED;
        return 1;
    }

    scene->net.remote_id = 0;
    scene->net.next_id = 1;

    struct sockaddr_in other_address;

    wait_for_then_use_lock(scene->controls_stream.locked);
    scene->controls_stream.current_frame = 0;
    scene->controls_stream.pos = 1;
    SDL_UnlockMutex(scene->controls_stream.locked);

    listen(scene->net.local_socket, MAX_PLAYERS);

    while (true) {
        // This gets freed at the end of network_server_loop
        ServerLoop* loop = aligned_malloc(sizeof(ServerLoop));

        int addrlen = sizeof(loop->address);
        SOCKET new_connection = accept(scene->net.local_socket, (struct sockaddr*)&loop->address, &addrlen);

        if (new_connection < 0) {
            printf("Bad TCP connection?\n");
            aligned_free(loop);
            continue;
        }

        set_tcp_nodelay(new_connection);

        // TODO this gets spammed at some point which is kind of worrysome (creating 1000000000000 threads... mallocing a lot)
        printf("New connection! Starting thread and waiting for handshake.\n");
        loop->socket = new_connection;
        loop->scene = scene;
        SDL_CreateThread(network_server_loop, "Network server loop", loop);
    }
}

int network_client_loop(void* vdata) {
    WorldScene* scene = (WorldScene*)vdata;
    byte* buffer = aligned_malloc(PACKET_SIZE);

    SET_LOCKED_STRING(scene->net.status_message, "Connecting to server!");

    struct sockaddr_in other_address;

    memset((char *) &other_address, 0, sizeof(other_address));
    other_address.sin_family = AF_INET;

    char* address_input = scene->net.textinput_ip_address;
    char* ip_address = NULL,* port = NULL;
    for (char* c = address_input; *c != '\0'; c++) {
        if (*c == ':') {
            *c = '\0';
            ip_address = address_input;
            port = c + 1;
            break;
        }
    }
    if (!(ip_address && port)) {
        ip_address = address_input;
        port = "2997";
    }

    other_address.sin_addr.s_addr = inet_addr(ip_address);
    other_address.sin_port = htons(atoi(port));
    if (connect(scene->net.local_socket, (struct sockaddr*)&other_address, sizeof(other_address)) < 0) {
        printf("COULD NOT CONNECT!!!!!!!!!\n");
        scene->net.status = NOT_CONNECTED;
        return 0;
    }

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

            buffer[0] = NETOP_WANT_ID;
            send_result = send(
                scene->net.local_socket,
                buffer,
                1,
                0
            );
        }
        else if (first_send) {
            int size = netwrite_guy_initialization(scene, buffer);
            send_result = send(
                scene->net.local_socket,
                buffer,
                size,
                0
            );
            first_send = false;
        }
        else {
            SDL_assert(scene->net.remote_id > 0);

            memset(buffer, 0, PACKET_SIZE);
            int pos = 0;
            buffer[pos++] = NETOP_UPDATE_CONTROLS;
            buffer[pos++] = scene->net.remote_id;
            buffer[pos++] = NETF_CONTROLS;
            netwrite_guy_controls(
                buffer,
                &scene->controls_stream,
                NULL, &pos, false
            );
            send_result = send(
                scene->net.local_socket,
                buffer,
                pos,
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
            int recv_len = recv(scene->net.local_socket, buffer, PACKET_SIZE, 0);
            if (recv_len == SOCKET_ERROR) {
                SET_LOCKED_STRING_F(scene->net.status_message, "Failed to receive: %i", WSAGetLastError());
                scene->net.status = NOT_CONNECTED;
                return 2;
            }

            // CLIENT
            switch (buffer[0]) {
            case NETOP_HERES_YOUR_ID: {
                scene->net.remote_id = buffer[1];
                // Allocate players for when update_position comes in
                scene->net.players[scene->net.number_of_players++] = allocate_new_player(0, &other_address);
                for (int i = 2; i < recv_len; i++) {
                    // NOTE as client I guess all `other_address`es will just be the server's address..
                    scene->net.players[scene->net.number_of_players++] = allocate_new_player(buffer[i], &other_address);
                }
                scene->net.connected = true;
                SET_LOCKED_STRING_F(scene->net.status_message, "Client ID: %i", scene->net.remote_id);
            } break;

            case NETOP_INITIALIZE_PLAYER:
                netop_initialize_state(scene, buffer, NULL);
                break;

            case NETOP_UPDATE_CONTROLS: {
                RemotePlayer* player = netop_update_controls(scene, buffer, NULL, recv_len);
                if (player == NULL) {
                    printf("UPDATING FORF BULLSHIT PLAYER\n");
                }
                else {
                    SDL_AtomicSet(&player->poke, true);
                }
            } break;

            default:
                break;
            }
        }
    }

    return 0;
}

SDL_Rect text_box_rect = { 200, 200, 400, 40 };

int set_tcp_nodelay(SOCKET socket) {
    int flag = 1;
    int result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
    if (result < 0) {
        SDL_ShowSimpleMessageBox(
            0, "Socket error",
            "Could not set TCP_NODELAY on socket. Network activity may be slow.",
            main_window
        );
    }

    SDL_assert(socket != SOCKET_ERROR);
    return result;
}

void scene_world_initialize(void* vdata, Game* game) {
    WorldScene* data = (WorldScene*)vdata;
    data->game = game;
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
        data->net.status = NOT_CONNECTED;
        SDL_AtomicSet(&data->net.status_message_locked, false);

        SDL_memset(data->net.textinput_ip_address, 0, sizeof(data->net.textinput_ip_address));
        SDL_strlcat(data->net.textinput_ip_address, "127.0.0.1", EDITABLE_TEXT_BUFFER_SIZE);
        SDL_memset(data->net.textinput_port, 0, sizeof(data->net.textinput_port));
        SDL_strlcat(data->net.textinput_port, "2997", EDITABLE_TEXT_BUFFER_SIZE);

        data->net.local_socket = socket(AF_INET, SOCK_STREAM, 0);
        set_tcp_nodelay(data->net.local_socket);
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
    if (rand() > RAND_MAX / 5)
        data->guy.right_foot_color = data->guy.left_foot_color;
    else {
        data->guy.right_foot_color.r = rand() % 255;
        data->guy.right_foot_color.g = rand() % 255;
        data->guy.right_foot_color.b = rand() % 255;
    }
    if (rand() < RAND_MAX / 5) {
        data->guy.eye_color.r = rand() % 255;
        data->guy.eye_color.g = rand() % 70;
        data->guy.eye_color.b = rand() % 140;
    }
    BENCH_END(loading_crattle1);

    BENCH_START(loading_tiles);
    data->current_area = AREA_TESTZONE_ONE;
    data->map = cached_map(game, map_asset_for_area(data->current_area));
    data->map->area_id = data->current_area;
    BENCH_END(loading_tiles);

    BENCH_START(loading_sound);
    data->music = cached_sound(game, ASSET_MUSIC_ARENA_OGG);
    game->audio.looped_waves[0] = data->music;

    data->test_sound = cached_sound(game, ASSET_SOUNDS_JUMP_OGG);
    data->guy_view.jump_sound = data->test_sound;
    BENCH_END(loading_sound);

    for (int i = 0; i < game->argc; i++) {
        if (strcmp(game->argv[i], "-h") == 0 || strcmp(game->argv[i], "--host") == 0) {
            printf("Starting server now!!!\n");
            data->net.status = HOSTING;
            SDL_CreateThread(network_server_listen, "Network server listen", data);
        }
    }

    // Add ruby object to game!
    mrb_iv_check(game->mrb, game->ruby.sym_atworld);
    mrb_value world = mrb_class_new_instance(mrb, 0, NULL, game->ruby.world_class);
    mrb_data_init(world, data, &mrb_controls_type);
    mrb_iv_set(game->mrb, game->ruby.game, game->ruby.sym_atworld, world);
}

void set_camera_target(Game* game, Map* map, Character* guy) {
    game->camera_target.x[X] = guy->position.x[X] - game->window_width / 2.0f;

    if (game->window_width > map->width) {
        game->camera_target.x[X] = (map->width - game->window_width) / 2.0f;
    }
    else if (game->camera_target.x[X] < 0)
        game->camera_target.x[X] = 0;
    else if (game->camera_target.x[X] + game->window_width > map->width)
        game->camera_target.x[X] = map->width - game->window_width;

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

    if (game->window_height > map->height)
        game->camera_target.x[Y] = (map->height - game->window_height) / 2.0f;
    else if (game->camera_target.x[Y] < 0)
        game->camera_target.x[Y] = 0;
    else if (game->camera_target.x[Y] + game->window_height > map->height)
        game->camera_target.x[Y] = map->height - game->window_height;
}

void local_go_through_door(void* vs, Game* game, Character* guy, Door* door) {
    WorldScene* s = (WorldScene*)vs;

    SDL_assert(door->dest_area > -1);
    s->current_area = door->dest_area;
    int map_asset = map_asset_for_area(s->current_area);
    SDL_assert(map_asset > -1);
    s->map = cached_map(game, map_asset);
    s->map->area_id = s->current_area;

    float dest_x = (float)door->dest_x;
    float dest_y = s->map->height - (float)door->dest_y;

    guy->position.x[X]     = dest_x;
    guy->position.x[Y]     = dest_y;
    guy->old_position.x[X] = dest_x;
    guy->old_position.x[Y] = dest_y;
    set_camera_target(game, s->map, &s->guy);
    game->camera.simd = game->camera_target.simd;
}

void remote_go_through_door(void* vs, Game* game, Character* guy, Door* door) {
    WorldScene* s = (WorldScene*)vs;

    RemotePlayer* player = NULL;
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* plr = s->net.players[i];
        if (plr && plr->id == guy->player_id) {
            player = plr;
            break;
        }
    }
    if (player == NULL) {
        printf("ERROR!!! COULD NOT FIND REMOTEPLAYER OF GUY GOING THROUGH DOOR!\n");
        return;
    }

    wait_for_then_use_lock(player->mob_event_buffer_locked);
    player->mob_event_count = 0;
    player->mob_event_buffer_pos = 0;

    SDL_assert(door->dest_area > -1);
    int area_id = door->dest_area;
    int map_asset = map_asset_for_area(area_id);
    SDL_assert(map_asset > -1);
    Map* map = cached_map(game, map_asset);
    map->area_id = area_id;

    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* other_player = s->net.players[i];
        if (other_player && other_player->id != player->id) {
            /*
            other_player->stream_spots_of[player->id].frame = player->controls_playback.current_frame;
            other_player->stream_spots_of[player->id].pos   = player->controls_playback.pos;
            */

            SDL_AtomicSet(&player->countdown_until_i_get_position_of[other_player->id], 0);
            SDL_AtomicSet(&other_player->countdown_until_i_get_position_of[player->id], 0);
        }
    }
    SDL_AtomicSet(&player->countdown_until_i_get_position_of[0], 0);

    float dest_x = (float)door->dest_x;
    float dest_y = map->height - (float)door->dest_y;

    guy->position.x[X]     = dest_x;
    guy->position.x[Y]     = dest_y;
    guy->old_position.x[X] = dest_x;
    guy->old_position.x[Y] = dest_y;

    SDL_AtomicSet(&player->area_id, area_id);
    SDL_AtomicSet(&player->just_switched_maps, true);

    SDL_UnlockMutex(player->mob_event_buffer_locked);
}

void local_spawn_mob(void* vs, Map* map, struct Game* game, int mobtype, vec2 pos) {
    spawn_mob(map, game, mobtype, pos);
}

void connected_spawn_mob(void* vs, Map* map, struct Game* game, int mob_type_id, vec2 pos) {
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);

    MobCommon* mob = spawn_mob(map, game, mob_type_id, pos);
    if (mob != NULL) {
        MobType* mob_type = &mob_registry[mob_type_id];

        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player == NULL || SDL_AtomicGet(&player->area_id) != map->area_id || SDL_AtomicGet(&player->just_switched_maps))
                continue;

            int m_id = mob_id(map, mob);
            wait_for_then_use_lock(player->mob_event_buffer_locked);

            player->mob_event_buffer[player->mob_event_buffer_pos++] = NETF_MOBEVENT_SPAWN;
            write_to_buffer(player->mob_event_buffer, &mob_type_id, &player->mob_event_buffer_pos, sizeof(int));
            write_to_buffer(player->mob_event_buffer, &pos,         &player->mob_event_buffer_pos, sizeof(vec2));
            write_to_buffer(player->mob_event_buffer, &m_id,        &player->mob_event_buffer_pos, sizeof(int));
            mob_type->save(mob, map, player->mob_event_buffer,      &player->mob_event_buffer_pos);

            player->mob_event_count += 1;
            SDL_UnlockMutex(player->mob_event_buffer_locked);
        }
    }
}

void write_mob_events(void* vs, Map* map, struct Game* game, MobCommon* mob) {
    WorldScene* s = (WorldScene*)vs;

    byte sync_buffer[MOB_EVENT_BUFFER_SIZE];
    int size = 0;

    MobType* reg = &mob_registry[mob->mob_type_id];
    if (reg->sync_send != NULL && reg->sync_send(mob, map, sync_buffer, &size)) {
        SDL_assert(size <= MOB_EVENT_BUFFER_SIZE);

        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player == NULL || SDL_AtomicGet(&player->area_id) != map->area_id || SDL_AtomicGet(&player->just_switched_maps))
                continue;
            SDL_assert(player->mob_event_buffer_pos <= MOB_EVENT_BUFFER_SIZE && player->mob_event_buffer_pos >= 0);
            SDL_assert(player->mob_event_buffer_pos + size < MOB_EVENT_BUFFER_SIZE);

            int m_id = mob_id(map, mob);
            wait_for_then_use_lock(player->mob_event_buffer_locked);

            player->mob_event_buffer[player->mob_event_buffer_pos++] = NETF_MOBEVENT_UPDATE;
            write_to_buffer(player->mob_event_buffer, &m_id,       &player->mob_event_buffer_pos, sizeof(int));
            write_to_buffer(player->mob_event_buffer, sync_buffer, &player->mob_event_buffer_pos, size);

            player->mob_event_count += 1;
            SDL_UnlockMutex(player->mob_event_buffer_locked);
        }
    }
}

void record_controls(Controls* from, ControlsBuffer* to) {
    for (enum Control ctrl = 0; ctrl < NUM_CONTROLS; ctrl++) {
        if (from->this_frame[ctrl])
            to->bytes[to->pos++] = ctrl;
    }
    to->bytes[to->pos++] = CONTROL_BLOCK_END;
    SDL_assert(to->pos < CONTROLS_BUFFER_SIZE);

    to->current_frame += 1;

    to->size = to->pos;
    to->bytes[0] = to->current_frame;
}

#define PERCENT_CHANCE(percent) (rand() < RAND_MAX / (100 / percent))

void scene_world_update(void* vs, Game* game) {
    WorldScene* s = (WorldScene*)vs;

    // Routine network stuff:
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* player = s->net.players[i];
        if (player == NULL) continue;
        // Calculate ping
        player->ping_counter += 1;
        if (SDL_AtomicGet(&player->poke)) {
            player->ping = player->ping_counter;
            player->ping_counter = 0;
            SDL_AtomicSet(&player->poke, false);
        }

        // Decrement position sync counters
        for (int j = 0; j < MAX_PLAYERS; j++)
            SDL_AtomicAdd(&player->countdown_until_i_get_position_of[j], -1);
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
        if (plr == NULL || SDL_AtomicGet(&plr->area_id) == -1)
            continue;

        if (s->net.status == JOINING && SDL_AtomicGet(&plr->area_id) != s->current_area)
            continue;

        wait_for_then_use_lock(plr->controls_playback.locked);
        plr->number_of_physics_updates_this_frame = 0;

        if (
            plr->controls_playback.current_frame >= 0                          &&
            plr->controls_playback.pos           < plr->controls_playback.size &&
            plr->controls_playback.current_frame < plr->controls_playback.bytes[0]
        ) {
            // Do 2 frames at a time if we're so many frames behind
            int frames_behind = (int)plr->controls_playback.bytes[0] - plr->controls_playback.current_frame;
            const int frames_behind_threshold = plr->ping * 2 + 1;

            if (frames_behind > frames_behind_threshold) {
                printf("Network control sync behind by %i frames\n", frames_behind);
                plr->number_of_physics_updates_this_frame = 2;
            }
            else
                plr->number_of_physics_updates_this_frame = 1;

            for (int i = 0; i < plr->number_of_physics_updates_this_frame; i++) {
                if (i > 0) // After the first update, we're doing consecutive ones and we need to "end" the frame.
                    net_character_post_update(plr);

                if (s->net.status == JOINING && SDL_AtomicGet(&plr->area_id) != s->current_area)
                    break;

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

                int area_id = SDL_AtomicGet(&plr->area_id);
                Map* map = cached_map(game, map_asset_for_area(area_id));
                map->area_id = area_id;

                apply_character_physics(game, &plr->guy, &plr->controls, s->gravity, s->drag);
                collide_character(&plr->guy, &map->tile_collision);
                slide_character(s->gravity, &plr->guy);
                interact_character_with_world(game, &plr->guy, &plr->controls, map, s, (s->net.status == HOSTING) ? remote_go_through_door : NULL);
                update_character_animation(&plr->guy);

                sync_player_frame_if_should(s->net.status, plr);

                if (plr->controls_playback.current_frame >= plr->controls_playback.bytes[0]) {
                    // This is a tricky situation. Happens rarely and don't know why.
                    if (plr->controls_playback.pos != plr->controls_playback.size) {
                        // uhhhh
                        int original_size = plr->controls_playback.size;
                        plr->controls_playback.size = plr->controls_playback.pos;
                        printf("ADJUSTED %i's PLAYBACK SIZE\n", plr->id);

                        if (s->net.status == HOSTING) {
                            int change_in_size = original_size - plr->controls_playback.size;

                            for (int i = 0; i < s->net.number_of_players; i++) {
                                RemotePlayer* player_of_spot = s->net.players[i];
                                if (player_of_spot == NULL || player_of_spot->id == plr->id)
                                    continue;

                                ControlsBufferSpot* spot = &player_of_spot->stream_spots_of[plr->id];
                                if (spot->frame < 0)
                                    continue;

                                spot->pos -= change_in_size;
                                /*
                                // Worth noting that this got tripped once when I disconnected a dude...
                                SDL_assert(spot->frame >= 0);
                                SDL_assert(spot->pos > 0);
                                */
                                if (spot->frame < 0 || spot->pos <= 0) {
                                    spot->frame = plr->controls_playback.current_frame;
                                    spot->pos = plr->controls_playback.pos;
                                    SDL_AtomicSet(&player_of_spot->countdown_until_i_get_position_of[plr->id], 0);
                                    printf("Inaccurate playback screwed up %i's spot in %i's buffer!!!", player_of_spot->id, plr->id);
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
    // END OF REMOTE PLAYER CONTROLS PLAYBACK

    // Update local player
    apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);
    interact_character_with_world(game, &s->guy, &game->controls, s->map, s, local_go_through_door);
    update_character_animation(&s->guy);

    // Update everything else on the map(s)
    void(*on_mob_spawn)(void*, Map*, struct Game*, int, vec2) = NULL;
    if (s->net.status == NOT_CONNECTED)
        on_mob_spawn = local_spawn_mob;
    else if (s->net.status == HOSTING)
        on_mob_spawn = connected_spawn_mob;

    void(*after_mob_update)(void*, Map*, struct Game*, MobCommon*) = NULL;
    if (s->net.status == HOSTING)
        after_mob_update = write_mob_events;

    SDL_assert(s->map->area_id == s->current_area);
    update_map(s->map, game, s, on_mob_spawn, after_mob_update);
    if (s->net.status == HOSTING) {
        int updated_maps[NUMBER_OF_AREAS];
        memset(updated_maps, 0, sizeof(updated_maps));
        updated_maps[s->current_area] = true;

        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player == NULL) continue;
            int area_id = SDL_AtomicGet(&player->area_id);
            if (updated_maps[area_id]) continue;

            Map* map = cached_map(game, map_asset_for_area(area_id));
            update_map(map, game, s, connected_spawn_mob, after_mob_update);
            updated_maps[area_id] = true;
        }
    }

    // Follow local player with camera
    set_camera_target(game, s->map, &s->guy);
    // move cam position towards cam target
    game->camera.simd = _mm_add_ps(game->camera.simd, _mm_mul_ps(_mm_sub_ps(game->camera_target.simd, game->camera.simd), _mm_set_ps(0, 0, 0.1f, 0.1f)));

    // Snap to cam target after awhile to stop a certain amount of jerkiness.
    const float cam_alpha = 0.1f;
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
        record_controls(&game->controls, &s->controls_stream);

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
                // TODO if the lowest doesn't drop us below 255, someone has disconnected!

                struct BufferChanges change = truncate_controls_buffer(&s->controls_stream, lowest_frame, lowest_pos);

                for (int i = 0; i < s->net.number_of_players; i++) {
                    RemotePlayer* player = s->net.players[i];
                    if (player != NULL) {
                        if (player->local_stream_spot.frame >= 0) {
                            player->local_stream_spot.frame -= change.change_in_bytes0;
                            player->local_stream_spot.pos   -= change.change_in_size;
                            // SDL_assert(player->local_stream_spot.frame >= 0);
                            // SDL_assert(player->local_stream_spot.pos > 0);
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
        if (plr != NULL && plr->number_of_physics_updates_this_frame > 0 && (s->net.status == HOSTING || SDL_AtomicGet(&plr->area_id) == s->current_area))
            net_character_post_update(plr);
    }

    // Swap to offset viewer on F1 press
    // if (just_pressed(&game->controls, C_F1))
        // switch_scene(game, SCENE_OFFSET_VIEWER);

    if (s->net.status == NOT_CONNECTED) {
        if (just_pressed(&game->controls, C_F1)) {
            start_editing_text(game, s->net.textinput_port, EDITABLE_TEXT_BUFFER_SIZE, &text_box_rect);
            s->net.status = HOSTING;
        }
        else if (just_pressed(&game->controls, C_F2)) {
            start_editing_text(game, s->net.textinput_ip_address, EDITABLE_TEXT_BUFFER_SIZE, &text_box_rect);
            s->net.status = JOINING;
            game->net_joining = true;
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
                SDL_CreateThread(network_server_listen, "Network server listen", s);
                break;
            case JOINING:
                SDL_CreateThread(network_client_loop, "Network client loop", s);
                printf("Created client loop thread\n");
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


void scene_world_render(void* vs, Game* game) {
    WorldScene* s = (WorldScene*)vs;
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

    BENCH_START(draw_map);
    // Draw WHOLE MAP
    draw_map(game, s->map);
    BENCH_END(draw_map);

    BENCH_START(characters);
    // Draw guys
    {
        draw_character(game, &s->guy, &s->guy_view);
        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player && SDL_AtomicGet(&player->area_id) == s->current_area)
                draw_character(game, &player->guy, &s->guy_view);
        }
    }
    BENCH_END(characters);

    // Recording indicator
    /*
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
    */

    BENCH_START(editable_text);
    // Draw editable text box!!
    if (game->text_edit.text == s->editable_text) {
        draw_text_box(game, &text_box_rect, s->editable_text);
    }
    else if (game->text_edit.text == s->net.textinput_ip_address || game->text_edit.text == s->net.textinput_port) {
        set_text_color(game, 255, 255, 255);
        if (s->net.status == HOSTING) {
            draw_text(game, text_box_rect.x - 130, 200, "So you want to be server");
            draw_text_box(game, &text_box_rect, s->net.textinput_port);
        }
        else {
            draw_text(game, text_box_rect.x - 100, 200, "So you want to be client");
            draw_text_box(game, &text_box_rect, s->net.textinput_ip_address);
        }
    }
    BENCH_END(editable_text);

    BENCH_START(status_message);
    if (s->net.status != NOT_CONNECTED) {
        set_text_color(game, 100, 50, 255);
        draw_text(game, 10, game->window_height - 50, s->net.status_message);
    }
    BENCH_END(status_message);

#ifdef _DEBUG
    BENCH_START(ping);
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* player = s->net.players[i];
        if (s->net.status == JOINING && player->id != 0) continue;
        if (player == NULL) continue;

        double ping_in_ms = (1000.0 * (double)player->ping) / game->frames_per_second;

        set_text_color(game, 255, 255, 50);
        if (s->net.status == JOINING) {
            draw_text_ex_f(game, game->window_width - 180, game->window_height - 60 - i * 20, -1, 0.7f, "Ping: %.1fms", ping_in_ms);
        }
        else {
            draw_text_ex_f(game, game->window_width - 200, game->window_height - 60 - i * 20, -1, 0.7f, "P%i Ping: %.1fms", player->id, ping_in_ms);
        }
    }
    BENCH_END(ping);
#endif
}

void scene_world_cleanup(void* vdata, Game* game) {
    WorldScene* data = (WorldScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;

    closesocket(data->net.local_socket);
}
