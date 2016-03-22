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
#define PACKET_SIZE (1024 * 5)
#define CONTROLS_BUFFER_SIZE (255 * NUM_CONTROLS)
#define CONTROL_BLOCK_END NUM_CONTROLS
#define MAX_PLAYERS 20
#define MOB_EVENT_BUFFER_SIZE (1024 * 3)

#define TRANSITION_STEP  4
#define TRANSITION_POINT 52

#define INV_FADE_ZERO_POINT 1
#define INV_FADE_MAX 11

// If multiple of these flags comes in an update, their content must be in this order.
#define NETF_CONTROLS   (1 << 0)
#define NETF_AREA       (1 << 1)
#define NETF_POSITION   (1 << 2)
#define NETF_MAPSTATE   (1 << 3)
#define NETF_MOBEVENT   (1 << 4)
#define NETF_ATTRIBUTES (1 << 5)
#define NETF_INVENTORY  (1 << 6)

#define NETF_MOBEVENT_SPAWN 0
#define NETF_MOBEVENT_UPDATE 1
#define NETF_MOBEVENT_DESPAWN 2

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
    float run_speed;
} PhysicsState;

#define MAP_DATA_NOT_NEEDED 0
#define MAP_DATA_NEED 1
#define MAP_DATA_RECEIVED 2

typedef struct MapTransition {
    SDL_mutex* locked;
    SDL_atomic_t map_data_status;
    int destination_area;
    vec2 destination_position;
    int progress_percent;
    void* map_data;

    byte door_flags;
    void(*on_transition)(void*);
} MapTransition;

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

    // Countdown until we SEND indexed guy's position (as server) (ALSO INDEXED BY ID)
    SDL_atomic_t countdown_until_i_get_position_of[MAX_PLAYERS];
    // Position we have RECEIVED for this guy and plan to set (if we so choose) (i.e. if we are client)
    PhysicsState sync;
    SDL_atomic_t frames_until_sync;

    // Flag for which character
    SDL_atomic_t i_need_attributes_of[MAX_PLAYERS];

    SDL_mutex* mob_event_buffer_locked;
    byte mob_event_buffer[MOB_EVENT_BUFFER_SIZE];
    int mob_event_buffer_pos;
    int mob_event_count;

    int last_frames_playback_pos;
    ControlsBuffer controls_playback;

    SDL_atomic_t i_want_inventory;
} RemotePlayer;

typedef struct WorldScene {
    Game* game;
    // TODO TODO MOVE GRAVITY AND DRAG TO MAPS
    float gravity;
    float drag;
    Character guy;

    // inventory interface stuff
    int inv_fade;
    int inv_fade_countdown;

    AudioWave* music;
    AudioWave* test_sound;
    int current_area;
    Map* map;
    char editable_text[EDITABLE_TEXT_BUFFER_SIZE];

    ControlsBuffer controls_stream;
    const char* dbg_last_action;

    MapTransition transition;

    mrb_value script_obj;
    mrb_value rguy;

    struct NetInfo {
        int remote_id;
        int next_id;
        SDL_atomic_t status_message_locked;
        char status_message[512];
        char textinput_ip_address[EDITABLE_TEXT_BUFFER_SIZE];
        char textinput_port[EDITABLE_TEXT_BUFFER_SIZE];
        byte status_message_countdown;
        enum { HOSTING, JOINING, NOT_CONNECTED, WANT_TO_JOIN } status;
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

    wait_for_then_use_lock(s->transition.locked);
    s->transition.destination_area = door->dest_area;
    s->transition.destination_position.x = (float)door->dest_x;
    s->transition.destination_position.y = (float)door->dest_y;
    s->transition.progress_percent = 0;
    s->transition.on_transition = door->callback;
    s->transition.door_flags = door->flags;
    SDL_UnlockMutex(s->transition.locked);
}

void transition_maps(WorldScene* s, Game* game, MapTransition* transition) {
    wait_for_then_use_lock(s->transition.locked);

    if (s->transition.on_transition) {
        s->transition.on_transition(s);
        s->transition.on_transition = NULL;
    }

    s->current_area = transition->destination_area;
    int map_asset = map_asset_for_area(s->current_area);
    SDL_assert(map_asset > -1);
    s->map = cached_map(game, map_asset);
    s->map->area_id = s->current_area;

    float dest_x = transition->destination_position.x;
    float dest_y;
    if (transition->door_flags & DOOR_INVERT_Y)
        dest_y = s->map->height - transition->destination_position.y;
    else
        dest_y = transition->destination_position.y;

    s->guy.position.x[X]     = dest_x;
    s->guy.position.x[Y]     = dest_y;
    s->guy.old_position.x[X] = dest_x;
    s->guy.old_position.x[Y] = dest_y;

    set_camera_target(game, s->map, &s->guy);
    game->camera.simd = game->camera_target.simd;

    SDL_UnlockMutex(s->transition.locked);
}

void remote_go_through_door(void* vs, Game* game, Character* guy, Door* door) {
    printf("remote go through door!\n");
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);

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

    if (door->callback)
        door->callback(vs);

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
    float dest_y;
    if (door->flags & DOOR_INVERT_Y)
        dest_y = map->height - (float)door->dest_y;
    else
        dest_y = (float)door->dest_y;

    guy->position.x[X]     = dest_x;
    guy->position.x[Y]     = dest_y;
    guy->old_position.x[X] = dest_x;
    guy->old_position.x[Y] = dest_y;

    SDL_AtomicSet(&player->area_id, area_id);
    SDL_AtomicSet(&player->just_switched_maps, true);

    SDL_UnlockMutex(player->mob_event_buffer_locked);
}

void unconnected_set_item(void* vs, struct Character* guy, struct Game* game, int slot, int item) {
    WorldScene* s = (WorldScene*)vs;
    if (s->net.status != JOINING)
        set_item(&guy->inventory, game, slot, item);
}

