#define DM_GOLD_TEST 1
#include "../dm_gold.c"
#include <assert.h>

static int level = 55, gold = 123, writes, saves, pulses, notices;
int dm_gold_test_stat(uint64_t player, int stat) { (void)player; assert(stat == 12 || stat == 14); return stat == 12 ? level : gold; }
void dm_gold_test_set(uint64_t player, int stat, int value) { (void)player; assert(stat == 14); gold = value; writes++; }
void dm_ui_key_pulse(uint64_t mask, int frames) { assert(mask == DM_BTN_B && frames == 6); pulses++; }
const char *dm_str(uint8_t *base, const char *key) { (void)base; return !strcmp(key, "dmGoldAdded") ? "Gold refilled (+%d)" : "Carried gold is full"; }
void dm_game_notice(uint8_t *base, const char *text) { (void)base; assert(text && *text); notices++; }
int dm_request_save(void) { saves++; return 1; }
static void click(void) { dm_gold_on_uimsg("PanelManager", "ClosePanel", "TouchGetGold"); }
int main(void) {
    uint8_t base[8] = {0}; uint32_t unit[0x200 / 4] = {0}; unit[2] = 42; unit[3] = 1;
    uint64_t player = (uint64_t)unit;
    click(); assert(!pulses); // outside a run
    dm_gold_srv_tick(base, 1, player);
    dm_gold_on_uimsg(NULL, NULL, NULL); dm_gold_on_uimsg("PanelManager", "OpenPanel", "TouchGetGold");
    assert(!pulses && !writes);
    click(); click(); assert(pulses == 1 && gold == 123 && !saves); // queued once; no UI-thread mutation
    dm_gold_srv_tick(base, 1, player); assert(gold == 550000 && writes == 1 && saves == 1 && notices == 1);
    dm_gold_srv_tick(base, 1, player); assert(writes == 1 && saves == 1); // no automatic refill on later ticks
    click(); dm_gold_srv_tick(base, 1, player); assert(writes == 1 && notices == 2); // full
    gold = 600000; click(); dm_gold_srv_tick(base, 1, player); assert(gold == 600000 && writes == 1); // never removes old excess
    level = 1; gold = 0; click(); dm_gold_srv_tick(base, 1, player); assert(gold == 10000);
    level = 99; gold = 1; click(); dm_gold_srv_tick(base, 1, player); assert(gold == 990000);
    int before = writes;
    for (int mode = 0; mode <= 17; mode += 17) { gold = 0; unit[3] = mode; click(); dm_gold_srv_tick(base, 1, player); assert(gold == 0 && writes == before); }
    unit[3] = 1;
    for (int bad = 0; bad <= 100; bad += 100) { level = bad; click(); dm_gold_srv_tick(base, 1, player); assert(writes == before); }
    level = 55;
    click(); unit[2]++; dm_gold_srv_tick(base, 1, player); assert(!gold && writes == before); // new character, same pointer
    click(); dm_gold_srv_tick(base, 2, player); assert(!gold && writes == before); // different run
    click(); dm_gold_srv_tick(base, 0, 0); dm_gold_srv_tick(base, 3, player); assert(!gold && writes == before); // leaving a run
    click(); dm_gold_srv_tick(base, 3, player); assert(gold == 550000 && writes == before + 1 && saves == writes);
    puts("gold: OK (menu request, server-only refill, cap, save, full/excess, repeat clicks, death, invalid stats, run/character ownership)");
    return 0;
}
