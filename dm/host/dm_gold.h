// 暂停菜单「获取金币」：只补满当前角色身上的金币，服务端拍执行，正常存盘。
// Pause menu Get Gold: refill the current character's carried gold on the server tick and save normally.
#pragma once
#include <stdint.h>
void dm_gold_on_uimsg(const char *category, const char *action, const char *argument);
void dm_gold_srv_tick(uint8_t *base, uint64_t game, uint64_t player);
