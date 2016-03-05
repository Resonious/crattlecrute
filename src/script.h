#ifndef SCRIPT_H
#define SCRIPT_H

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/data.h>
#include <mruby/string.h>

// In other words: don't.
static void mrb_free_game(mrb_state* mrb, void* p) { }

static const struct mrb_data_type mrb_controls_type = { "Controls", mrb_free };
static const struct mrb_data_type mrb_game_type = { "Game", mrb_free_game };

mrb_value mrb_controls_init(mrb_state* mrb, mrb_value self);
mrb_value mrb_controls_just_pressed(mrb_state* mrb, mrb_value self);
mrb_value mrb_controls_just_released(mrb_state* mrb, mrb_value self);
mrb_value mrb_game_init(mrb_state* mrb, mrb_value self);
mrb_value mrb_game_controls(mrb_state* mrb, mrb_value self);
struct Game;
void script_init(struct Game* game);
void ruby_p(mrb_state *mrb, mrb_value obj, int prompt);

#endif