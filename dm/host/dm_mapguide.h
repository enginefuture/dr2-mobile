#pragma once
#include <stdint.h>
#include "dm_world.h"
// Main game thread; scans incrementally, publishes only world coordinates and copied names.
void dm_mapguide_collect(uint8_t *b, uint64_t player, uint64_t level, int version, dm_map_goal_view *view);
