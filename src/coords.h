#ifndef COORDS_H
#define COORDS_H
#include "types.h"

#define S1X 0
#define S1Y 1
#define S2X 2
#define S2Y 3
#define SENSOR_1 0
#define SENSOR_2 2
#define X 0
#define Y 1
#define BOTTOM_SENSOR 0
#define TOP_SENSOR 1
#define RIGHT_SENSOR 2
#define LEFT_SENSOR 3

#define TOP2DOWN 0
#define BOTTOM2UP 1
#define RIGHT2LEFT 2
#define LEFT2RIGHT 3

typedef struct GenericBody {
    // (x[0] left to right, x[1] down to up)
    vec4 position;
    // (x[0] left to right, x[1] down to up)
    vec4 old_position;

    // (x[0], x[1])  (x[2], x[3])
    vec4i top_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i bottom_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i left_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i right_sensors;

    bool left_hit, right_hit, grounded, hit_ceiling, hit_wall;
    // In degrees
    float ground_angle;
} GenericBody;
#endif