void connected_set_item(void* vs, struct Character* guy, struct Game* game, int slot, int item) {
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);

    printf("Charcter with ID %i picking up an item.\n", guy->player_id);
    set_item(&guy->inventory, game, slot, item);

    // Don't bother with non-player characters or the local character.
    if (guy->player_id == -1 || guy->player_id == 0)
        return;

    RemotePlayer* player = NULL;
    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* plr = s->net.players[i];
        if (plr && plr->id == guy->player_id) {
            player = plr;
            break;
        }
    }
    if (player == NULL) {
        printf("ERROR COULD NOT FIND GUY PICKING UP ITEM.\n");
        return;
    }

    SDL_AtomicSet(&player->i_want_inventory, true);
}

void unconnected_despawn_mob(void* vs, Map* map, struct Game* game, MobCommon* mob) {
    WorldScene* s = (WorldScene*)vs;
    if (s->net.status != JOINING)
        despawn_mob(map, game, mob);
}

void connected_despawn_mob(void* vs, Map* map, struct Game* game, MobCommon* mob) {
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);

    int id = mob_id(map, mob);
    despawn_mob(map, game, mob);

    for (int i = 0; i < s->net.number_of_players; i++) {
        RemotePlayer* player = s->net.players[i];
        if (player == NULL || SDL_AtomicGet(&player->area_id) != map->area_id || SDL_AtomicGet(&player->just_switched_maps))
            continue;

        wait_for_then_use_lock(player->mob_event_buffer_locked);

        player->mob_event_buffer[player->mob_event_buffer_pos++] = NETF_MOBEVENT_DESPAWN;
        write_to_buffer(player->mob_event_buffer, &id, &player->mob_event_buffer_pos, sizeof(int));
        player->mob_event_count += 1;

        SDL_UnlockMutex(player->mob_event_buffer_locked);
    }
}

// Never spawn mobs when joining
void unconnected_spawn_mob(void* vs, Map* map, struct Game* game, int mobtype, vec2 pos) {
    WorldScene* s = (WorldScene*)vs;
    if (s->net.status != JOINING)
        spawn_mob(map, game, mobtype, pos);
}

void connected_spawn_mob(void* vs, Map* map, struct Game* game, int mob_type_id, vec2 pos) {
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);

    MobCommon* mob = spawn_mob(map, game, mob_type_id, pos);
    if (mob != NULL) {
        MobType* mob_type = &mob_registry[mob_type_id];
        int m_id = mob_id(map, mob);

        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player == NULL || SDL_AtomicGet(&player->area_id) != map->area_id || SDL_AtomicGet(&player->just_switched_maps))
                continue;

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
        plr->guy.run_speed     = plr->sync.run_speed;

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
#define NETOP_PLAYER_DC 12

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

RemotePlayer* allocate_new_player(WorldScene* scene, int id, struct sockaddr_in* addr) {
    RemotePlayer* new_player = aligned_malloc(sizeof(RemotePlayer));
    memset(new_player, 0, sizeof(RemotePlayer));

    new_player->id = id;
    new_player->address = *addr;
    SDL_AtomicSet(&new_player->area_id, -1);
    SDL_AtomicSet(&new_player->just_switched_maps, false);

    default_character(scene->game, &new_player->guy);
    // NOTE loading textures in network thread - problem or no problem? Just add a mutex to cached assets if necessary.
    default_character_animations(scene->game, &new_player->guy);
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

    for (int i = 0; i < MAX_PLAYERS; i++) {
        SDL_AtomicSet(&new_player->countdown_until_i_get_position_of[i], 0);
        SDL_AtomicSet(&new_player->i_need_attributes_of[i], 0);
    }
    SDL_AtomicSet(&new_player->frames_until_sync, -1);

    SDL_AtomicSet(&new_player->i_want_inventory, false);

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
    read_from_buffer(buffer, guy->old_position.x,  pos, sizeof(guy->old_position.x));
    read_from_buffer(buffer, &guy->flip,       pos, sizeof(guy->flip));
    read_from_buffer(buffer, &guy->body_color, pos, sizeof(SDL_Color) * 3);
    read_from_buffer(buffer, &guy->eye_type,   pos, sizeof(guy->eye_type));
    read_from_buffer(buffer, &guy->eye_color,  pos, sizeof(SDL_Color));
    read_from_buffer(buffer, &guy->body_type,  pos, sizeof(guy->body_type));
    read_from_buffer(buffer, &guy->feet_type,  pos, sizeof(guy->feet_type));
    read_from_buffer(buffer, &guy->ground_speed_max,    pos, sizeof(guy->ground_speed_max));
    read_from_buffer(buffer, &guy->run_speed_max,       pos, sizeof(guy->run_speed_max));
    read_from_buffer(buffer, &guy->ground_acceleration, pos, sizeof(guy->ground_acceleration));
    read_from_buffer(buffer, &guy->ground_deceleration, pos, sizeof(guy->ground_deceleration));
    read_from_buffer(buffer, &guy->jump_acceleration,   pos, sizeof(guy->jump_acceleration));
    read_from_buffer(buffer, &guy->jump_cancel_dy,      pos, sizeof(guy->jump_cancel_dy));
    read_from_buffer(buffer, area_id,          pos, sizeof(int));
}

void write_guy_info_to_buffer(byte* buffer, Character* guy, int area_id, int* pos) {
    write_to_buffer(buffer, guy->position.x,  pos, sizeof(guy->position.x));
    write_to_buffer(buffer, guy->old_position.x,  pos, sizeof(guy->old_position.x));
    write_to_buffer(buffer, &guy->flip,       pos, sizeof(guy->flip));
    write_to_buffer(buffer, &guy->body_color, pos, sizeof(guy->body_color) * 3);
    write_to_buffer(buffer, &guy->eye_type,   pos, sizeof(guy->eye_type));
    write_to_buffer(buffer, &guy->eye_color,  pos, sizeof(guy->eye_color));
    write_to_buffer(buffer, &guy->body_type,  pos, sizeof(guy->body_type));
    write_to_buffer(buffer, &guy->feet_type,  pos, sizeof(guy->feet_type));
    write_to_buffer(buffer, &guy->ground_speed_max,    pos, sizeof(guy->ground_speed_max));
    write_to_buffer(buffer, &guy->run_speed_max,       pos, sizeof(guy->run_speed_max));
    write_to_buffer(buffer, &guy->ground_acceleration, pos, sizeof(guy->ground_acceleration));
    write_to_buffer(buffer, &guy->ground_deceleration, pos, sizeof(guy->ground_deceleration));
    write_to_buffer(buffer, &guy->jump_acceleration,   pos, sizeof(guy->jump_acceleration));
    write_to_buffer(buffer, &guy->jump_cancel_dy,      pos, sizeof(guy->jump_cancel_dy));
    write_to_buffer(buffer, &area_id, pos, sizeof(int));
}

