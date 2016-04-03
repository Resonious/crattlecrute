#include "script.h"
#include "game.h"
#include "character.h"
#include "egg.h"

struct MrbItem {
    Inventory* inventory;
    int slot;
};

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

mrb_value mrb_color_hue(mrb_state* mrb, mrb_value self) {
    SDL_Color* color = DATA_PTR(self);
    return mrb_float_value(mrb, color_to_hue(*color));
}

mrb_value mrb_color_set_hue(mrb_state* mrb, mrb_value self) {
    SDL_Color* color = DATA_PTR(self);

    mrb_value hue;
    mrb_get_args(mrb, "o", &hue);

    SDL_Color new_rgb = hue_to_color(mrb_to_f(mrb, hue));
    color->r = new_rgb.r;
    color->g = new_rgb.g;
    color->b = new_rgb.b;

    return mrb_float_value(mrb, color_to_hue(*color));
}

mrb_value mrb_num_as_hue(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    SDL_Color rgb = hue_to_color(wrap_degrees(mrb_to_f(mrb, self)));

    mrb_value args[3];
    args[0] = mrb_fixnum_value(rgb.r);
    args[1] = mrb_fixnum_value(rgb.g);
    args[2] = mrb_fixnum_value(rgb.b);

    return mrb_obj_new(mrb, game->ruby.color_class, 3, args);
}

mrb_value mrb_controls_init(mrb_state* mrb, mrb_value self) {
    Controls* new_controls = mrb_malloc(mrb, sizeof(Controls));

    memset(new_controls, 0, sizeof(Controls));
    mrb_data_init(self, new_controls, &mrb_controls_type);

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
        rcontrols = mrb_instance_alloc(mrb, game->ruby.controls_class);
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

mrb_value mrb_game_ai_controls(mrb_state* mrb, mrb_value self) {
    return ((Game*)DATA_PTR(self))->ruby.controls;
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

#define EGG_ATTR(name) \
    mrb_value mrb_egg_##name(mrb_state* mrb, mrb_value self) { \
        struct EggData* egg = DATA_PTR(self); \
        int value = *((int*)&egg->name); \
        return mrb_fixnum_value(value); \
    } \
    mrb_value mrb_egg_##name##_eq(mrb_state* mrb, mrb_value self) { \
        struct EggData* egg = DATA_PTR(self); \
        mrb_int value; \
        mrb_get_args(mrb, "i", &value); \
        int* field = (int*)&egg->name; \
        *field = value; \
        return mrb_fixnum_value(*field); \
    }

EGG_ATTR(age);
EGG_ATTR(hatching_age);
EGG_ATTR(genes);

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

mrb_value mrb_character_name(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    return mrb_str_new_cstr(mrb, guy->name);
}

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

// Defines a mrb_value called varname of a Item instance on the stack.
// (Currently assumes inv is an Inventory*)
#define MRB_TEMP_SLOT(varname, index)\
    mrb_value varname = mrb_instance_alloc(mrb, game->ruby.item_class);\
    struct MrbItem _item;\
    _item.inventory = inv;\
    _item.slot = index;\
    mrb_data_init(varname, &_item, &mrb_dont_free_type);\
    mrb_gc_unregister(mrb, varname);

