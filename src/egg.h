#ifndef EGG_H
#define EGG_H

#include "item.h"
#include "mob.h"

// TODO actually use these flags -- or get rid of them I was just brainstorming
#define GSPEC_HUE (0 << 0)
#define GSPEC_ADJUST_ATTR (0 << 1)
#define GSPEC_DARKER_COLOR (0 << 2)

#define GFLAG_GSPD (0 << 0)
#define GFLAG_RNPD (0 << 1)
#define GFLAG_GACC (0 << 2)
#define GFLAG_GDEC (0 << 3)
#define GFLAG_JACC (0 << 4)
#define GFLAG_JCDY (0 << 5)

#define GFLAG_BODY (0 << 6)
#define GFLAG_FEET (0 << 0)
#define GFLAG_ASYM (0 << 5)

#define GFLAG_INVERT (0 << 7)
#define GFLAG_SHIFT (0 << 8)
#define GFLAG_INTENSE (0 << 9)

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
} MobEgg;

typedef struct ItemEgg {
    LAYERED_ICON_ITEM;
    struct EggData e;
} ItemEgg;

void default_egg(struct EggData* egg);

struct Game;
struct Character;
// This is done separate because of the half-in-egg situation...
void genes_decide_body_color(Genes* genes, SDL_Color* color);
void initialize_genes(struct Game* game, struct Character* guy, SDL_Color* body_color);
// I suppose this'll also handle growth
void update_genes(struct Game* game, struct Character* guy);
void mature_genes(struct Game* game, struct Character* guy);

#endif