void read_guy_inventory_from_buffer(byte* buffer, Character* guy, int* pos) {
    byte cap = buffer[*pos];
    *pos += 1;
    wait_for_then_use_lock(guy->inventory.locked);
    SDL_assert((int)cap == guy->inventory.capacity);
    read_from_buffer(buffer, guy->inventory.items, pos, cap * sizeof(ItemCommon));
    SDL_UnlockMutex(guy->inventory.locked);
}

void write_guy_inventory_to_buffer(byte* buffer, Character* guy, int* pos) {
    wait_for_then_use_lock(guy->inventory.locked);
    buffer[*pos] = (byte)guy->inventory.capacity;
    *pos += 1;
    write_to_buffer(buffer, guy->inventory.items, pos, guy->inventory.capacity * sizeof(ItemCommon));
    SDL_UnlockMutex(guy->inventory.locked);
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
                player = allocate_new_player(scene, player_id, &scene->net.server_address);
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
        read_guy_inventory_from_buffer(buffer, &player->guy, &pos);
        load_character_atlases(scene->game, &player->guy);
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

RemotePlayer* netop_update_controls(WorldScene* scene, byte* buffer, struct sockaddr_in* addr) {
    int pos = 1; // [0] is opcode, which we know by now

    short bufsize;
    read_from_buffer(buffer, &bufsize, &pos, sizeof(short));

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
            read_from_buffer(buffer, &player->sync.run_speed, &pos, sizeof(int));
            if (frame_of_position == -1)
                SDL_AtomicSet(&player->frames_until_sync, 0);
            else
                SDL_AtomicSet(&player->frames_until_sync, frame_of_position - player->controls_playback.current_frame);

            sync_player_frame_if_should(scene->net.status, player);
        }

        SDL_UnlockMutex(player->controls_playback.locked);

        if (flags & NETF_MAPSTATE) {
            int area_id;
            read_from_buffer(buffer, &area_id, &pos, sizeof(int));
            float dest_x, dest_y;
            read_from_buffer(buffer, &dest_x, &pos, sizeof(float));
            read_from_buffer(buffer, &dest_y, &pos, sizeof(float));
            int map_data_size;
            read_from_buffer(buffer, &map_data_size, &pos, sizeof(int));
            SDL_assert(map_data_size >= 0);

            wait_for_then_use_lock(scene->transition.locked);
            scene->transition.destination_position.x = dest_x;
            scene->transition.destination_position.y = dest_y;
            scene->transition.destination_area = area_id;
            if (scene->transition.door_flags & DOOR_INVERT_Y)
                scene->transition.door_flags ^= DOOR_INVERT_Y;

            if (scene->transition.progress_percent <= TRANSITION_POINT) {
                if (scene->transition.map_data)
                    aligned_free(scene->transition.map_data);

                scene->transition.map_data = aligned_malloc(map_data_size);
                read_from_buffer(buffer, scene->transition.map_data, &pos, map_data_size);
                SDL_AtomicSet(&scene->transition.map_data_status, MAP_DATA_RECEIVED);
            }
            else {
                // Don't we wait until we get MAP_DATA_RECEIVED before setting current_area and such?
                // If I'm right, this should never happen.
                // (This gets) This got triggered.
                SDL_assert(false);
                wait_for_then_use_lock(scene->map->locked);
                SDL_assert(area_id == scene->map->area_id);
                SDL_assert(area_id == scene->current_area);

                clear_map_state(scene->map);
                read_map_state(scene->map, buffer, &pos);

                SDL_UnlockMutex(scene->map->locked);
            }

            SDL_UnlockMutex(scene->transition.locked);

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

            if (total_size_of_events == 0 && number_of_events != 0) {
                printf("WARNING: 0 event buffer size but %i events!? Setting number_of_events to 0.\n", number_of_events);
                number_of_events = 0;
            }

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

                    case NETF_MOBEVENT_DESPAWN: {
                        int m_id;
                        read_from_buffer(buffer, &m_id, &pos, sizeof(int));
                        MobCommon* mob = mob_from_id(scene->map, m_id);
                        if (mob == NULL) {
                            printf("WARNING: Server wants us to despawn a non existant mob of id %i\n", m_id);
                        }
                        else {
                            despawn_mob(scene->map, scene->game, mob);
                        }
                    } break;

                    default:
                        printf("WARNING: UNKNOWN MOB EVENT %i\n", (int)event_type);
                    }
                }
            }
            else {
                printf("Got mob events for the wrong area woops!!!! Draining buffer...\n");
                pos += total_size_of_events;
            }

            SDL_UnlockMutex(scene->map->locked);
        }

        if (flags & NETF_ATTRIBUTES) {
            int a;
            read_guy_info_from_buffer(buffer, &player->guy, &a, &pos);
            if (scene->net.status == HOSTING) {
                for (int i = 0; i < scene->net.number_of_players; i++) {
                    RemotePlayer* other_player = scene->net.players[i];
                    if (other_player == NULL || other_player->id == player->id)
                        continue;
                    SDL_AtomicSet(&other_player->i_need_attributes_of[player->id], true);
                }
            }
        }

        // Indeed this just sets the local inventory and ignores the current player that the other
        // updates apply to.
        if (flags & NETF_INVENTORY) {
            read_guy_inventory_from_buffer(buffer, &scene->guy, &pos);
            printf("received inventory\n");
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
    write_to_buffer(buffer, &guy->run_speed, pos, sizeof(float));
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

void netwrite_guy_mapstate(byte* buffer, RemotePlayer* player, Map* map, int* pos) {
    wait_for_then_use_lock(map->locked);

    float x = player->guy.position.x[X], y = player->guy.position.x[Y];
    write_to_buffer(buffer, &map->area_id, pos, sizeof(int));
    write_to_buffer(buffer, &x, pos, sizeof(float));
    write_to_buffer(buffer, &y, pos, sizeof(float));

    int size_pos = *pos;
    *pos += sizeof(int);

    int before_pos = *pos;
    write_map_state(map, buffer, pos);
    int bytes_wrote = (*pos) - size_pos;
    write_to_buffer(buffer, &bytes_wrote, &size_pos, sizeof(int));

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
    write_guy_inventory_to_buffer(buffer, &scene->guy, &pos);

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
    write_guy_inventory_to_buffer(buffer, &scene->guy, &pos);

    // Write players
    for (int i = 0; i < scene->net.number_of_players; i++) {
        RemotePlayer* player = scene->net.players[i];
        if (player == NULL || player->id == target_player->id)
            continue;

        buffer[pos++] = (byte)player->id;
        write_guy_info_to_buffer(buffer, &player->guy, SDL_AtomicGet(&player->area_id), &pos);
        write_guy_inventory_to_buffer(buffer, &player->guy, &pos);
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

    // The player that this thread is for.
    int player_id = -1;

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
            // scene->net.status = NOT_CONNECTED;
            r = 2; goto Done;
        }
        else if (recv_len == 0) {
            // TODO actual cleanup or something
            SET_LOCKED_STRING(scene->net.status_message, "Connection closed.");
            // scene->net.status = NOT_CONNECTED;
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
            player_id = scene->net.next_id++;
            RemotePlayer* player = allocate_new_player(scene, player_id, &loop->address);
            SDL_AtomicSet(&player->just_switched_maps, true);

            int pos = 0;
            buffer[pos++] = NETOP_HERES_YOUR_ID;
            buffer[pos++] = (byte)player_id;
            for (int i = 0; i < scene->net.number_of_players; i++)
                if (scene->net.players[i])
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
                SDL_assert(their_area_id == AREA_NET_ZONE);
                Map* their_map = cached_map(scene->game, map_asset_for_area(their_area_id));
                if (their_map->doors[1].flags & DOOR_VISIBLE)
                    printf("Netzone door visible!");
                else
                    printf("Netzone door INvisible!");
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
            RemotePlayer* player = netop_update_controls(scene, buffer, &loop->address);
            if (player) {
                SDL_AtomicSet(&player->poke, true);

                int pos = 0;

                memset(buffer, 0, PACKET_SIZE);

                buffer[pos++] = NETOP_UPDATE_CONTROLS;

                // Once we're done writing, we will write the number of bytes we wrote to the beginning of the buffer (as a short).
                int len_pos = pos;
                pos += sizeof(short);

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

                if (SDL_AtomicGet(&player->just_switched_maps)) {
                    *flags |= NETF_MAPSTATE;

                    int area_id = SDL_AtomicGet(&player->area_id);
                    Map* map = cached_map(scene->game, map_asset_for_area(area_id));
                    map->area_id = area_id;
                    netwrite_guy_mapstate(buffer, player, map, &pos);

                    SDL_AtomicSet(&player->just_switched_maps, false);
                    wait_for_then_use_lock(player->mob_event_buffer_locked);

                    player->mob_event_buffer_pos = 0;
                    player->mob_event_count = 0;

                    SDL_UnlockMutex(player->mob_event_buffer_locked);
                }

                if (player->mob_event_count > 0) {
                    *flags |= NETF_MOBEVENT;
                    // This will never deadlock with above, as mob_event_count will be 0 if it's locked from there.
                    wait_for_then_use_lock(player->mob_event_buffer_locked);

                    netwrite_guy_mobevents(buffer, player, &pos);

                    SDL_UnlockMutex(player->mob_event_buffer_locked);
                }

                if (SDL_AtomicGet(&scene->guy.dirty)) {
                    *flags |= NETF_ATTRIBUTES;
                    write_guy_info_to_buffer(buffer, &scene->guy, scene->current_area, &pos);
                    SDL_AtomicSet(&scene->guy.dirty, false);
                    printf("sent local guy attrs.\n");
                }

                if (SDL_AtomicGet(&player->i_want_inventory)) {
                    *flags |= NETF_INVENTORY;
                    write_guy_inventory_to_buffer(buffer, &player->guy, &pos);
                    SDL_AtomicSet(&player->i_want_inventory, false);
                    printf("sent inventory to %i\n", player->id);
                }

                // If there's more than just this guy playing with us, we need to send them the controls
                // of all other players..
                if (scene->net.number_of_players > 1) {
                    for (int i = 0; i < scene->net.number_of_players; i++) {
                        RemotePlayer* other_player = scene->net.players[i];
                        if (other_player == NULL || other_player->id == player->id)
                            continue;

                        ControlsBufferSpot* players_spot_of_other_player = &player->stream_spots_of[other_player->id];
                        wait_for_then_use_lock(other_player->controls_playback.locked);

                        byte* flags = NULL;

                        int other_player_area = SDL_AtomicGet(&other_player->area_id);
                        if (other_player_area != SDL_AtomicGet(&player->area_id)) {
                            if (other_player_area < 0)
                                goto UnlockAndContinue;

                            // Set spots so that they don't "fall behind"
                            players_spot_of_other_player->pos   = other_player->controls_playback.size;
                            players_spot_of_other_player->frame = other_player->controls_playback.bytes[0];

                            // We also need to inform player that other_player is not in their map...
                            buffer[pos++] = other_player->id;
                            flags = &buffer[pos++];
                            *flags = NETF_AREA | NETF_POSITION;
                            netwrite_guy_area(buffer, other_player_area, &pos);
                            netwrite_guy_position(buffer, &other_player->controls_playback, &other_player->guy, &pos);

                            goto PossiblySendAttributes;
                        }

                        if (
                            other_player->controls_playback.size > 0 &&
                            players_spot_of_other_player->frame >= 0 &&
                            players_spot_of_other_player->pos < other_player->controls_playback.size &&
                            players_spot_of_other_player->frame < other_player->controls_playback.bytes[0]
                        ) {
                            buffer[pos++] = other_player->id;
                            flags = &buffer[pos++];
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
                            goto UnlockAndContinue;
                        }
                    PossiblySendAttributes:
                        SDL_assert(flags != NULL);

                        if (SDL_AtomicGet(&player->i_need_attributes_of[other_player->id])) {
                            SDL_AtomicSet(&player->i_need_attributes_of[other_player->id], false);
                            *flags |= NETF_ATTRIBUTES;
                            write_guy_info_to_buffer(buffer, &other_player->guy, SDL_AtomicGet(&other_player->area_id), &pos);
                            printf("sent remote guy %i attrs\n", other_player->id);
                        }

                    UnlockAndContinue:
                        SDL_UnlockMutex(other_player->controls_playback.locked);
                    }
                }

                short size_wrote = (short)pos;
                write_to_buffer(buffer, &size_wrote, &len_pos, sizeof(short));

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
    if (player_id != -1) {
        for (int i = 0; i < scene->net.number_of_players; i++) {
            RemotePlayer* player = scene->net.players[i];
            if (player == NULL)
                continue;

            // free the player locally.
            if (player->id == player_id) {
                aligned_free(scene->net.players[i]);
                scene->net.players[i] = NULL;
            }
            // tell others to free them.
            else {
                char dc_msg[] = { NETOP_PLAYER_DC, (byte)player_id };
                send(player->socket, dc_msg, sizeof(dc_msg), 0);
            }
        }
    }

    aligned_free(loop);
    closesocket(loop->socket);
    return r;
}

// NOTE this should be the only door.callback for any door while hosting....
void remote_player_enters_net_zone_door(void* vs) {
    printf("REMOTE PLAYER ENTERS NET ZONE DOOR!\n");
    WorldScene* s = (WorldScene*)vs;
    SDL_assert(s->net.status == HOSTING);
    Map* net_zone = cached_map(s->game, map_asset_for_area(AREA_NET_ZONE));
    SDL_assert(s->current_area != AREA_NET_ZONE);
    net_zone->doors[1].dest_area = s->current_area;
    net_zone->doors[1].dest_x = s->guy.position.x[X];
    net_zone->doors[1].dest_y = s->guy.position.x[Y];
    if (net_zone->doors[1].flags & DOOR_INVERT_Y)
        net_zone->doors[1].flags ^= DOOR_INVERT_Y;
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
    else {
        Map* net_zone = cached_map(scene->game, map_asset_for_area(AREA_NET_ZONE));
        net_zone->area_id = AREA_NET_ZONE;

        if (net_zone->doors[0].flags & DOOR_VISIBLE)
            net_zone->doors[0].flags ^= DOOR_VISIBLE;

        net_zone->doors[1].flags |= DOOR_VISIBLE;
        net_zone->doors[1].callback = remote_player_enters_net_zone_door;
    }

    scene->net.remote_id = 0;
    scene->guy.player_id = 0;
    scene->net.next_id = 1;

    struct sockaddr_in other_address;

    wait_for_then_use_lock(scene->controls_stream.locked);
    scene->controls_stream.current_frame = 0;
    scene->controls_stream.pos = 1;
    SDL_UnlockMutex(scene->controls_stream.locked);

    listen(scene->net.local_socket, MAX_PLAYERS);

    scene->game->net.set_item = connected_set_item;
    scene->game->net.spawn_mob = connected_spawn_mob;
    scene->game->net.despawn_mob = connected_despawn_mob;

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

    while (scene->current_area != AREA_NET_ZONE) {
        SDL_Delay(1000);
    }

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
        SET_LOCKED_STRING(scene->net.status_message, "Failed to connect.");
        scene->net.status_message_countdown = 60 * 10;
        scene->net.status = NOT_CONNECTED;
        scene->map->doors[0].flags |= DOOR_VISIBLE;
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
            SDL_assert(scene->current_area == AREA_NET_ZONE);
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

            int len_pos = pos;
            pos += sizeof(short);

            buffer[pos++] = scene->net.remote_id;
            byte* flags = &buffer[pos++];
            *flags = NETF_CONTROLS;
            netwrite_guy_controls(
                buffer,
                &scene->controls_stream,
                NULL, &pos, false
            );

            if (SDL_AtomicGet(&scene->guy.dirty)) {
                *flags |= NETF_ATTRIBUTES;
                write_guy_info_to_buffer(buffer, &scene->guy, scene->current_area, &pos);
                SDL_AtomicSet(&scene->guy.dirty, false);
            }

            short size_wrote = (short)pos;
            write_to_buffer(buffer, &size_wrote, &len_pos, sizeof(short));

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
                scene->guy.player_id = scene->net.remote_id;
                // Allocate players for when update_position comes in
                SDL_assert(scene->net.number_of_players == 0);
                scene->net.players[scene->net.number_of_players++] = allocate_new_player(scene, 0, &other_address);
                for (int i = 2; i < recv_len; i++) {
                    // NOTE as client I guess all `other_address`es will just be the server's address..
                    scene->net.players[scene->net.number_of_players++] = allocate_new_player(scene, buffer[i], &other_address);
                }
                scene->net.connected = true;
                SET_LOCKED_STRING_F(scene->net.status_message, "Client ID: %i", scene->net.remote_id);
            } break;

            case NETOP_INITIALIZE_PLAYER: {
                RemotePlayer* player = netop_initialize_state(scene, buffer, NULL);

                if (player->id == 0) {
                    SDL_assert(scene->current_area == AREA_NET_ZONE);
                    Door* door_there = &scene->map->doors[1];
                    // NOTE this stuff will be overridden by the server anyways, but whatever.
                    door_there->dest_area = SDL_AtomicGet(&player->area_id);
                    door_there->dest_x = player->guy.position.x[X];
                    door_there->dest_y = player->guy.position.x[Y];
                    door_there->flags = DOOR_VISIBLE;
                }

            } break;

            case NETOP_UPDATE_CONTROLS: {
                RemotePlayer* player = netop_update_controls(scene, buffer, NULL);
                if (player == NULL) {
                    printf("UPDATING FORF BULLSHIT PLAYER\n");
                }
                else {
                    SDL_AtomicSet(&player->poke, true);
                }
            } break;

            case NETOP_PLAYER_DC: {
                int pos = 1;
                byte player_id = buffer[pos++];
                for (int i = 0; i < scene->net.number_of_players; i++) {
                    if (scene->net.players[i] && scene->net.players[i]->id == player_id) {
                        aligned_free(scene->net.players[i]);
                        scene->net.players[i] = NULL;
                        printf("Disconnected player %i\n", i);
                        break;
                    }
                }
                goto Receive;
            } break;

            default:
                break;
            }
        }
    }

    closesocket(scene->net.local_socket);
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

