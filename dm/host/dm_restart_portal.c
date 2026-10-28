#include "dm_restart_portal.h"
#include "dm_custom.h"
#include "dm_unit.h"
#include "dm_i18n.h"
#include "hle.h"
#include <stdlib.h>
#include <string.h>

enum { RP_BLUE = 59, RP_WAIT = 250, RP_ROOM_LIMIT = 2000 };
typedef struct {
    char name[64];
    int valid, difficulty, version, act, level, x, y;
    uint32_t portal_id;
    uint64_t layout, serial;
} rp_bookmark;
static struct {
    uint64_t G, serial;
    uint32_t last_tick, since, next_try;
    int entering;
    rp_bookmark last, armed;
} R;

static uint64_t rd(uint64_t a) { return a ? *(uint64_t *)(uintptr_t)a : 0; }
static int town(int act) { static const int levels[] = { 1, 40, 75, 103, 109 }; return act >= 0 && act < 5 ? levels[act] : 0; }
static int is_town(int level) { for (int i = 0; i < 5; i++) if (town(i) == level) return 1; return 0; }
static uint64_t object_level(uint64_t u) { uint64_t path = rd(u + 0x38), room = rd(path), ex = room ? rd(room + 0x18) : 0; return ex ? rd(ex + 0x90) : 0; }
static uint64_t find_object(uint64_t G, uint32_t id) {
    if (!G || id == 0xffffffffu) return 0;
    uint64_t u = rd(G + 0x2a30 + 8 * (id & 127));
    for (int n = 0; u && n < 4096; n++, u = rd(u + 0x158))
        if (*(uint32_t *)u == 2 && *(uint32_t *)(u + 8) == id) return u;
    return 0;
}
static uint64_t mix(uint64_t h, uint32_t v) { return (h ^ v) * 1099511628211ull; }
static uint64_t layout_of(uint64_t level) {
    // 房间矩形的无序指纹：不把房间指针、载入状态、怪物或本局随机数写进书签。
    // Order-independent room geometry fingerprint; no pointers, loaded state, monsters or combat RNG.
    uint64_t sum = 0, xored = 0, r = dm_level_rooms(level); unsigned n = 0;
    for (; r && n < RP_ROOM_LIMIT; r = rd(r + 0x48), n++) {
        uint64_t h = 1469598103934665603ull;
        for (int k = 0; k < 4; k++) h = mix(h, *(uint32_t *)(r + 0x60 + k * 4));
        sum += h; xored ^= h;
    }
    if (!n || r) return 0;
    return mix(mix(sum, (uint32_t)xored), (uint32_t)(xored >> 32) ^ n);
}
static uint64_t room_load(uint8_t *b, uint64_t G, uint64_t level, int x, int y) {
    int n = 0;
    for (uint64_t r = dm_level_rooms(level); r && n++ < RP_ROOM_LIMIT; r = rd(r + 0x48)) {
        int32_t *rect = (int32_t *)(r + 0x60);
        if (x < rect[0] * 5 || y < rect[1] * 5 || x >= (rect[0] + rect[2]) * 5 || y >= (rect[1] + rect[3]) * 5) continue;
        if (!rd(r + 0x58)) ((void (*)(int, uint64_t))(b + 0x52dcc0))(*(uint8_t *)(G + 0x106), r);
        return rd(r + 0x58);
    }
    return 0;
}
static uint64_t native_level(uint8_t *b, uint64_t G, const rp_bookmark *p) {
    uint64_t acts = rd(G + 0x190), act = acts ? rd(acts + 8 * p->act) : 0;
    if (!act) return 0;
    int32_t x = 0, y = 0;
    uint64_t room = ((uint64_t (*)(int, uint64_t, int, int, int32_t *, int32_t *, int))(b + 0x5596c0))(p->version, act, p->level, 11, &x, &y, 3);
    uint64_t ex = room ? rd(room + 0x18) : 0, level = ex ? rd(ex + 0x90) : 0;
    return level && *(int32_t *)(level + 0x1f8) == p->level ? level : 0;
}
static int native_make(uint8_t *b, uint64_t G, uint64_t player, uint64_t level, const rp_bookmark *p) {
    uint64_t room = room_load(b, G, level, p->x, p->y); if (!room) return 0;
    int32_t pos[2] = { p->x, p->y };
    // 与进局落点相同的玩家碰撞检查；最多挪 5 格，不把门放进墙或图外。
    uint64_t clear = ((uint64_t (*)(uint64_t, int32_t *, uint64_t, int, int, int, int, int, int))(b + 0x4d2c60))(room, pos, 0, 3, 0x1c09, 0x1c09, 0, 5, 1);
    if (!clear || abs(pos[0] - p->x) > 5 || abs(pos[1] - p->y) > 5) return 0;
    uint64_t portal = 0;
    int ok = ((int (*)(uint64_t, uint64_t, uint64_t, int, int, int, uint64_t *, int, int, int))(b + 0x7bfde0))(G, player, clear, pos[0], pos[1], town(p->act), &portal, RP_BLUE, 0, 0);
    if (!ok || !portal || *(uint32_t *)portal != 2 || *(uint32_t *)(portal + 4) != RP_BLUE) return 0;
    // 原版 0x64c8e0 → 0x64ce50：临时新门 id 在玩家数据 +0xa4，链接后 +0xa0 记野外门，
    // 并给两端的 +0xd0/+8 写所属玩家 id、打开城门。证据：3.3.94012 db0507f… townportal/owner.asm。
    uint64_t pd = rd(player + 0x10); if (!pd) return 0;
    *(uint32_t *)(pd + 0xa4) = *(uint32_t *)(portal + 8);
    ((void (*)(uint64_t, uint64_t))(b + 0x64ce50))(G, player);
    uint64_t owner = rd(portal + 0xd0);
    int linked = owner && *(uint32_t *)(owner + 8) == *(uint32_t *)(player + 8) && *(uint32_t *)(pd + 0xa0) == *(uint32_t *)(portal + 8);
    if (!linked) return 0;
    // 城镇端由原版放在正常传送门区域；静态传送门不能套用人物的重定位路径。
    return 1;
}
// Native boundaries are isolated so the lifecycle tests can drive real bookkeeping with synthetic game memory.
static struct {
    uint64_t (*level)(uint8_t *, uint64_t, const rp_bookmark *);
    int (*make)(uint8_t *, uint64_t, uint64_t, uint64_t, const rp_bookmark *);
} ops = { native_level, native_make };

