#include "egg.h"
#include "game.h"
#include "character.h"

void default_egg(struct EggData* egg) {
    egg->age = 0;
    egg->hatching_age = 15 MINUTES;
    egg->genes.word = 0;
}

static void mod_color(Genes* genes, SDL_Color* color, Uint16 flags) {
    if (genes->specifiers & GSPEC_DARKER_COLOR) {
        float factor;

        if (genes->flags & GFLAG_INVERT)
            factor = 1.4f;
        else
            factor = 0.7f;
        if (genes->flags & GFLAG_INTENSE)
            if (genes->flags & GFLAG_INVERT)
                factor *= 1.5f;
            else
                factor /= 2.0f;

        color->r = (int)((float)color->r * factor);
        color->g = (int)((float)color->g * factor);
        color->b = (int)((float)color->b * factor);
    }
}

SDL_Color genes_solid_color(Genes* genes) {
    return hue_to_color((float)(genes->flags % 360));
}

void genes_decide_body_color(Genes* genes, SDL_Color* color) {
    if (genes->specifiers & GSPEC_SOLID_COLOR) {
        *color = genes_solid_color(genes);
    }
    else {
        color->a = 255;
        color->r = 0;
        color->g = 210;
        color->b = 255;
    }

    mod_color(genes, color, genes->flags);
}

void genes_decide_eye_color(Genes* genes, SDL_Color* color) {
    color->a = 255;
    color->r = 255;
    color->g = 255;
    color->b = 255;

    mod_color(genes, color, genes->flags ^ GFLAG_INVERT);
}

void genes_decide_eye_type(Genes* genes, int* eye_type) {
    // There's only one of these lol
    *eye_type = 0;
}

void initialize_genes_with_colors(struct Game* game, struct Character* guy, SDL_Color* body_color, SDL_Color* eye_color, int eye_type) {
    SDL_Color backup_body_color;
    SDL_Color backup_eye_color;
    if (body_color == NULL) {
        body_color = &backup_body_color;
        genes_decide_body_color(&guy->genes, body_color);
    }
    if (eye_color == NULL) {
        eye_color = &backup_eye_color;
        genes_decide_eye_color(&guy->genes, eye_color);
    }
    if (eye_type < 0) {
        genes_decide_eye_type(&guy->genes, &eye_type);
    }

    // Assuming this is a newborn, so no stats
    SDL_memset(&guy->stats, 0, sizeof(guy->stats));

    // COLOR
    guy->body_color = *body_color;
    guy->eye_color  = *eye_color;
    guy->eye_type = eye_type;

    if (guy->genes.specifiers & GSPEC_SOLID_COLOR) {
        guy->left_foot_color = genes_solid_color(&guy->genes);
        guy->right_foot_color = guy->left_foot_color;
    }
    else {
        guy->left_foot_color.r = 255;
        guy->left_foot_color.g = 0;
        guy->left_foot_color.b = 0;
        guy->left_foot_color.a = 255;
        guy->right_foot_color = guy->left_foot_color;
    }
    mod_color(&guy->genes, &guy->right_foot_color, guy->genes.flags);
    mod_color(&guy->genes, &guy->left_foot_color, guy->genes.flags);

    // ATTRIBUTES
    // const float base_stat = 0.7f;
    const float base_stat = 0.9f;
    guy->ground_speed_max    = base_stat;
    guy->run_speed_max       = base_stat;
    guy->ground_acceleration = base_stat;
    guy->ground_deceleration = base_stat;
    guy->jump_acceleration   = base_stat;
    guy->jump_cancel_dy      = base_stat;
    // TEMP
    guy->age = guy->age_of_maturity - 1;
}

void update_genes(struct Game* game, struct Character* guy) {
    if (guy->age < guy->age_of_maturity) {
#define INC_ATTR(attr) guy->attr += 0.3f * (1.0f / (float)guy->age_of_maturity);
        INC_ATTR(ground_speed_max);
        INC_ATTR(run_speed_max);
        INC_ATTR(ground_acceleration);
        INC_ATTR(ground_deceleration);
        INC_ATTR(jump_acceleration);
        INC_ATTR(jump_cancel_dy);
    }
}

void mature_genes(struct Game* game, struct Character* guy) {
    // TODO ..
}
