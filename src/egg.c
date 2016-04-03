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
        __m128i vicolor = _mm_loadu_si128(&color);
        __m128 vcolor = _mm_cvtepi32_ps(vicolor);
        float factor;

        if (genes->flags & GFLAG_INVERT)
            factor = 1.4f;
        else
            factor = 0.6f;
        if (genes->flags & GFLAG_INTENSE)
            if (genes->flags & GFLAG_INVERT)
                factor *= 1.5f;
            else
                factor /= 2.0f;

        vec4 vfactor;
        vfactor.simd = _mm_load1_ps(&factor);
        vfactor.x[3] = 0.0f;
        vcolor = _mm_mul_ps(vcolor, vfactor.simd);
        _mm_storeu_si128(color, _mm_cvtps_epi32(vcolor));
    }
}

void genes_decide_body_color(Genes* genes, SDL_Color* color) {
    // TODO actual variation
    color->a = 255;
    color->r = 0;
    color->g = 210;
    color->b = 255;

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

    guy->left_foot_color.r = 255;
    guy->left_foot_color.g = 0;
    guy->left_foot_color.b = 0;
    guy->left_foot_color.a = 255;
    guy->right_foot_color = guy->left_foot_color;
    mod_color(&guy->genes, &guy->right_foot_color, guy->genes.flags);
    mod_color(&guy->genes, &guy->left_foot_color,  guy->genes.flags);

    // ATTRIBUTES
    const float base_stat = 0.7f;
    guy->ground_speed_max    = base_stat;
    guy->run_speed_max       = base_stat;
    guy->ground_acceleration = base_stat;
    guy->ground_deceleration = base_stat;
    guy->jump_acceleration   = base_stat;
    guy->jump_cancel_dy      = base_stat;
}

void update_genes(struct Game* game, struct Character* guy) {
    // TODO ..
}