void dm_restart_portal_cancel(void) { memset(&R.armed, 0, sizeof R.armed); R.entering = 0; }
int dm_restart_portal_prepare(const char *name, int difficulty) {
    dm_restart_portal_cancel();
    if (!dm_custom_get("restart_portal") || !name || !R.last.valid || strcmp(name, R.last.name) || difficulty != R.last.difficulty) {
        LOG("返场门：本次重开没有同角色、同难度的有效蓝门书签（或设置已关闭），正常重开"); return 0;
    }
    R.armed = R.last;
    LOG("返场门：重开前记住「%s」难度 %d，关卡 %d，蓝门 %d,%d，布局 %llx", name, difficulty, R.armed.level, R.armed.x, R.armed.y, (unsigned long long)R.armed.layout);
    return 1;
}
static void notice(uint8_t *b, const char *key) { void dm_game_notice(uint8_t *, const char *); dm_game_notice(b, dm_str(b, key)); }
static void capture(uint64_t G, uint64_t player, const char *name, int difficulty, int version) {
    uint64_t pd = rd(player + 0x10); uint32_t id = *(uint32_t *)(pd + 0xa0);
    uint64_t portal = find_object(G, id), owner = portal ? rd(portal + 0xd0) : 0;
    if (!portal || *(uint32_t *)(portal + 4) != RP_BLUE || !owner || *(uint32_t *)(owner + 8) != *(uint32_t *)(player + 8)) return;
    uint64_t level = object_level(portal), path = rd(portal + 0x38); int lv = level ? *(int32_t *)(level + 0x1f8) : 0, act = *(uint8_t *)(portal + 0x18);
    if (!level || !path || lv <= 0 || is_town(lv) || !town(act)) return;
    int x = *(int32_t *)(path + 0x10), y = *(int32_t *)(path + 0x14);
    if (R.last.valid && R.last.serial == R.serial && R.last.portal_id == id && R.last.level == lv && R.last.x == x && R.last.y == y) return;
    uint64_t fingerprint = layout_of(level); if (!fingerprint || x <= 0 || y <= 0) return;
    R.last = (rp_bookmark){ .valid = 1, .difficulty = difficulty, .version = version, .act = act, .level = lv, .x = x, .y = y, .portal_id = id, .layout = fingerprint, .serial = R.serial };
    strlcpy(R.last.name, name, sizeof R.last.name);
    LOG("返场门：记录自己的野外蓝门 id %u，关卡 %d，位置 %d,%d", id, lv, x, y);
}
void dm_restart_portal_tick(uint8_t *b, uint64_t G, uint64_t player) {
    if (!b || !G) return;
    uint32_t t = *(uint32_t *)(G + 0x170);
    if (G != R.G || t < R.last_tick) {
        R.G = G; R.serial++; R.since = t; R.next_try = t + 10; R.entering = R.armed.valid;
        memset(&R.last, 0, sizeof R.last);
    }
    R.last_tick = t;
    if (!dm_custom_get("restart_portal")) { dm_restart_portal_cancel(); memset(&R.last, 0, sizeof R.last); return; }
    if (!player || *(uint32_t *)player != 0) return;
    uint64_t pd = rd(player + 0x10); if (!pd || !*(char *)pd) return;
    char name[64] = {0}; memcpy(name, (const void *)pd, 16);
    int difficulty = *(uint8_t *)(G + 0x104), version = *(uint8_t *)(G + 0x106);
    if (R.armed.valid && R.entering && R.serial != R.armed.serial) {
        if (strcmp(name, R.armed.name) || difficulty != R.armed.difficulty || version != R.armed.version) {
            LOG("返场门：新局角色、难度或数据版本不同，丢弃书签"); dm_restart_portal_cancel();
        } else if (t - R.since > RP_WAIT) { LOG("返场门：新局等待超时，保留正常城镇落点"); notice(b, "dmRestartPortalFailed"); dm_restart_portal_cancel(); }
        else if ((int32_t)(t - R.next_try) >= 0) {
            R.next_try = t + 5;
            uint32_t mode = *(uint32_t *)(player + 0xc); int lv = dm_unit_level(player);
            if (mode == 0 || mode == 17 || !lv) return;
            if (lv != town(R.armed.act)) { LOG("返场门：新局落在关卡 %d，与原入口城镇不同，不自动换幕", lv); notice(b, "dmRestartPortalFailed"); dm_restart_portal_cancel(); }
            else {
                uint64_t level = ops.level(b, G, &R.armed);
                if (!level) return;
                uint64_t fingerprint = layout_of(level);
                if (!fingerprint) return;
                if (fingerprint != R.armed.layout) { LOG("返场门：目标地图布局已变化，不复用旧坐标"); notice(b, "dmRestartPortalChanged"); dm_restart_portal_cancel(); }
                else {
                    int ok = ops.make(b, G, player, level, &R.armed);
                    LOG("返场门：新局重建「%s」关卡 %d 的蓝门 %d,%d → %s", name, R.armed.level, R.armed.x, R.armed.y, ok ? "成功" : "失败，留在城镇");
                    notice(b, ok ? "dmRestartPortalRestored" : "dmRestartPortalFailed");
                    dm_restart_portal_cancel();
                }
            }
        }
    }
    // 蓝门回城后野外房间可能卸载：暂时找不到单位时保留最近一次有效书签。
    // 新局未手动准备时 last 已清空；普通退出、换角色和不同难度不会自动带门。
    capture(G, player, name, difficulty, version);
}