void net_host(WorldScene* data) {
    data->net.status = HOSTING;
    SDL_CreateThread(network_server_listen, "Network server listen", data);
}


void create_client_thread(void* vs) {
    WorldScene* s = (WorldScene*)vs;
    Map* net_zone = cached_map(s->game, map_asset_for_area(AREA_NET_ZONE));
    net_zone->area_id = AREA_NET_ZONE;

    Door* door_back  = &net_zone->doors[0];
    Door* door_there = &net_zone->doors[1];

    door_back->flags = 0;
    if (s->current_area != AREA_NET_ZONE) {
        door_back->dest_area = s->current_area;
        door_back->dest_x = s->guy.position.x[X];
        door_back->dest_y = s->guy.position.x[Y];
    }

    door_there->flags = 0;

    s->net.status = JOINING;
    s->game->net_joining = true;
    SDL_CreateThread(network_client_loop, "Network client loop", vs);
    printf("Created client loop thread\n");
}

void net_join(WorldScene* s) {
    s->net.status = NOT_CONNECTED;
    if (s->current_area == AREA_NET_ZONE) {
        create_client_thread(s);
    }
    else {
        wait_for_then_use_lock(s->transition.locked);
        s->transition.destination_area = AREA_NET_ZONE;
        s->transition.destination_position.x = 69.0f;
        s->transition.destination_position.y = 512.0f;
        s->transition.progress_percent = 0;
        s->transition.on_transition = create_client_thread;
        SDL_UnlockMutex(s->transition.locked);
    }
}

