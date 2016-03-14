#include "script.h"
#include "game.h"
#include "character.h"

mrb_value mrb_color_init(mrb_state* mrb, mrb_value self) {
    mrb_int r = 0, g = 0, b = 0, a = 255, noalloc = false;
    mrb_get_args(mrb, "|iiiii", &r, &g, &b, &a, &noalloc);

    if (noalloc) {
        mrb_data_init(self, NULL, &mrb_dont_free_type);
    }
    else {
        SDL_Color* color = mrb_malloc(mrb, sizeof(SDL_Color));
        color->r = r;
        color->g = g;
        color->b = b;
        color->a = a;
        mrb_data_init(self, color, &mrb_color_type);
    }
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
        Character* guy = DATA_PTR(self); \
        mrb_value value; \
        mrb_get_args(mrb, "o", &value); \
        SDL_Color* color = DATA_PTR(value); \
        guy->name = *color; \
        return guy->r##name; \
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
FLOAT_CHARACTER_ATTR(ground_acceleration);
FLOAT_CHARACTER_ATTR(ground_deceleration);
FLOAT_CHARACTER_ATTR(jump_acceleration);
FLOAT_CHARACTER_ATTR(jump_cancel_dy);

#define RUBY_CRATTLETYPE_SYM(val, rval)\
    mrb_hash_set(game->mrb, game->ruby.cc_type_to_sym, mrb_fixnum_value(val), mrb_symbol_value(mrb_intern_lit(game->mrb, #rval)));\
    mrb_hash_set(game->mrb, game->ruby.cc_sym_to_type, mrb_symbol_value(mrb_intern_lit(game->mrb, #rval)), mrb_fixnum_value(val));

// World functions are defined in scene_world.c

void script_init(struct Game* game) {
    game->ruby.sym_atcontrols        = mrb_intern_lit(game->mrb, "@controls");
    game->ruby.sym_atworld           = mrb_intern_lit(game->mrb, "@world");
    game->ruby.sym_atgame            = mrb_intern_lit(game->mrb, "@game");
    game->ruby.sym_atmap             = mrb_intern_lit(game->mrb, "@map");
    game->ruby.sym_atlocal_character = mrb_intern_lit(game->mrb, "@local_character");
    game->ruby.sym_update            = mrb_intern_lit(game->mrb, "update");
    game->mrb->ud = game;

    // ==================================== class Controls =============================
    game->ruby.controls_class = mrb_define_class(game->mrb, "Controls", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.controls_class, MRB_TT_DATA);

    RUBY_CONST_CTRL(C_UP, UP);
    RUBY_CONST_CTRL(C_DOWN, DOWN);
    RUBY_CONST_CTRL(C_LEFT, LEFT);
    RUBY_CONST_CTRL(C_RIGHT, RIGHT);
    RUBY_CONST_CTRL(C_JUMP, JUMP);
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

    // ==================================== class Color ================================
    game->ruby.color_class = mrb_define_class(game->mrb, "Color", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.color_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.color_class, "initialize", mrb_color_init, MRB_ARGS_OPT(5));
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

    // ==================================== class Game ================================
    game->ruby.game_class = mrb_define_class(game->mrb, "Game", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.game_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.game_class, "initialize", mrb_game_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.game_class, "controls", mrb_game_controls, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.game_class, "world", mrb_game_world, MRB_ARGS_NONE());

    // ==================================== class World ===============================
    game->ruby.world_class = mrb_define_class(game->mrb, "World", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.world_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.world_class, "initialize", mrb_world_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "current_map", mrb_world_current_map, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.world_class, "local_character", mrb_world_local_character, MRB_ARGS_NONE());

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

    // ==================================== class Character ===========================
    game->ruby.character_class = mrb_define_class(game->mrb, "Crattlecrute", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.character_class, MRB_TT_DATA);

    game->ruby.cc_type_to_sym = mrb_hash_new_capa(game->mrb, CRATTLECRUTE_TYPE_COUNT);
    game->ruby.cc_sym_to_type = mrb_hash_new_capa(game->mrb, CRATTLECRUTE_TYPE_COUNT);

    RUBY_CRATTLETYPE_SYM(CRATTLECRUTE_YOUNG,    young);
    RUBY_CRATTLETYPE_SYM(CRATTLECRUTE_STANDARD, standard);

    mrb_define_method(game->mrb, game->ruby.character_class, "initialize", mrb_character_init, MRB_ARGS_NONE());
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
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_acceleration", mrb_character_ground_acceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_deceleration", mrb_character_ground_deceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_acceleration", mrb_character_jump_acceleration, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_cancel_dy", mrb_character_jump_cancel_dy, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_speed_max=", mrb_character_ground_speed_max_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_acceleration=", mrb_character_ground_acceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "ground_deceleration=", mrb_character_ground_deceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_acceleration=", mrb_character_jump_acceleration_eq, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.character_class, "jump_cancel_dy=", mrb_character_jump_cancel_dy_eq, MRB_ARGS_REQ(1));
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
bool load_script_file(mrb_state* mrb) {
    FILE* file = fopen("script.rb", "r");
    if (file == NULL) {
        file = fopen("C:\\Users\\Metre\\game\\crattlecrute\\build\\script.rb", "r");
        if (file == NULL) {
            file = fopen("C:\\Users\\Nigel\\game\\crattle\\build\\script.rb", "r");
            if (file == NULL)
                return false;
        }
    }
    mrb_load_file(mrb, file);
    fclose(file);
    return true;
}
#endif
