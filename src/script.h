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

static void mrb_dont_free(mrb_state* mrb, void* p) { }

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

mrb_value mrb_controls_init(mrb_state* mrb, mrb_value self);
mrb_value mrb_controls_just_pressed(mrb_state* mrb, mrb_value self);
mrb_value mrb_controls_just_released(mrb_state* mrb, mrb_value self);

mrb_value mrb_game_init(mrb_state* mrb, mrb_value self);
mrb_value mrb_game_controls(mrb_state* mrb, mrb_value self);
mrb_value mrb_game_world(mrb_state* mrb, mrb_value self);

mrb_value mrb_world_init(mrb_state* mrb, mrb_value self);

// These are defined in scene_world.c of course
mrb_value mrb_world_host(mrb_state* mrb, mrb_value self);
mrb_value mrb_world_join(mrb_state* mrb, mrb_value self);
mrb_value mrb_world_is_connected(mrb_state* mrb, mrb_value self);
mrb_value mrb_world_is_hosting(mrb_state* mrb, mrb_value self);
mrb_value mrb_world_is_joining(mrb_state* mrb, mrb_value self);

mrb_value mrb_world_current_map(mrb_state* mrb, mrb_value self);
mrb_value mrb_world_local_character(mrb_state* mrb, mrb_value self);

mrb_value mrb_map_init(mrb_state* mrb, mrb_value self);
// mrb_value mrb_map_spawn_mob(mrb_state* mrb, mrb_value self);

// mrb_value mrb_mob_init(mrb_state* mrb, mrb_value self);

struct Game;
void script_init(struct Game* game);
#ifdef _DEBUG
bool load_script_file(mrb_state* mrb);
#else
#define load_script_file(x) (false)
#endif
void ruby_p(mrb_state* mrb, mrb_value obj, int prompt);

#endif