#include "tilemap.h"

TileIndex tile_at(Tilemap* tilemap, vec4i* tilespace, const int sensor) {
    TileIndex result;

    int raw_tile_index = tilemap->tiles[tilespace->x[sensor + 1] * tilemap->width + tilespace->x[sensor]];
    if (raw_tile_index == -1) {
        result.flags = NOT_A_TILE;
        result.index = -1;
        return result;
    }

    // ENDIAN dependent?
    result.index = raw_tile_index & 0x00FFFFFF;
    result.flags = (raw_tile_index & 0xFF000000) >> 24;
    return result;
}

int* tile_height_for_sensor(TileHeights* all_heights, TileIndex* tile_index, const int sensor_dir) {
  // ((int*)(&heights[tile_index.index]) + (sensor_dir * 32))
    // Assumes LEFT and RIGHT sensors are higher numbers than TOP_SENSOR
    int offset_to_use = sensor_dir;
    if (sensor_dir > TOP_SENSOR) {
        if (tile_index->flags & TILE_FLIP_X) {
            // At this point, it's either left or right
            int offset_to_use = sensor_dir == LEFT_SENSOR ? RIGHT_SENSOR : LEFT_SENSOR;
        }
    }
    return (int*)(&all_heights[tile_index->index]) + (sensor_dir * 32);
}