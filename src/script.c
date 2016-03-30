#include "script.h"
#include "game.h"
#include "character.h"

// NOTE this is largely copied from mruby/src/class.c
// not sure why this isn't an MRB_API function.
mrb_value mrb_instance_alloc(mrb_state* mrb, struct RClass* c) {
  struct RObject *o;
  enum mrb_vtype ttype = MRB_INSTANCE_TT(c);

  SDL_assert(c->tt != MRB_TT_SCLASS);

  if (ttype == 0) ttype = MRB_TT_OBJECT;
  o = (struct RObject*)mrb_obj_alloc(mrb, ttype, c);
  return mrb_obj_value(o);
}

float mrb_to_f(mrb_state* mrb, mrb_value v) {
    switch (v.tt) {
    case MRB_TT_FLOAT:
        return v.value.f;
    case MRB_TT_FIXNUM:
        return (float)v.value.i;
    default: {
        mrb_value r = mrb_funcall(mrb, v, "to_f", 0);
        return r.value.f;
    }
    }
}

vec2 mrb_vec2(mrb_state* mrb, mrb_value value) {
    Game* game = (Game*)mrb->ud;

    if (mrb_array_p(value)) {
        mrb_value* array = RARRAY_PTR(value);
        int len = RARRAY_LEN(value);
        if (len < 2)
            return (vec2) { mrb_to_f(mrb, array[0]), mrb_to_f(mrb, array[0]) };
        else
            return (vec2) { mrb_to_f(mrb, array[0]), mrb_to_f(mrb, array[1]) };
    }
    else if (mrb_obj_class(mrb, value) == game->ruby.vec2_class) {
        return *(vec2*)DATA_PTR(value);
    }
    else
        return (vec2){0, 0};
}

mrb_value mrb_debug_game_bench(mrb_state* mrb, mrb_value self) {
  return _bench_hash;
}

mrb_value mrb_sym_to_i(mrb_state* mrb, mrb_value self) {
    return mrb_fixnum_value(self.value.i);
}

mrb_value mrb_int_to_sym(mrb_state* mrb, mrb_value self) {
    return mrb_symbol_value(self.value.sym);
}

mrb_value mrb_vec2_init(mrb_state* mrb, mrb_value self) {
    mrb_float x = 0.0f, y = 0.0f;
    mrb_get_args(mrb, "|ff", &x, &y);

    vec2* u = mrb_malloc(mrb, sizeof(vec2));
    u->x = x;
    u->y = y;
    mrb_data_init(self, u, &mrb_free_type);
    return self;
}

mrb_value mrb_vec2_inspect(mrb_state* mrb, mrb_value self) {
    vec2* u = DATA_PTR(self);

    char strbuf[64];
    sprintf(strbuf, "Vec2 { %f, %f }", u->x, u->y);

    return mrb_str_new_cstr(mrb, strbuf);
}

mrb_value mrb_vec2_x(mrb_state* mrb, mrb_value self) {
    vec2* u = DATA_PTR(self);
    return mrb_float_value(mrb, u->x);
}

mrb_value mrb_vec2_x_eq(mrb_state* mrb, mrb_value self) {
    vec2* u = DATA_PTR(self);
    mrb_float x;
    mrb_get_args(mrb, "f", &x);
    u->x = x;
    return mrb_float_value(mrb, u->x);
}

mrb_value mrb_vec2_y(mrb_state* mrb, mrb_value self) {
    vec2* u = DATA_PTR(self);
    return mrb_float_value(mrb, u->y);
}

mrb_value mrb_vec2_y_eq(mrb_state* mrb, mrb_value self) {
    vec2* u = DATA_PTR(self);
    mrb_float y;
    mrb_get_args(mrb, "f", &y);
    u->y = y;
    return mrb_float_value(mrb, u->y);
}