mrb_value rb_world(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    if (game->current_scene->id == SCENE_WORLD) {
        WorldScene* scene = (WorldScene*)game->current_scene_data;
        return scene->script_obj;
    }
    else {
        return mrb_nil_value();
    }
}

mrb_value mrb_world_host(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    if (scene->net.status != NOT_CONNECTED) {
        mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), "Can't host while already in a netgame");
        return mrb_nil_value();
    }

    mrb_value port = mrb_nil_value();
    mrb_get_args(mrb, "|o", &port);
    if (!mrb_nil_p(port)) {
        if (!mrb_string_p(port))
            if (mrb_respond_to(mrb, port, mrb_intern_lit(mrb, "to_s")))
                port = mrb_funcall(mrb, port, "to_s", 0);
            else
                goto just_connect;

        char* port_cstr = mrb_string_value_cstr(mrb, &port);
        SDL_strlcpy(scene->net.textinput_port, port_cstr, EDITABLE_TEXT_BUFFER_SIZE);
    }

just_connect:
    net_host(scene);
    return mrb_true_value();
}
mrb_value mrb_world_join(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    if (scene->net.status != NOT_CONNECTED) {
        mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), "Can't join while already in a netgame");
        return mrb_nil_value();
    }

    mrb_value addr = mrb_nil_value();
    mrb_get_args(mrb, "|o", &addr);
    if (!mrb_nil_p(addr)) {
        if (!mrb_string_p(addr))
            if (mrb_respond_to(mrb, addr, mrb_intern_lit(mrb, "to_s")))
                addr = mrb_funcall(mrb, addr, "to_s", 0);
            else
                goto just_connect;

        char* addr_cstr = mrb_string_value_cstr(mrb, &addr);
        SDL_strlcpy(scene->net.textinput_ip_address, addr_cstr, EDITABLE_TEXT_BUFFER_SIZE);
    }

