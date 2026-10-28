// 药囊：服务端保存原版药水位流，腰带只保留使用中的实物。
#pragma once
#include <stdint.h>
void dm_potbag_tick(uint8_t *base, uint64_t game, uint64_t player);
void dm_potbag_step(uint8_t *base);
void dm_potbag_on_uimsg(const char *cat, const char *act, const char *arg);
int dm_potbag_pickup(uint8_t *base, uint64_t game, uint64_t player, uint32_t id);
int dm_potbag_ready(uint32_t id);
int dm_potbag_packet_blocked(const uint8_t *packet, unsigned length);
int dm_potbag_kind(uint32_t code); // hp1..hp5=0..4, mp1..mp5=5..9, rvs/rvl=10/11
void dm_potbag_counts(int counts[12]);
