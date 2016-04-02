#ifndef SCRIPT_H
#define SCRIPT_H

#include "types.h"
#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/proc.h>
#include <mruby/dump.h>
#include <mruby/array.h>
#include <mruby/hash.h>

#define MRB_CFUNC(name) mrb_value name(mrb_state* mrb, mrb_value self)

mrb_value mrb_instance_alloc(mrb_state *mrb, struct RClass* c);

vec2 mrb_vec2(mrb_state* mrb, mrb_value value);

static void mrb_dont_free(mrb_state* mrb, void* p) {
    printf("oooooooooo boi");
}

static const struct mrb_data_type mrb_free_type = { "MrbFree", mrb_free };
static const struct mrb_data_type mrb_dont_free_type = { "DontFree", mrb_dont_free };
static const struct mrb_data_type mrb_color_type = { "Color", mrb_free };
static const struct mrb_data_type mrb_controls_type = { "Controls", mrb_free };
static const struct mrb_data_type mrb_game_type = { "Game", mrb_dont_free };
static const struct mrb_data_type mrb_world_type = { "World", mrb_dont_free };
static const struct mrb_data_type mrb_map_type = { "Map", mrb_dont_free };
// TODO perhaps a special free method that despawns the mob if it's spawned?
// We don't currently have any mob spawning.
static const struct mrb_data_type mrb_mob_type = { "Mob", mrb_free };
static const struct mrb_data_type mrb_character_type = { "Character", mrb_dont_free };

MRB_CFUNC(mrb_controls_init);
MRB_CFUNC(mrb_controls_just_pressed);
MRB_CFUNC(mrb_controls_just_released);

MRB_CFUNC(mrb_game_init);
MRB_CFUNC(mrb_game_controls);
MRB_CFUNC(mrb_game_world);

MRB_CFUNC(mrb_world_init);

// These are defined in scene_world.c of course
MRB_CFUNC(mrb_world_host);
MRB_CFUNC(mrb_world_join);
MRB_CFUNC(mrb_world_is_connected);
MRB_CFUNC(mrb_world_is_hosting);
MRB_CFUNC(mrb_world_is_joining);
MRB_CFUNC(mrb_world_save);
MRB_CFUNC(mrb_world_area);
MRB_CFUNC(mrb_world_area_eq);

MRB_CFUNC(mrb_world_current_map);
MRB_CFUNC(mrb_world_local_character);

MRB_CFUNC(mrb_map_init);
// MRB_CFUNC(mrb_map_spawn_mob);

// MRB_CFUNC(mrb_mob_init);

struct Game;
void script_init(struct Game* game);
#ifdef _DEBUG
bool load_script_file(mrb_state* mrb);
#else
#define load_script_file(x) (false)
#endif
void ruby_p(mrb_state* mrb, mrb_value obj, int prompt);

#endif
