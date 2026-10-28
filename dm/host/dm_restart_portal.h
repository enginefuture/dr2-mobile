// 一键重开保留返场位置。所有入口只在游戏主线程（含服务端拍）调用，不跨局保留任何游戏指针。
// Quick Restart return portal. Game main thread only; bookmarks contain values, never pointers into a previous game.
#pragma once
#include <stdint.h>
int dm_restart_portal_prepare(const char *character, int difficulty);
void dm_restart_portal_cancel(void);
void dm_restart_portal_tick(uint8_t *base, uint64_t game, uint64_t player);
