#pragma once
#include <stdint.h>
#include "dm_world.h"
void dm_mapselect_prepare(dm_map_goal_view *v, int level);
void dm_mapselect_next(void);
uint64_t dm_mapselect_filter(uint64_t buttons);