just_connect:
    net_join(scene);
    return mrb_true_value();
}

mrb_value mrb_world_is_connected(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    return scene->net.status == NOT_CONNECTED ? mrb_false_value() : mrb_true_value();
}
mrb_value mrb_world_is_hosting(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    return scene->net.status == HOSTING ? mrb_true_value() : mrb_false_value();
}
mrb_value mrb_world_is_joining(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    return scene->net.status == JOINING ? mrb_true_value() : mrb_false_value();
}

void scene_world_initialize(void* vdata, Game* game) {
    WorldScene* data = (WorldScene*)vdata;
    data->game = game;
    SDL_memset(data->editable_text, 0, sizeof(data->editable_text));
    SDL_strlcat(data->editable_text, "hey", EDITABLE_TEXT_BUFFER_SIZE);

    game->net.set_item = unconnected_set_item;
    game->net.spawn_mob = unconnected_spawn_mob;
    game->net.despawn_mob = unconnected_despawn_mob;

    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->drag = 0.025f; // Again p/s^2

    data->controls_stream.current_frame = -1;
    data->controls_stream.pos = 1;

    data->transition.door_flags = (DOOR_INVERT_Y | DOOR_VISIBLE);
    data->transition.on_transition = NULL;
    data->transition.map_data = NULL;
    data->transition.progress_percent = 101;
    data->transition.locked = SDL_CreateMutex();
    SDL_AtomicSet(&data->transition.map_data_status, MAP_DATA_NOT_NEEDED);
    if (!data->transition.locked) {
        printf("BAD TRANSITION MUTEX\n");
    }

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
    SDL_assert(!game->mrb->exc);
    default_character(game, &data->guy);
    SDL_assert(!game->mrb->exc);
    default_character_animations(game, &data->guy);
    SDL_assert(!game->mrb->exc);
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

    data->inv_fade = INV_FADE_MAX;
    data->inv_fade_countdown = 0;

    SDL_assert(((int)&data->guy.position) % 16 == 0);
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
    BENCH_END(loading_sound);

    for (int i = 0; i < game->argc; i++) {
        if (strcmp(game->argv[i], "-h") == 0 || strcmp(game->argv[i], "--host") == 0) {
            printf("Starting server now!!!\n");
            net_host(data);
        }
    }

    // Add ruby object to game!
    SDL_assert(!game->mrb->exc);
    mrb_iv_check(game->mrb, game->ruby.sym_atworld);
    SDL_assert(!game->mrb->exc);
    data->script_obj = mrb_obj_new(game->mrb, game->ruby.world_class, 0, NULL);
    mrb_data_init(data->script_obj, data, &mrb_world_type);
    mrb_iv_set(game->mrb, game->ruby.game, game->ruby.sym_atworld, data->script_obj);

    mrb_iv_check(game->mrb, game->ruby.sym_atgame);
    mrb_iv_set(game->mrb, data->script_obj, game->ruby.sym_atgame, game->ruby.game);

    data->rguy = mrb_obj_new(game->mrb, game->ruby.character_class, 0, NULL);
    mrb_data_init(data->rguy, &data->guy, &mrb_character_type);

    mrb_define_singleton_method(game->mrb, game->mrb->top_self, "world", rb_world, MRB_ARGS_NONE());
}

