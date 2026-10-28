#include "dm_gold.h"
#include "dm_cursor.h"
#include "dm_input.h"
#include "dm_i18n.h"
#include "hle.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

// UI 只投递请求；只在同一局、同一角色的服务端拍修改属性，退出或换角色即丢弃。
// The UI queues a request; only the owning run/character's server tick may change stats.
static _Atomic uint64_t g_ready, g_request;
static uint64_t g_game, g_player, g_serial;
static uint32_t g_uid;

#ifdef DM_GOLD_TEST
int dm_gold_test_stat(uint64_t player, int stat);
void dm_gold_test_set(uint64_t player, int stat, int value);
#define GOLD_STAT(b, p, s) dm_gold_test_stat(p, s)
#define GOLD_SET(b, p, s, v) dm_gold_test_set(p, s, v)
#else
#define GOLD_STAT(b, p, s) ((int (*)(uint64_t, int, int))((b) + 0x5b9290))((p), (s), 0)
#define GOLD_SET(b, p, s, v) ((void (*)(uint64_t, int, int, int))((b) + 0x5bc1f0))((p), (s), (v), 0)
#endif

void dm_gold_on_uimsg(const char *cat, const char *act, const char *arg) {
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel") || strcmp(arg, "TouchGetGold")) return;
    uint64_t session = atomic_load(&g_ready); if (!session) return;
    if (atomic_exchange(&g_request, session) != session) {
        dm_ui_key_pulse(DM_BTN_B, 6);   // 返回游戏，暂停结束后原版才继续服务端拍。
        LOG("获取金币：菜单请求已接收，返回游戏后补满");
    }
}
void dm_gold_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    if (!b || !G || !player || *(uint32_t *)(player + 0) != 0) {
        atomic_store(&g_ready, 0); atomic_store(&g_request, 0); g_game = g_player = 0; return;
    }
    uint32_t uid = *(uint32_t *)(player + 8);
    if (g_game != G || g_player != player || g_uid != uid) {
        atomic_store(&g_ready, 0); atomic_store(&g_request, 0);
        g_game = G; g_player = player; g_uid = uid;
        if (!++g_serial) ++g_serial;
        atomic_store(&g_ready, g_serial);
    }
    uint64_t request = atomic_exchange(&g_request, 0);
    if (!request || request != atomic_load(&g_ready)) return;
    uint32_t mode = *(uint32_t *)(player + 0xc);
    if (mode == 0 || mode == 17) { LOG("获取金币：角色已死亡，忽略请求"); return; }
    int level = GOLD_STAT(b, player, 12), current = GOLD_STAT(b, player, 14);
    if (level < 1 || level > 99 || current < 0) { LOG("【红】获取金币：角色属性无效（等级 %d、金币 %d），不修改", level, current); return; }
    int cap = level * 10000;   // 原版携带上限；不会扣掉已超过上限的旧金币，也不改仓库属性 15。
    void dm_game_notice(uint8_t *, const char *);
    if (current >= cap) { dm_game_notice(b, dm_str(b, "dmGoldFull")); LOG("获取金币：已满（%d / %d），不重复增加", current, cap); return; }
    GOLD_SET(b, player, 14, cap);
    int after = GOLD_STAT(b, player, 14);
    if (after != cap) { LOG("【红】获取金币：原版设属性后读回 %d，预期 %d", after, cap); return; }
    char text[192]; snprintf(text, sizeof text, dm_str(b, "dmGoldAdded"), cap - current);
    dm_game_notice(b, text);
    int dm_request_save(void); int saving = dm_request_save();
    LOG("获取金币：等级 %d，金币 %d → %d（+%d），请求保存 %d", level, current, after, after - current, saving);
}
