#ifndef EGG_H
#define EGG_H

#include "item.h"
#include "mob.h"

// TODO actually use these flags -- or get rid of them I was just brainstorming
#define GSPEC_HUE (1 << 0)
#define GSPEC_ADJUST_ATTR (1 << 1)
#define GSPEC_DARKER_COLOR (1 << 2)

#define GFLAG_GSPD (1 << 0)
#define GFLAG_RNPD (1 << 1)
#define GFLAG_GACC (1 << 2)
#define GFLAG_GDEC (1 << 3)
#define GFLAG_JACC (1 << 4)
#define GFLAG_JCDY (1 << 5)

#define GFLAG_BODY (1 << 6)
#define GFLAG_FEET (1 << 0)
#define GFLAG_ASYM (1 << 5)

#define GFLAG_INVERT (1 << 7)
#define GFLAG_SHIFT (1 << 8)
#define GFLAG_INTENSE (1 << 9)

typedef union Genes {
    struct {
        Uint16 specifiers;
        Uint16 flags;
    };
    Uint32 word;
} Genes;

struct EggData {
    int age;
    int hatching_age;
    Genes genes;
};

typedef struct MobEgg {
    PHYSICS_MOB_FIELDS;
    float dy;
    struct EggData e;
    SDL_Color decided_color;
    SDL_Color decided_eye_color;
    int decided_eye_type;
} MobEgg;

typedef struct ItemEgg {
    LAYERED_ICON_ITEM;
    struct EggData e;
} ItemEgg;

void default_egg(struct EggData* egg);

struct Game;
struct Character;
void genes_decide_body_color(Genes* genes, SDL_Color* color);
void genes_decide_eye_color(Genes* genes, SDL_Color* color);
void genes_decide_eye_type(Genes* genes, int* eye_type);
#define initialize_genes(game, guy) initialize_genes_with_colors(game, guy, 0, 0, -1)
void initialize_genes_with_colors(struct Game* game, struct Character* guy, SDL_Color* body_color, SDL_Color* eye_color, int eye_type);
// I suppose this'll also handle growth
void update_genes(struct Game* game, struct Character* guy);
void mature_genes(struct Game* game, struct Character* guy);

#endif