void write_mob_events(void* vs, Map* map, struct Game* game, MobCommon* mob) {
    WorldScene* s = (WorldScene*)vs;

    byte sync_buffer[MOB_EVENT_BUFFER_SIZE];
    int size = 0;

    MobType* reg = &mob_registry[mob->mob_type_id];
    if (reg->sync_send != NULL && reg->sync_send(mob, map, sync_buffer, &size)) {
        SDL_assert(size <= MOB_EVENT_BUFFER_SIZE);
        if (size == 0) {
            printf("WARNING: About to write mob sync of size 0 for some reason.\n");
        }

        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player == NULL || SDL_AtomicGet(&player->area_id) != map->area_id || SDL_AtomicGet(&player->just_switched_maps))
                continue;
            wait_for_then_use_lock(player->mob_event_buffer_locked);
            SDL_assert(player->mob_event_buffer_pos <= MOB_EVENT_BUFFER_SIZE && player->mob_event_buffer_pos >= 0);
            SDL_assert(player->mob_event_buffer_pos + size < MOB_EVENT_BUFFER_SIZE);

            int m_id = mob_id(map, mob);

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
            const int frames_behind_threshold = (s->net.status == HOSTING ? plr->ping : s->net.players[0]->ping) * 2 + 1;

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

                apply_character_inventory(&plr->guy, &plr->controls, game, map);
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

    // Update local player and map transition
    bool updated_guy_physics = false;
    if (s->transition.progress_percent <= 100) {
        int map_data_status = SDL_AtomicGet(&s->transition.map_data_status);
        if (map_data_status == MAP_DATA_NOT_NEEDED)
            s->transition.progress_percent += TRANSITION_STEP;
        // We still want to increment when we receive map data before the transition reaches 50%.
        else if (map_data_status == MAP_DATA_RECEIVED && s->transition.progress_percent < TRANSITION_POINT)
            s->transition.progress_percent += TRANSITION_STEP;

        if (s->transition.progress_percent == TRANSITION_POINT) {
            if (s->net.status == JOINING) {
                switch (SDL_AtomicGet(&s->transition.map_data_status)) {
                case MAP_DATA_NOT_NEEDED:
                    SDL_AtomicSet(&s->transition.map_data_status, MAP_DATA_NEED);
                    break;

                case MAP_DATA_RECEIVED: {
                    SDL_assert(s->transition.map_data);
                    transition_maps(s, game, &s->transition);
                    int pos = 0;
                    clear_map_state(s->map);
                    read_map_state(s->map, s->transition.map_data, &pos);
                    aligned_free(s->transition.map_data);
                    s->transition.map_data = NULL;

                    SDL_AtomicSet(&s->transition.map_data_status, MAP_DATA_NOT_NEEDED);
                } break;
                }
            }
            else
                transition_maps(s, game, &s->transition);
        }
    }
    if (s->transition.progress_percent > TRANSITION_POINT) {
        switch (apply_character_inventory(&s->guy, &game->controls, game, s->map)) {
        case INV_TOGGLE:
            if (s->inv_fade_countdown > 0) {
                s->inv_fade_countdown = 0;
                break;
            }

        case INV_ACTION:
            s->inv_fade_countdown = 60 * 2;
            break;

        default:
            s->inv_fade_countdown -= 1;
        }

        if (s->inv_fade_countdown <= 0) {
            s->inv_fade += 1;
            if (s->inv_fade > INV_FADE_MAX) {
                s->inv_fade = INV_FADE_MAX;
            }
        }
        else {
            s->inv_fade -= 1;
            if (s->inv_fade < 0)
                s->inv_fade = 0;
        }

        apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
        collide_character(&s->guy, &s->map->tile_collision);
        slide_character(s->gravity, &s->guy);
        interact_character_with_world(game, &s->guy, &game->controls, s->map, s, local_go_through_door);
        update_character_animation(&s->guy);
        updated_guy_physics = true;
    }

    // Update everything else on the map(s)
    void(*after_mob_update)(void*, Map*, struct Game*, MobCommon*) = NULL;
    if (s->net.status == HOSTING)
        after_mob_update = write_mob_events;

    SDL_assert(s->map->area_id == s->current_area);
    update_map(s->map, game, s, after_mob_update);
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
            update_map(map, game, s, after_mob_update);
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
        if (updated_guy_physics)
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
            s->net.status = WANT_TO_JOIN;
        }
    }
    else {
        if (game->text_edit.canceled) {
            s->net.status = NOT_CONNECTED;
            game->net_joining = false;
        }
        if (game->text_edit.enter_pressed) {
            stop_editing_text(game);
            switch (s->net.status) {
            case HOSTING:
                net_host(s);
                break;
            case JOINING: case WANT_TO_JOIN:
                net_join(s);
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
        draw_character(game, &s->guy, s->guy.view);
        for (int i = 0; i < s->net.number_of_players; i++) {
            RemotePlayer* player = s->net.players[i];
            if (player && SDL_AtomicGet(&player->area_id) == s->current_area)
                draw_character(game, &player->guy, player->guy.view);
        }
    }
    BENCH_END(characters);

    // Draw inventory
    BENCH_START(inventory);
    {
        if (s->inv_fade >= INV_FADE_MAX)
            goto done_with_inventory;

        wait_for_then_use_lock(s->guy.inventory.locked);

        // Space between slots
        const float base_padding = 3.0f;
        const int selection_bump = 4;
        const int render_y = game->window_height - 36;
        const int render_x_start = (game->window_width / 2) - ((s->guy.inventory.capacity * 32) / 2);

        int x_padding = (int)((1.0f - (float)s->inv_fade / (float)INV_FADE_ZERO_POINT) * base_padding);
        int y_padding = base_padding;

        SDL_Rect bar = {
            render_x_start - base_padding, render_y - y_padding,

            (s->guy.inventory.capacity * 32) + base_padding + (x_padding * s->guy.inventory.capacity),
            32 + y_padding * 2
        };
        SDL_Rect top_bar = bar;
        top_bar.h /= 2;
        SDL_Rect bot_bar = top_bar;
        bot_bar.y += top_bar.h;

        SDL_Color left_color = s->guy.left_foot_color;
        SDL_Color right_color = s->guy.right_foot_color;
        SDL_Color slot_color = s->guy.body_color;

        SDL_SetRenderDrawBlendMode(game->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(game->renderer, left_color.r, left_color.g, left_color.b, 200);
        SDL_RenderFillRect(game->renderer, &top_bar);
        SDL_SetRenderDrawColor(game->renderer, right_color.r, right_color.g, right_color.b, 200);
        SDL_RenderFillRect(game->renderer, &bot_bar);
        SDL_SetRenderDrawBlendMode(game->renderer, SDL_BLENDMODE_NONE);

        SDL_Texture* slot_tex = cached_texture(game, ASSET_MISC_SLOT_PNG);
        for (int i = 0; i < s->guy.inventory.capacity; i++) {
            ItemCommon* item = &s->guy.inventory.items[i];

            SDL_Rect dest = {
                render_x_start + (i * 32) + (i * x_padding), render_y,
                32, 32
            };
            if (s->inv_fade_countdown > 0 && i == s->guy.selected_slot)
                dest.y -= selection_bump;

            SDL_SetTextureColorMod(slot_tex, slot_color.r, slot_color.g, slot_color.b);
            SDL_RenderCopy(game->renderer, slot_tex, NULL, &dest);

            if (item->item_type_id != ITEM_NONE && i != s->guy.grabbed_slot) {
                SDL_assert(item->item_type_id < NUMBER_OF_ITEM_TYPES);

                ItemType* reg = &item_registry[item->item_type_id];
                reg->render(item, game, &dest);
            }
        }

        if (s->guy.grabbed_slot >= 0 && s->guy.grabbed_slot < s->guy.inventory.capacity) {
            int sel  = s->guy.selected_slot;
            int grab = s->guy.grabbed_slot;
            ItemCommon* item = &s->guy.inventory.items[grab];
            ItemType* reg    = &item_registry[item->item_type_id];

            SDL_Rect dest = {
                render_x_start + (sel * 32) + (sel * x_padding), render_y - 32 - y_padding * 2,
                32, 32
            };

            reg->render(item, game, &dest);
        }

        SDL_UnlockMutex(s->guy.inventory.locked);
    }
done_with_inventory:;
    BENCH_END(inventory);

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
    if (s->net.status != NOT_CONNECTED || s->net.status_message_countdown > 0) {
        set_text_color(game, 255, 50, 50);
        draw_text(game, 10, game->window_height - 50, s->net.status_message);
        if (s->net.status_message_countdown > 0)
            s->net.status_message_countdown -= 1;
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

    if (s->transition.progress_percent < 100) {
        float alpha = (float)s->transition.progress_percent * (255.0f / (float)TRANSITION_POINT);
        if (alpha > 255)
            alpha = 255 - (alpha - 255);

        SDL_BlendMode blend;
        SDL_GetRenderDrawBlendMode(game->renderer, &blend);

        SDL_SetRenderDrawBlendMode(game->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, (byte)alpha);
        SDL_RenderFillRect(game->renderer, NULL);
        SDL_SetRenderDrawBlendMode(game->renderer, blend);
    }
}

void scene_world_cleanup(void* vdata, Game* game) {
    WorldScene* data = (WorldScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;

    closesocket(data->net.local_socket);
}

mrb_value mrb_world_init(mrb_state* mrb, mrb_value self) {
    mrb_data_init(self, NULL, &mrb_world_type);
    return self;
}

mrb_value mrb_world_current_map(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    if (scene == NULL) {
        mrb_raise(mrb, mrb->eStandardError_class, "Invalid world");
        return mrb_nil_value();
    }
    mrb_sym atmap = scene->game->ruby.sym_atmap;

    mrb_value rmap;
    if (mrb_iv_defined(mrb, self, scene->game->ruby.sym_atmap)) {
        rmap = mrb_iv_get(mrb, self, atmap);
    }
    else {
        rmap = mrb_obj_new(mrb, scene->game->ruby.map_class, 0, NULL);
        mrb_iv_set(mrb, self, atmap, rmap);
    }
    mrb_data_init(rmap, scene->map, &mrb_map_type);
    return rmap;
}

mrb_value mrb_world_local_character(mrb_state* mrb, mrb_value self) {
    WorldScene* scene = DATA_PTR(self);
    return scene->rguy;
}