mrb_value mrb_color_init(mrb_state* mrb, mrb_value self) {
    mrb_int r = 0, g = 0, b = 0, a = 255;
    mrb_get_args(mrb, "|iiii", &r, &g, &b, &a);

    SDL_Color* color = mrb_malloc(mrb, sizeof(SDL_Color));
    color->r = r;
    color->g = g;
    color->b = b;
    color->a = a;
    mrb_data_init(self, color, &mrb_color_type);
    return self;
}

mrb_value mrb_color_inspect(mrb_state* mrb, mrb_value self) {
    SDL_Color* color = DATA_PTR(self);

    char strbuf[64];
    sprintf(strbuf, "Color {r=%i,g=%i,b=%i,a=%i}", color->r, color->g, color->b, color->a);

    return mrb_str_new_cstr(mrb, strbuf);
}

#define DEF_RUBY_COLOR_ATTR(x) \
    mrb_value mrb_color_##x(mrb_state* mrb, mrb_value self) { \
        SDL_Color* color = DATA_PTR(self); \
        return mrb_fixnum_value(color->x); \
    } \
    mrb_value mrb_color_##x##_eq(mrb_state* mrb, mrb_value self) { \
        SDL_Color* color = DATA_PTR(self); \
        mrb_value value; \
        mrb_get_args(mrb, "i", &value); \
        color->x = mrb_fixnum(value); \
        return mrb_fixnum_value(color->x); \
    }

DEF_RUBY_COLOR_ATTR(r);
DEF_RUBY_COLOR_ATTR(g);
DEF_RUBY_COLOR_ATTR(b);
DEF_RUBY_COLOR_ATTR(a);

mrb_value mrb_controls_init(mrb_state* mrb, mrb_value self) {
    mrb_bool dont_allocate;
    mrb_get_args(mrb, "|b", &dont_allocate);

    if (!dont_allocate) {
        Controls* new_controls;

        if (DATA_PTR(self) != NULL)
            new_controls = DATA_PTR(self);
        else
            mrb_malloc(mrb, sizeof(Controls));

        memset(new_controls, 0, sizeof(Controls));
        mrb_data_init(self, new_controls, &mrb_controls_type);
    }
    else {
        mrb_data_init(self, NULL, &mrb_controls_type);
    }

    return self;
}

mrb_value mrb_controls_just_pressed(mrb_state* mrb, mrb_value self) {
    Controls* controls = DATA_PTR(self);

    mrb_int arg;
    mrb_get_args(mrb, "i", &arg);

    return mrb_bool_value(just_pressed(controls, arg));
}

mrb_value mrb_controls_just_released(mrb_state* mrb, mrb_value self) {
    Controls* controls = DATA_PTR(self);

    mrb_int arg;
    mrb_get_args(mrb, "i", &arg);

    return mrb_bool_value(just_released(controls, arg));
}

mrb_value mrb_controls_access(mrb_state* mrb, mrb_value self) {
    Controls* controls = DATA_PTR(self);

    mrb_int arg;
    mrb_get_args(mrb, "i", &arg);

    if (arg < 0 || arg > NUM_CONTROLS)
        return mrb_nil_value();

    return mrb_bool_value(controls->this_frame[arg]);
}

mrb_value mrb_controls_access_eq(mrb_state* mrb, mrb_value self) {
    Controls* controls = DATA_PTR(self);

    mrb_int index;
    mrb_bool value;
    mrb_get_args(mrb, "ib", &index, &value);

    controls->this_frame[index] = value;

    return mrb_fixnum_value(value);
}

