#include "egg.h"
#include "game.h"
#include "character.h"

void default_egg(struct EggData* egg) {
    egg->age = 0;
    egg->hatching_age = 15 MINUTES;
    egg->genes.word = 0;
}

static void mod_color(Genes* genes, SDL_Color* color) {
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

    mod_color(genes, color);
}

void initialize_genes(struct Game* game, struct Character* guy, SDL_Color* body_color) {
    SDL_Color backup_body_color;
    if (body_color == NULL) {
        body_color = &backup_body_color;
        genes_decide_body_color(&guy->genes, body_color);
    }
    // Assuming this is a newborn, so it gets all stats set to 0
    SDL_memset(&guy->stats, 0, sizeof(guy->stats));

    // COLOR
    guy->body_color = *body_color;

    guy->left_foot_color.r = 255;
    guy->left_foot_color.g = 0;
    guy->left_foot_color.b = 0;
    guy->left_foot_color.a = 255;
    guy->right_foot_color = guy->left_foot_color;
    mod_color(&guy->genes, &guy->right_foot_color);
    mod_color(&guy->genes, &guy->left_foot_color);

    // eye color will just be random for now (mob_egg randomizes it before calling this)

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
