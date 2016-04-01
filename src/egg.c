#include "egg.h"

void default_egg(struct EggData* egg) {
    egg->body_color.r = 0;
    egg->body_color.g = 210;
    egg->body_color.b = 255;
    egg->body_color.a = 255;

    egg->left_foot_color.r = 255;
    egg->left_foot_color.g = 0;
    egg->left_foot_color.b = 0;
    egg->left_foot_color.a = 255;
    egg->right_foot_color = egg->left_foot_color;

    egg->eye_color.a = 255;
    egg->eye_color.r = 255;
    egg->eye_color.g = 255;
    egg->eye_color.b = 255;
}