#define RUBY_CONST_CTRL(ctrl, rctrl)\
    mrb_const_set(\
        game->mrb, mrb_obj_value(game->ruby.controls_class),\
        mrb_intern_lit(game->mrb, #rctrl), mrb_fixnum_value(ctrl)\
    )

mrb_value mrb_game_init(mrb_state* mrb, mrb_value self) {
    mrb_data_init(self, NULL, &mrb_game_type);
    return self;
}

mrb_value mrb_game_controls(mrb_state* mrb, mrb_value self) {
    Game* game = DATA_PTR(self);

    mrb_value rcontrols;

    mrb_iv_check(mrb, game->ruby.sym_atcontrols);
    if (!mrb_iv_defined(mrb, self, game->ruby.sym_atcontrols)) {
        mrb_value rtrue = mrb_true_value();
        rcontrols = mrb_class_new_instance(mrb, 1, &rtrue, game->ruby.controls_class);
        mrb_data_init(rcontrols, &game->controls, &mrb_controls_type);
        mrb_iv_set(game->mrb, self, game->ruby.sym_atcontrols, rcontrols);
    }
    else {
        rcontrols = mrb_iv_get(mrb, self, game->ruby.sym_atcontrols);
    }

    return rcontrols;
}

mrb_value mrb_game_world(mrb_state* mrb, mrb_value self) {
    Game* game = DATA_PTR(self);
    return mrb_iv_get(mrb, self, game->ruby.sym_atworld);
}

mrb_value mrb_game_save(mrb_state* mrb, mrb_value self) {
    return mrb_bool_value(save_game((Game*)DATA_PTR(self)));
}

mrb_value mrb_map_init(mrb_state* mrb, mrb_value self) {
    mrb_data_init(self, NULL, &mrb_map_type);
    return self;
}

mrb_value mrb_map_game(mrb_state* mrb, mrb_value self) {
    Map* map = DATA_PTR(self);
    return mrb_iv_get(mrb, self, map->game->ruby.sym_atgame);
}

mrb_value mrb_map_spawn_script_mob(mrb_state* mrb, mrb_value self) {
    Map* map = DATA_PTR(self);
    if (map == NULL) {
        mrb_raise(mrb, mrb->eStandardError_class, "NULL map");
        return mrb_nil_value();
    }

    printf("no can do yet\n");
    return mrb_nil_value();
}

mrb_value mrb_character_init(mrb_state* mrb, mrb_value self) {
    mrb_data_init(self, NULL, &mrb_character_type);
    return self;
}

mrb_value mrb_character_age(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    return mrb_fixnum_value(guy->age);
}

mrb_value mrb_character_age_of_maturity(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    return mrb_fixnum_value(guy->age_of_maturity);
}

mrb_value mrb_character_mark_dirty(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    SDL_AtomicSet(&guy->dirty, true);
    return mrb_true_value();
}

mrb_value mrb_character_randomize(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    randomize_character(guy);
    return mrb_true_value();
}

mrb_value mrb_character_body_type(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    Game* game = (Game*)mrb->ud;
    return mrb_hash_get(mrb, game->ruby.cc_type_to_sym, mrb_fixnum_value(guy->body_type));
}

mrb_value mrb_character_feet_type(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    Game* game = (Game*)mrb->ud;
    return mrb_hash_get(mrb, game->ruby.cc_type_to_sym, mrb_fixnum_value(guy->feet_type));
}

mrb_value mrb_character_body_type_eq(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    Game* game = (Game*)mrb->ud;
    mrb_value nil = mrb_nil_value();

    mrb_sym value_sym;
    mrb_get_args(mrb, "n", &value_sym);

    mrb_value value_int = mrb_hash_get(mrb, game->ruby.cc_sym_to_type, mrb_symbol_value(value_sym));
    if (mrb_equal(mrb, value_int, nil))
        return mrb_false_value();

    guy->body_type = mrb_fixnum(value_int);
    load_character_atlases(game, guy);

    return mrb_true_value();
}

mrb_value mrb_character_feet_type_eq(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    Game* game = (Game*)mrb->ud;
    mrb_value nil = mrb_nil_value();

    mrb_sym value_sym;
    mrb_get_args(mrb, "n", &value_sym);

    mrb_value value_int = mrb_hash_get(mrb, game->ruby.cc_sym_to_type, mrb_symbol_value(value_sym));
    if (mrb_equal(mrb, value_int, nil))
        return mrb_false_value();

    guy->feet_type = mrb_fixnum(value_int);
    load_character_atlases(game, guy);

    return mrb_true_value();
}

#define SIMPLE_CHARACTER_RATTR(name)\
    mrb_value mrb_character_##name(mrb_state* mrb, mrb_value self) { \
        Character* guy = DATA_PTR(self); \
        return guy->r##name; \
    } \
    mrb_value mrb_character_##name##_eq(mrb_state* mrb, mrb_value self) { \
        Character* guy  = DATA_PTR(self); \
        Game* game = (Game*)mrb->ud; \
        mrb_value value; \
        mrb_get_args(mrb, "o", &value); \
        struct RClass* value_class = mrb_obj_class(mrb, value); \
        if (value_class == game->ruby.color_class) { \
            SDL_Color* color = DATA_PTR(value); \
            guy->name = *color; \
            return guy->r##name; \
        } \
        else if (value_class == NULL) \
            mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), "Passed value has no class!"); \
        else { \
            char err[128]; sprintf(err, "Expected Color, got %s", mrb_class_name(mrb, value_class)); \
            mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), err); \
        } \
        return mrb_nil_value(); \
    }