mrb_value mrb_inventory_add(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    Inventory* inv = DATA_PTR(self);

    mrb_value item_type;
    mrb_get_args(mrb, "o", &item_type);

    int slot = find_good_inventory_slot(inv);
    if (slot >= 0) {
        if (mrb_fixnum_p(item_type))
            set_item(inv, (Game*)mrb->ud, slot, mrb_fixnum(item_type));
        else if (mrb_respond_to(mrb, item_type, game->ruby.sym_into_item)) {
            MRB_TEMP_SLOT(item, slot);
            mrb_funcall_argv(mrb, item_type, game->ruby.sym_into_item, 1, &item);
        }
        else {
            mrb_raise(
                mrb,
                mrb_class_get(mrb, "StandardError"),
                "Attempted to add unknown item to inventory"
            );
        }
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

mrb_value mrb_inventory_access(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    Inventory* inv = DATA_PTR(self);

    mrb_int index;
    mrb_get_args(mrb, "i", &index);

    mrb_value args[2];
    args[0] = self;
    args[1] = mrb_fixnum_value(index);
    return mrb_obj_new(mrb, game->ruby.item_class, 2, args);
}

mrb_value mrb_inventory_access_eq(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    Inventory* inv = DATA_PTR(self);

    mrb_int index;
    mrb_value obj;
    mrb_get_args(mrb, "io", &index, &obj);

    if (mrb_respond_to(mrb, obj, game->ruby.sym_into_item)) {
        MRB_TEMP_SLOT(slot, index);
        mrb_funcall_argv(mrb, obj, game->ruby.sym_into_item, 1, &slot);
    }
    else {
        mrb_raise(
            mrb,
            mrb_class_get(mrb, "ArgumentError"),
            "Given argument doesn't respond to `into_item`"
        );
    }
    return self;
}

mrb_value mrb_character_print_stats(mrb_state* mrb, mrb_value self) {
    Character* guy = DATA_PTR(self);
    printf("==== %s's stats: ====\n", guy->name);
    printf("Frames walked       :: %i\n", guy->stats.frames_walked);
    printf("Frames ran          :: %i\n", guy->stats.frames_ran);
    printf("Times jumped        :: %i\n", guy->stats.times_jumped);
    printf("Times jump-canceled :: %i\n", guy->stats.times_jump_canceled);
    printf("Frames on ground    :: %i\n", guy->stats.frames_on_ground);
    printf("Frames in air       :: %i\n", guy->stats.frames_in_air);
    printf("=====================\n");
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

mrb_value mrb_egg_init(mrb_state* mrb, mrb_value self) {
    struct EggData* egg = DATA_PTR(self);
    if (egg == NULL) {
        egg = mrb_malloc(mrb, sizeof(struct EggData));
        mrb_data_init(self, egg, &mrb_free_type);
    }

    egg->age = 0;
    egg->hatching_age = 15 MINUTES;
    default_egg(egg);
    return self;
}

mrb_value mrb_egg_into_item(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    struct EggData* egg = DATA_PTR(self);

    mrb_value ritem;
    mrb_get_args(mrb, "o", &ritem);

    if (mrb_obj_class(mrb, ritem) != game->ruby.item_class) {
        mrb_raisef(mrb, mrb_class_get(mrb, "ArgumentError"), "Expected Item, got %S", mrb_str_new_cstr(mrb, mrb_obj_classname(mrb, ritem)));
        return mrb_nil_value();
    }
    struct MrbItem* item = DATA_PTR(ritem);

    ItemEgg* egg_item = (ItemEgg*)set_item(item->inventory, game, item->slot, ITEM_EGG);
    if (egg_item == NULL) {
        mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), "Unable to set item");
        return mrb_nil_value();
    }

    egg_item->e = *egg;

    return mrb_true_value();
}

mrb_value mrb_item_init(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    struct MrbItem* item = DATA_PTR(self);
    if (item == NULL) {
        item = mrb_malloc(mrb, sizeof(struct MrbItem));
        mrb_data_init(self, item, &mrb_free_type);
    }

    mrb_value inv;
    mrb_int slot;
    mrb_get_args(mrb, "oi", &inv, &slot);

    if (mrb_obj_class(mrb, inv) != game->ruby.inventory_class) {
        mrb_raisef(mrb, mrb_class_get(mrb, "ArgumentError"), "Expected Inventory, got %S", mrb_str_new_cstr(mrb, mrb_obj_classname(mrb, inv)));
        return mrb_nil_value();
    }

    item->inventory = (Inventory*)DATA_PTR(inv);
    item->slot = slot;

    if (item->slot > item->inventory->capacity) {
        mrb_raisef(
            mrb,
            E_ARGUMENT_ERROR,
            "Item slot %S is greater than inventory capacity %S",
            mrb_fixnum_value(item->slot),
            mrb_fixnum_value(item->inventory->capacity)
        );
        return mrb_nil_value();
    }
    else if (item->slot < 0) {
        mrb_raise(mrb, mrb_class_get(mrb, "ArgumentError"), "Item slot is negative");
        return mrb_nil_value();
    }

    return self;
}

mrb_value mrb_item_type(mrb_state* mrb, mrb_value self) {
    struct MrbItem* item = DATA_PTR(self);
    return mrb_fixnum_value(item->inventory->items[item->slot].item_type_id);
}

mrb_value mrb_item_to_egg(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    struct MrbItem* item = DATA_PTR(self);

    int item_type = item->inventory->items[item->slot].item_type_id;
    if (item_type == ITEM_EGG) {
        mrb_value egg = mrb_obj_new(mrb, game->ruby.egg_class, 0, NULL);
        struct EggData* egg_data = DATA_PTR(egg);
        struct ItemEgg* inventory_egg = (ItemEgg*)&item->inventory->items[item->slot];

        *egg_data = inventory_egg->e;

        return egg;
    }
    else {
        mrb_raise(mrb, mrb_class_get(mrb, "StandardError"), "Item is not an egg");

        return mrb_nil_value();
    }
}

// World functions are defined in scene_world.c

void script_init(struct Game* game) {
    game->ruby.sym_atcontrols        = mrb_intern_lit(game->mrb, "@controls");
    game->ruby.sym_atworld           = mrb_intern_lit(game->mrb, "@world");
    game->ruby.sym_atgame            = mrb_intern_lit(game->mrb, "@game");
    game->ruby.sym_atmap             = mrb_intern_lit(game->mrb, "@map");
    game->ruby.sym_atlocal_character = mrb_intern_lit(game->mrb, "@local_character");
    game->ruby.sym_update            = mrb_intern_lit(game->mrb, "update");
    game->ruby.sym_into_item         = mrb_intern_lit(game->mrb, "into_item");
    game->mrb->ud = game;

    game->ruby.controls = mrb_ary_new_capa(game->mrb, 8);

    // ==== core ext ====
    mrb_define_method(game->mrb, game->mrb->symbol_class, "to_i", mrb_sym_to_i, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->mrb->fixnum_class, "to_sym", mrb_int_to_sym, MRB_ARGS_NONE());

    mrb_define_method(game->mrb, game->mrb->fixnum_class, "as_hue", mrb_num_as_hue, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->mrb->float_class,  "as_hue", mrb_num_as_hue, MRB_ARGS_NONE());

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

    mrb_define_method(game->mrb, game->ruby.controls_class, "initialize", mrb_controls_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.controls_class, "just_pressed", mrb_controls_just_pressed, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "just_released", mrb_controls_just_released, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "[]", mrb_controls_access, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.controls_class, "[]=", mrb_controls_access_eq, MRB_ARGS_REQ(2));

    // ==================================== class Color ================================
    game->ruby.color_class = mrb_define_class(game->mrb, "Color", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.color_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.color_class, "initialize", mrb_color_init, MRB_ARGS_OPT(4));
    mrb_define_method(game->mrb, game->ruby.color_class, "inspect", mrb_color_inspect, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "hue", mrb_color_hue, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.color_class, "set_hue", mrb_color_set_hue, MRB_ARGS_REQ(1));
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
    mrb_define_method(game->mrb, game->ruby.game_class, "ai_controls", mrb_game_ai_controls, MRB_ARGS_NONE());

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

    mrb_define_method(game->mrb, game->ruby.character_class, "name", mrb_character_name, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "position", mrb_character_position, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.character_class, "print_stats", mrb_character_print_stats, MRB_ARGS_NONE());

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

    mrb_define_method(game->mrb, game->ruby.item_class, "initialize", mrb_item_init, MRB_ARGS_REQ(2));
    mrb_define_method(game->mrb, game->ruby.item_class, "type", mrb_item_type, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.item_class, "to_egg", mrb_item_to_egg, MRB_ARGS_NONE());

    // ================================== class Inventory ===============================
    game->ruby.inventory_class = mrb_define_class(game->mrb, "Inventory", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.inventory_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.inventory_class, "capacity", mrb_inventory_capacity, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.inventory_class, "count", mrb_inventory_count, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.inventory_class, "<<", mrb_inventory_add, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.inventory_class, "[]", mrb_inventory_access, MRB_ARGS_REQ(1));
    mrb_define_method(game->mrb, game->ruby.inventory_class, "[]=", mrb_inventory_access_eq, MRB_ARGS_REQ(2));

    // ================================== class Egg ====================================
    game->ruby.egg_class = mrb_define_class(game->mrb, "Egg", game->mrb->object_class);
    MRB_SET_INSTANCE_TT(game->ruby.egg_class, MRB_TT_DATA);

    mrb_define_method(game->mrb, game->ruby.egg_class, "initialize", mrb_egg_init, MRB_ARGS_NONE());
    mrb_define_method(game->mrb, game->ruby.egg_class, "into_item", mrb_egg_into_item, MRB_ARGS_REQ(2));

#define DECL_EGG_FIELD(field)\
    mrb_define_method(game->mrb, game->ruby.egg_class, #field, mrb_egg_##field, MRB_ARGS_NONE());\
    mrb_define_method(game->mrb, game->ruby.egg_class, #field"=", mrb_egg_##field##_eq, MRB_ARGS_REQ(1));

    DECL_EGG_FIELD(age);
    DECL_EGG_FIELD(hatching_age);
    DECL_EGG_FIELD(genes);

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
