#include "script.h"
#include "game.h"

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

// World functions are defined in scene_world.c

void script_init(struct Game* game) {
    game->ruby.sym_atcontrols = mrb_intern_lit(game->mrb, "@controls");
    game->ruby.sym_atworld = mrb_intern_lit(game->mrb, "@world");
    game->ruby.sym_update = mrb_intern_lit(game->mrb, "update");

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

    // ==================================== class Map =================================
    game->ruby.map_class = mrb_define_class(game->mrb, "Map", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.map_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.map_class, "initialize", mrb_map_init, MRB_ARGS_NONE());
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