#define FLOAT_CHARACTER_ATTR(name)\
    mrb_value mrb_character_##name(mrb_state* mrb, mrb_value self) { \
        Character* guy = DATA_PTR(self); \
        return mrb_float_value(mrb, guy->name); \
    } \
    mrb_value mrb_character_##name##_eq(mrb_state* mrb, mrb_value self) { \
        Character* guy = DATA_PTR(self); \
        mrb_value value; \
        mrb_get_args(mrb, "f", &value); \
        guy->name = mrb_float(value); \
        return mrb_float_value(mrb, guy->name); \
    }

SIMPLE_CHARACTER_RATTR(body_color);
SIMPLE_CHARACTER_RATTR(eye_color);
SIMPLE_CHARACTER_RATTR(left_foot_color);
SIMPLE_CHARACTER_RATTR(right_foot_color);
FLOAT_CHARACTER_ATTR(ground_speed_max);
FLOAT_CHARACTER_ATTR(run_speed_max);
FLOAT_CHARACTER_ATTR(ground_acceleration);
FLOAT_CHARACTER_ATTR(ground_deceleration);
FLOAT_CHARACTER_ATTR(jump_acceleration);
FLOAT_CHARACTER_ATTR(jump_cancel_dy);

#define RUBY_CRATTLETYPE_SYM(val, rval)\
    mrb_hash_set(game->mrb, game->ruby.cc_type_to_sym, mrb_fixnum_value(val), mrb_symbol_value(mrb_intern_lit(game->mrb, #rval)));\
    mrb_hash_set(game->mrb, game->ruby.cc_sym_to_type, mrb_symbol_value(mrb_intern_lit(game->mrb, #rval)), mrb_fixnum_value(val));

mrb_value mrb_character_inventory(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    return guy->rinventory;
}

mrb_value mrb_inventory_capacity(mrb_state* mrb, mrb_value self) {
    Inventory* inv = DATA_PTR(self);
    return mrb_fixnum_value(inv->capacity);
}

mrb_value mrb_inventory_count(mrb_state* mrb, mrb_value self) {
    Inventory* inv = DATA_PTR(self);

    int count = 0;
    for (int i = 0; i < inv->capacity; i++) {
        if (inv->items[i].item_type_id != ITEM_NONE)
            count += 1;
    }
    return mrb_fixnum_value(count);
}

mrb_value mrb_inventory_add(mrb_state* mrb, mrb_value self) {
    Inventory* inv = DATA_PTR(self);

    mrb_value item_type;
    mrb_get_args(mrb, "i", &item_type);

    int slot = find_good_inventory_slot(inv);
    if (slot >= 0) {
        set_item(inv, (Game*)mrb->ud, slot, mrb_fixnum(item_type));
    }
    else {
        mrb_raise(
            mrb,
            mrb_class_get(mrb, "RuntimeError"),
            "Inventory out of room!"
        );
    }
    return mrb_nil_value();
}

mrb_value mrb_character_position(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    Character* guy = DATA_PTR(self);

    mrb_value position = mrb_instance_alloc(mrb, game->ruby.vec2_class);
    mrb_data_init(position, guy->position.x, &mrb_dont_free_type);

    mrb_gc_unregister(mrb, position);
    return position;
}

// World functions are defined in scene_world.c

void script_init(struct Game* game) {
    game->ruby.sym_atcontrols        = mrb_intern_lit(game->mrb, "@controls");
    game->ruby.sym_atworld           = mrb_intern_lit(game->mrb, "@world");
    game->ruby.sym_atgame            = mrb_intern_lit(game->mrb, "@game");
    game->ruby.sym_atmap             = mrb_intern_lit(game->mrb, "@map");
    game->ruby.sym_atlocal_character = mrb_intern_lit(game->mrb, "@local_character");
    game->ruby.sym_update            = mrb_intern_lit(game->mrb, "update");
    game->mrb->ud = game;

    // ==== core ext ====
    mrb_define_method(game->mrb, game->mrb->symbol_class, "to_i", mrb_sym_to_i, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->mrb->fixnum_class, "to_sym", mrb_int_to_sym, MRB_ARGS_NONE());

    // ==================================== class Controls =============================
    game->ruby.controls_class = mrb_define_class(game->mrb, "Controls", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.controls_class, MRB_TT_DATA);

    RUBY_CONST_CTRL(C_UP, UP);
    RUBY_CONST_CTRL(C_DOWN, DOWN);
    RUBY_CONST_CTRL(C_LEFT, LEFT);
    RUBY_CONST_CTRL(C_RIGHT, RIGHT);
    RUBY_CONST_CTRL(C_JUMP, JUMP);
    RUBY_CONST_CTRL(C_RUN, RUN);
    RUBY_CONST_CTRL(C_W, W);
    RUBY_CONST_CTRL(C_S, S);
    RUBY_CONST_CTRL(C_A, A);
    RUBY_CONST_CTRL(C_D, D);
    RUBY_CONST_CTRL(C_PAUSE, PAUSE);
    RUBY_CONST_CTRL(C_F1, F1);
    RUBY_CONST_CTRL(C_F2, F2);
    RUBY_CONST_CTRL(C_SPACE, SPACE);
    RUBY_CONST_CTRL(C_DEBUG_ADV, ADV);

    mrb_define_method(game->mrb, game->ruby.controls_class, "initialize", mrb_controls_init, MRB_ARGS_OPT(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "just_pressed", mrb_controls_just_pressed, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "just_released", mrb_controls_just_released, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "[]", mrb_controls_access, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "[]=", mrb_controls_access_eq, MRB_ARGS_REQ(2));

    // ==================================== class Color ================================
    game->ruby.color_class = mrb_define_class(game->mrb, "Color", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.color_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.color_class, "initialize", mrb_color_init, MRB_ARGS_OPT(4));
    mrb_define_method(game->mrb, game->ruby.color_class, "inspect", mrb_color_inspect, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "r", mrb_color_r, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "g", mrb_color_g, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "b", mrb_color_b, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "a", mrb_color_a, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "r=", mrb_color_r_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.color_class, "g=", mrb_color_g_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.color_class, "b=", mrb_color_b_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.color_class, "a=", mrb_color_a_eq, MRB_ARGS_REQ(1));
    mrb_define_alias(game->mrb,  game->ruby.color_class, "red",   "r");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "blue",  "b");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "green", "g");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "alpha", "a");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "red=",   "r=");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "blue=",  "b=");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "green=", "g=");
    mrb_define_alias(game->mrb,  game->ruby.color_class, "alpha=", "a=");

    // ==================================== class Vec2 ================================
    game->ruby.vec2_class = mrb_define_class(game->mrb, "Vec2", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.vec2_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.vec2_class, "initialize", mrb_vec2_init, MRB_ARGS_OPT(2));
    mrb_define_method(game->mrb, game->ruby.vec2_class, "inspect", mrb_vec2_inspect, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.vec2_class, "x", mrb_vec2_x, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.vec2_class, "x=", mrb_vec2_x_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.vec2_class, "y", mrb_vec2_y, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.vec2_class, "y=", mrb_vec2_y_eq, MRB_ARGS_REQ(1));

    // ==================================== class Game ================================
    game->ruby.game_class = mrb_define_class(game->mrb, "Game", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.game_class, MRB_TT_DATA);

#ifdef _DEBUG
    _bench_mrb = game->mrb;
    _bench_hash = mrb_hash_new_capa(_bench_mrb, 16);
    mrb_define_method(game->mrb, game->ruby.game_class, "bench", mrb_debug_game_bench, MRB_ARGS_NONE());
#endif
    mrb_define_method(game->mrb, game->ruby.game_class, "initialize", mrb_game_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.game_class, "controls", mrb_game_controls, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.game_class, "world", mrb_game_world, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.game_class, "save", mrb_game_save, MRB_ARGS_NONE());

    // ==================================== class World ===============================
    game->ruby.world_class = mrb_define_class(game->mrb, "World", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.world_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.world_class, "initialize", mrb_world_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "host", mrb_world_host, MRB_ARGS_OPT(1));
    mrb_define_method(game->mrb, game->ruby.world_class, "join", mrb_world_join, MRB_ARGS_OPT(1));
    mrb_define_method(game->mrb, game->ruby.world_class, "connected?", mrb_world_is_connected, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "joining?", mrb_world_is_joining, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "hosting?", mrb_world_is_hosting, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "current_map", mrb_world_current_map, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "local_character", mrb_world_local_character, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "save", mrb_world_save, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "area", mrb_world_area, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "move_to", mrb_world_area_eq, MRB_ARGS_REQ(2));

    // ==================================== class Map =================================
    game->ruby.map_class = mrb_define_class(game->mrb, "Map", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.map_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.map_class, "initialize", mrb_map_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.map_class, "game", mrb_map_game, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.map_class, "spawn_script_mob", mrb_map_spawn_script_mob, MRB_ARGS_REQ(1));

    // ==================================== class Mob =================================
    game->ruby.mob_class = mrb_define_class(game->mrb, "Mob", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.mob_class, MRB_TT_DATA);

    // mrb_define_method(game->mrb, game->ruby.map_class, "initialize", mrb_mob_init, MRB_ARGS_NONE());

    // ==================================== class Crattlecrute (Character) ===========================
    game->ruby.character_class = mrb_define_class(game->mrb, "Crattlecrute", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.character_class, MRB_TT_DATA);

    game->ruby.cc_type_to_sym = mrb_hash_new_capa(game->mrb, CRATTLECRUTE_TYPE_COUNT);
    game->ruby.cc_sym_to_type = mrb_hash_new_capa(game->mrb, CRATTLECRUTE_TYPE_COUNT);

    RUBY_CRATTLETYPE_SYM(CRATTLECRUTE_YOUNG,    young);
    RUBY_CRATTLETYPE_SYM(CRATTLECRUTE_STANDARD, standard);

    mrb_define_method(game->mrb, game->ruby.character_class, "initialize", mrb_character_init, MRB_ARGS_NONE());

    mrb_define_method(game->mrb, game->ruby.character_class, "inventory", mrb_character_inventory, MRB_ARGS_NONE());

    mrb_define_method(game->mrb, game->ruby.character_class, "position", mrb_character_position, MRB_ARGS_NONE());

    mrb_define_method(game->mrb, game->ruby.character_class, "body_type", mrb_character_body_type, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "feet_type", mrb_character_feet_type, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "body_type=", mrb_character_body_type_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "feet_type=", mrb_character_feet_type_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "body_color", mrb_character_body_color, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "eye_color", mrb_character_eye_color, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "left_foot_color", mrb_character_left_foot_color, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "right_foot_color", mrb_character_right_foot_color, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "body_color=", mrb_character_body_color_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "eye_color=", mrb_character_eye_color_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "left_foot_color=", mrb_character_left_foot_color_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "right_foot_color=", mrb_character_right_foot_color_eq, MRB_ARGS_REQ(1));

    mrb_define_method(game->mrb, game->ruby.character_class, "ground_speed_max", mrb_character_ground_speed_max, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "run_speed_max", mrb_character_run_speed_max, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_acceleration", mrb_character_ground_acceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_deceleration", mrb_character_ground_deceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_acceleration", mrb_character_jump_acceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_cancel_dy", mrb_character_jump_cancel_dy, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_speed_max=", mrb_character_ground_speed_max_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "run_speed_max=", mrb_character_run_speed_max_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_acceleration=", mrb_character_ground_acceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_deceleration=", mrb_character_ground_deceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_acceleration=", mrb_character_jump_acceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_cancel_dy=", mrb_character_jump_cancel_dy_eq, MRB_ARGS_REQ(1));

    mrb_define_method(game->mrb, game->ruby.character_class, "randomize", mrb_character_randomize, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "mark_dirty", mrb_character_mark_dirty, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "dirty!", mrb_character_mark_dirty, MRB_ARGS_NONE());

    mrb_define_method(game->mrb, game->ruby.character_class, "age", mrb_character_age, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "age_of_maturity", mrb_character_age_of_maturity, MRB_ARGS_NONE());

    // ==================================== class Item =================================
    game->ruby.item_class = mrb_define_class(game->mrb, "Item", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.item_class, MRB_TT_DATA);

    // ================================== class Inventory ===============================
    game->ruby.inventory_class = mrb_define_class(game->mrb, "Inventory", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.inventory_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.inventory_class, "capacity", mrb_inventory_capacity, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.inventory_class, "count", mrb_inventory_count, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.inventory_class, "<<", mrb_inventory_add, MRB_ARGS_REQ(1));

    // =============================== STATICALLY GENERATED STUFF ============================
    define_mrb_enum_constants(game);
}

// Pasted in from mirb code lol.
void ruby_p(mrb_state *mrb, mrb_value obj, int prompt) {
  mrb_value val;

  val = mrb_funcall(mrb, obj, "inspect", 0);
  if (prompt) {
    if (!mrb->exc) {
      fputs(" => ", stdout);
    }
    else {
      val = mrb_funcall(mrb, mrb_obj_value(mrb->exc), "inspect", 0);
    }
  }
  if (!mrb_string_p(val)) {
    val = mrb_obj_as_string(mrb, obj);
  }
  fwrite(RSTRING_PTR(val), RSTRING_LEN(val), 1, stdout);
  putc('\n', stdout);
}

#ifdef _DEBUG
// Dear lord do this better...
bool load_script_file(mrb_state* mrb) {
    FILE* file = fopen("script.rb", "r");
    if (file == NULL) {
        file = fopen("build/script.rb", "r");
        if (file == NULL) {
            file = fopen("C:\\Users\\Metre\\game\\crattlecrute\\build\\script.rb", "r");
            if (file == NULL) {
                file = fopen("C:\\Users\\Nigel\\game\\crattle\\build\\script.rb", "r");
                if (file == NULL) {
                    file = fopen("/Users/nigelbaillie/p/crattlecrute/build/script.rb", "r");
                    if (file == NULL)
                        return false;
                }
            }
        }
    }
    mrb_load_file(mrb, file);
    fclose(file);
    return true;
}
#endif
