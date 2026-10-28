// 药囊与角色档同一次事务提交。取出的药先持久化到腰带，再允许喝或移动；
// 只看到「请求存档」或写侧文件成功都不算提交成功（还要等 fs commit 序号）。
#include "dm_potbag.h"
#include "dm_custom.h"
#include "dm_i18n.h"
#include "dm_widget.h"
#include "hle.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PB_CAP = 2048, PB_PENDING = 64, PB_BYTES = 64 };
typedef struct { uint32_t code; uint16_t len; uint8_t bits[PB_BYTES]; } pb_item;
typedef struct {
    pb_item item;
    uint8_t proof[PB_BYTES]; uint16_t plen;
    int put, ground, applied, cancelled;
    uint32_t id, slot;
    uint64_t commit;
} pb_pending;
static struct {
    pb_item live[PB_CAP], disk[PB_CAP]; int nlive, ndisk;
    pb_pending pending[PB_PENDING]; int np, loaded, blocked;
    char name[64]; uint64_t game, player; uint32_t tick, next, last_save;
} P;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int store_requested;
const char *dm_save_workdir(void);
uint64_t dm_fs_commit_count(void);
int dm_request_save(void);
void dm_fs_save_poison(int);
int dm_item_write(uint8_t *, uint64_t, uint8_t *);
uint64_t dm_item_make(uint8_t *, uint64_t, uint64_t, const uint8_t *, int, int *);
uint64_t dm_diag_srv_item(uint32_t);

int dm_potbag_kind(uint32_t code) {
    unsigned a = code & 255, b = code >> 8 & 255, c = code >> 16 & 255;
    if ((a == 'h' || a == 'm') && b == 'p' && c >= '1' && c <= '5') return (a == 'h' ? 0 : 5) + c - '1';
    if (a == 'r' && b == 'v' && (c == 's' || c == 'l')) return c == 's' ? 10 : 11;
    return -1;
}
static uint64_t rd(uint64_t p) { return p ? *(uint64_t *)(uintptr_t)p : 0; }
static uint32_t code_of(uint8_t *b, uint64_t it) { return ((uint32_t (*)(uint64_t))(b + 0x570260))(it); }
static uint32_t digest(const void *data, size_t n) { const uint8_t *b = data; uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u; return h; }
static void path_for(char *path, size_t n, const char *wd, const char *ext) { snprintf(path, n, "%s/%s/%s.%s", wd, P.name, P.name, ext); }
static int same(const pb_item *a, const pb_item *b) { return a->code == b->code && a->len == b->len && !memcmp(a->bits, b->bits, a->len); }
static int save_side(const char *wd) {
    char path[1200], tmp[1220]; path_for(path, sizeof path, wd, "swpot"); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    uint32_t header[4] = { 0x42505753, 1, (uint32_t)P.ndisk, digest(P.disk, P.ndisk * sizeof *P.disk) };
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    int ok = fwrite(header, sizeof header, 1, f) == 1 && fwrite(P.disk, sizeof *P.disk, P.ndisk, f) == (size_t)P.ndisk;
    if (fclose(f)) ok = 0;
    if (!ok || rename(tmp, path)) { remove(tmp); return 0; }
    return 1;
}
static void load_side(void) {
    P.nlive = P.ndisk = P.np = P.blocked = 0;
    const char *wd = dm_save_workdir(); if (!wd || !P.name[0]) { P.blocked = 1; return; }
    char path[1200]; path_for(path, sizeof path, wd, "swpot"); FILE *f = fopen(path, "rb");
    if (!f) { if (errno != ENOENT) P.blocked = 1; return; }
    uint32_t h[4]; int ok = fread(h, sizeof h, 1, f) == 1 && h[0] == 0x42505753 && h[1] == 1 && h[2] <= PB_CAP;
    if (ok) ok = fread(P.disk, sizeof *P.disk, h[2], f) == h[2] && fgetc(f) == EOF && digest(P.disk, h[2] * sizeof *P.disk) == h[3];
    for (unsigned i = 0; ok && i < h[2]; i++) if (dm_potbag_kind(P.disk[i].code) < 0 || P.disk[i].len < 8 || P.disk[i].len > PB_BYTES) ok = 0;
    fclose(f);
    if (!ok) { P.blocked = 1; LOG("【红】药囊：%s 校验失败，保留文件并停止收放", path); return; }
    P.nlive = P.ndisk = (int)h[2]; memcpy(P.live, P.disk, P.ndisk * sizeof *P.disk);
    LOG("药囊：%s 读入 %d 瓶", P.name, P.nlive);
}
static int blob_contains(const uint8_t *b, size_t n, const uint8_t *p, size_t m) {
    if (!m || m > n) return 0;
    for (size_t i = 0; i <= n - m; i++) if (!memcmp(b + i, p, m)) return 1;
    return 0;
}
static void potbag_commit(const char *wd) {
    pthread_mutex_lock(&mu);
    if (!P.loaded || P.blocked || !P.np) { pthread_mutex_unlock(&mu); return; }
    char path[1200]; path_for(path, sizeof path, wd, "d2s"); FILE *f = fopen(path, "rb"); uint8_t *blob = NULL; size_t n = 0;
    if (f) { fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f); if (size > 0 && size < (16 << 20)) { blob = malloc((size_t)size); if (blob) n = fread(blob, 1, (size_t)size, f); } fclose(f); }
    int changed = 0;
    for (int i = 0; i < P.np; i++) {
        pb_pending *p = &P.pending[i];
        if (p->applied) { changed = 1; continue; }
        if (!n) continue;
        int have = blob_contains(blob, n, p->proof, p->plen);
        if (p->cancelled) {
            // 死亡等流程移走了尚未提交的补药。旧快照若仍含这瓶就拒绝，等新快照再归还囊内。
            if (have) { dm_fs_save_poison(EAGAIN); continue; }
            p->applied = 1; changed = 1; continue;
        }
        if (p->put ? (!p->ground && have) : !have) continue;
        if (p->put) {
            if (P.ndisk == PB_CAP) { dm_fs_save_poison(EOVERFLOW); continue; }
            P.disk[P.ndisk++] = p->item;
        } else {
            int k = 0; while (k < P.ndisk && !same(&P.disk[k], &p->item)) k++;
            if (k == P.ndisk) { dm_fs_save_poison(EIO); continue; }
            P.disk[k] = P.disk[--P.ndisk];
        }
        p->applied = 1; changed = 1;
    }
    free(blob);
    if (changed) {
        if (!save_side(wd)) { dm_fs_save_poison(EIO); LOG("【红】药囊：侧文件写入失败，本次提交拒绝并重试"); }
        else for (int i = 0; i < P.np; i++) if (P.pending[i].applied) P.pending[i].commit = dm_fs_commit_count() + 1;
    }
    pthread_mutex_unlock(&mu);
}
__attribute__((constructor)) static void potbag_hook_install(void) { extern void (*g_dm_pre_commit_hook[6])(const char *); g_dm_pre_commit_hook[5] = potbag_commit; }
void dm_potbag_counts(int counts[12]) {
    memset(counts, 0, 12 * sizeof *counts); pthread_mutex_lock(&mu);
    for (int i = 0; i < P.nlive; i++) { int k = dm_potbag_kind(P.live[i].code); if (k >= 0) counts[k]++; }
    pthread_mutex_unlock(&mu);
}
int dm_potbag_ready(uint32_t id) {
    int ready = 1; pthread_mutex_lock(&mu);
    for (int i = 0; i < P.np; i++) if (!P.pending[i].put && P.pending[i].id == id) { ready = 0; break; }
    pthread_mutex_unlock(&mu); return ready;
}
int dm_potbag_packet_blocked(const uint8_t *packet, unsigned n) {
    // 0x26 原版使用物品包长 36，id 在 +10（0x2ced20）；移动物品包 id 在 +1。
    if (n < 5 || !((packet[0] >= 0x16 && packet[0] <= 0x2a) || (packet[0] >= 0x53 && packet[0] <= 0x55) || packet[0] == 0x63)) return 0;
    unsigned off = packet[0] == 0x26 ? 10 : 1; if (n < off + 4) return 0;
    uint32_t id; memcpy(&id, packet + off, 4); return !dm_potbag_ready(id);
}
static void notify_belt(uint8_t *b, uint64_t G, uint64_t player, uint64_t it, uint32_t slot) {
    // 原版 0x6dfe6c–0x6dfe8c：0 / 0x400，目标模式 2，页 255，线性腰带格。
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, it, 0, 0x400, 2, 255, 0, slot);
}
static int collect(uint8_t *b, uint64_t G, uint64_t player, uint64_t it, int ground) {
    if (P.blocked || P.nlive == PB_CAP || P.ndisk + P.np >= PB_CAP || P.np == PB_PENDING || dm_potbag_kind(code_of(b, it)) < 0) return 0;
    uint64_t d = rd(it + 0x10), pp = rd(it + 0x38); if (!d || !pp) return 0;
    uint8_t bits[1024]; int len = dm_item_write(b, it, bits); if (len < 8 || len > PB_BYTES) return 0;
    uint32_t code = code_of(b, it), id = *(uint32_t *)(it + 8), mode = *(uint32_t *)(it + 0xc), page = *(uint8_t *)(d + 0x55);
    int x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14);
    if (ground) {
        // 原版拾取 0x646c10：可拾取标志 + 视线检查，广播 remove-unit，再清碰撞与房间链。
        if (!(*(uint32_t *)(it + 0x124) & 2) || ((int (*)(uint64_t, uint64_t, int))(b + 0x5cee20))(player, it, 0x804)) return 0;
        for (uint64_t client = rd(G + 0x138), n = 0; client && n < 128; client = rd(client + 0x658), n++)
            ((void (*)(uint64_t, int, int, uint32_t))(b + 0x638ce0))(client, 10, 4, id);
        ((void (*)(uint64_t))(b + 0x5c8630))(it);
        ((void (*)(uint64_t))(b + 0x5c69a0))(it);
        *(uint32_t *)(it + 0x124) &= 0xfdfffffdu;
    } else ((void (*)(uint64_t))(b + 0x55e430))(it);
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(b + 0x642f70))(G, player, it, 0x20, 4, (int)mode, (int)page, y, x);
    ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, it);
    pb_item u = {0}; u.code = code; u.len = (uint16_t)len; memcpy(u.bits, bits, (size_t)len);
    P.live[P.nlive++] = u;
    pb_pending *p = &P.pending[P.np++]; memset(p, 0, sizeof *p);
    p->put = 1; p->ground = ground; p->item = u; p->plen = (uint16_t)len; memcpy(p->proof, bits, (size_t)len);
    LOG("药囊：收进 %.3s（id %u，来源 %s）→ 库存 %d", (const char *)&code, id, ground ? "地面" : mode == 2 ? "腰带调配" : "背包", P.nlive);
    return 1;
}
static int family(int kind) { return kind < 0 ? -1 : kind < 5 ? 0 : kind < 10 ? 1 : 2; }
static int belt_capacity(uint8_t *b, uint64_t player) {
    uint64_t inv = rd(player + 0x90), pages = inv ? rd(inv + 0x20) : 0, gear = pages ? rd(pages + 0x18) : 0;
    uint64_t belt = gear ? rd(gear + 0x40) : 0;
    int type = belt ? ((int (*)(uint64_t))(b + 0x5cc9b0))(belt) : 2;
    unsigned ver = *(uint8_t *)(player + 0x1bd); if (ver < 1 || ver > 3 || type < 0 || type > 255) return 0;
    uint64_t table = rd((uint64_t)b + 0x2d64a18 + ver * 16), rows = table ? rd(table + 0x1578) : 0;
    int n = rows ? *(uint8_t *)(rows + (uint64_t)type * 0x120 + 4) : 0;
    return n >= 4 && n <= 16 ? n : 0;
}
static int take_belt(uint8_t *b, uint64_t G, uint64_t player, int index) {
    if (index < 0 || index >= P.nlive || P.np >= PB_PENDING) return 0;
    pb_item u = P.live[index]; uint64_t nu = dm_item_make(b, G, 0, u.bits, u.len, NULL); if (!nu) return 0;
    uint64_t inv = rd(player + 0x90), d = rd(nu + 0x10), pp = rd(nu + 0x38); uint32_t slot = 0;
    if (!d || !pp || !((int (*)(uint64_t, uint64_t, uint32_t *, int))(b + 0x561de0))(inv, nu, &slot, 1) || slot >= 16) {
        ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, nu); return 0;
    }
    *(uint32_t *)(nu + 0xc) = 2; *(uint8_t *)(d + 0x55) = 255; *(int32_t *)(pp + 0x10) = (int32_t)slot; *(int32_t *)(pp + 0x14) = 0;
    uint64_t one = nu, extra = 0;
    ((int (*)(uint64_t, uint64_t, uint64_t *, uint64_t *))(b + 0x6f1940))(G, player, &one, &extra);
    if (!rd(d + 0xa0)) { LOG("药囊诊断：原版读档放腰带未成功（槽 %u）", slot); ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, nu); return 0; }
    uint8_t bits[1024]; int len = dm_item_write(b, nu, bits);
    if (len < 8 || len > PB_BYTES) {
        ((void (*)(uint64_t))(b + 0x55e430))(nu); ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, nu); return 0;
    }
    pb_pending *p = &P.pending[P.np++]; memset(p, 0, sizeof *p);
    p->item = u; p->id = *(uint32_t *)(nu + 8); p->slot = slot; p->plen = (uint16_t)len; memcpy(p->proof, bits, (size_t)len);
    P.live[index] = P.live[--P.nlive];
    LOG("药囊：%.3s → 腰带槽 %u（id %u），等实际提交后放行使用", (const char *)&u.code, slot, p->id);
    return 1;
}
int dm_potbag_pickup(uint8_t *b, uint64_t G, uint64_t player, uint32_t id) {
    if (!b || !G || !player || !dm_custom_get("pot_bag")) return 0;
    uint32_t mode = *(uint32_t *)(player + 0xc); uint64_t inv = rd(player + 0x90);
    if (mode == 0 || mode == 17 || !inv) return 0;
    pthread_mutex_lock(&mu); int ok = 0;
    uint64_t it = dm_diag_srv_item(id), pp = rd(player + 0x38), ip = it ? rd(it + 0x38) : 0;
    if (P.loaded && !P.blocked && P.game == G && P.player == player && it && ip && pp && *(uint32_t *)(it + 0xc) == 3) {
        int dx = *(int32_t *)(ip + 0x10) - *(uint16_t *)(pp + 2), dy = *(int32_t *)(ip + 0x14) - *(uint16_t *)(pp + 6);
        uint64_t ir = rd(ip), pr = rd(pp + 0x20), ie = ir ? rd(ir + 0x18) : 0, pe = pr ? rd(pr + 0x18) : 0;
        if (abs(dx) <= 3 && abs(dy) <= 3 && ie && pe && rd(ie + 0x90) == rd(pe + 0x90) && !rd(inv + 0x40)) ok = collect(b, G, player, it, 1);
    }
    pthread_mutex_unlock(&mu);
    if (ok) dm_request_save();
    return ok;
}
static void acknowledge(uint8_t *b, uint64_t G, uint64_t player) {
    uint64_t c = dm_fs_commit_count();
    for (int i = 0; i < P.np; ) {
        pb_pending *p = &P.pending[i];
        if (!p->commit || c < p->commit) { i++; continue; }
        if (p->cancelled) { if (P.nlive < PB_CAP) P.live[P.nlive++] = p->item; }
        else if (!p->put) {
            uint64_t it = dm_diag_srv_item(p->id), pp = it ? rd(it + 0x38) : 0;
            if (pp && *(uint32_t *)(it + 0xc) == 2) notify_belt(b, G, player, it, *(uint32_t *)(pp + 0x10));
        }
        LOG("药囊：提交 #%llu 确认 %.3s %s", (unsigned long long)c, (const char *)&p->item.code, p->put ? "入囊" : "出囊可用");
        P.pending[i] = P.pending[--P.np];
    }
}
static void refresh_pending(uint8_t *b, uint64_t G) {
    for (int i = 0; i < P.np; i++) {
        pb_pending *p = &P.pending[i]; if (p->put || p->applied || p->cancelled) continue;
        uint64_t it = dm_diag_srv_item(p->id), pp = it ? rd(it + 0x38) : 0;
        unsigned mode = it ? *(uint32_t *)(it + 0xc) : 255;
        if (it && mode == 2) {
            uint8_t bits[1024]; int len = dm_item_write(b, it, bits);
            if (len >= 8 && len <= PB_BYTES) { p->plen = (uint16_t)len; memcpy(p->proof, bits, (size_t)len); }
            continue;
        }
        if (!it || (mode == 3 && pp)) {
            if (it) {
                for (uint64_t client = rd(G + 0x138), n = 0; client && n < 128; client = rd(client + 0x658), n++)
                    ((void (*)(uint64_t, int, int, uint32_t))(b + 0x638ce0))(client, 10, 4, p->id);
                ((void (*)(uint64_t))(b + 0x5c8630))(it); ((void (*)(uint64_t))(b + 0x5c69a0))(it);
                ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, it);
            }
            p->cancelled = 1;
            LOG("药囊：未提交的补药 id %u 已离开腰带，等待一致存档后归还囊内", p->id);
        }
    }
}
static int test_driver(uint8_t *b, uint64_t G, uint64_t player, uint32_t tick) {
    // 只用于隔离存档的原版引擎闭环测试；产品不设置这些环境变量。
    static int drain = -1, low = -1, percent = 20;
    if (drain < 0) { const char *s = getenv("DM_TEST_POTBAG_DRAIN"); drain = s ? atoi(s) : 0; }
    if (low < 0) { const char *s = getenv("DM_TEST_POTBAG_LOW"); low = s ? atoi(s) : 0; if (s && strchr(s, ':')) { int p = atoi(strchr(s, ':') + 1); if (p > 0 && p < 100) percent = p; } }
    int changed = 0;
    if (drain && tick >= (uint32_t)drain && !P.np) {
        drain = 0; uint64_t inv = rd(player + 0x90);
        for (uint64_t it = rd(inv + 0x10); it; ) { uint64_t d = rd(it + 0x10), next = d ? rd(d + 0xb0) : 0; if (*(uint32_t *)(it + 0xc) == 2) changed |= collect(b, G, player, it, 0); it = next; }
        LOG("DM_TEST_POTBAG_DRAIN：腰带收回药囊，重新验证直接补入与提交屏障");
    }
    if (low && tick >= (uint32_t)low) {
        low = 0; uint64_t client = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c));
        for (int k = 0; k < 2; k++) { uint64_t u = k ? client : player; if (!u || (k && u == player)) continue;
            int max = ((int (*)(uint64_t, int, int))(b + 0x5b9290))(u, 7, 0);
            ((void (*)(uint64_t, int, int, int))(b + 0x5bc1f0))(u, 6, max / 100 * percent, 0);
            ((void (*)(uint64_t, int, int, int))(b + 0x5bc1f0))(u, 8, 0, 0);
        }
        LOG("DM_TEST_POTBAG_LOW：拍 %u 人物生命 %d%%、法力 0，验证自动喝药", tick, percent);
    }
    return changed;
}
void dm_potbag_tick(uint8_t *b, uint64_t G, uint64_t player) {
    pthread_mutex_lock(&mu);
    if (!b || !G || !player) { P.loaded = 0; P.game = P.player = 0; pthread_mutex_unlock(&mu); return; }
    uint32_t tick = *(uint32_t *)(G + 0x170); uint64_t pd = rd(player + 0x10);
    if (!pd || !*(char *)pd) { pthread_mutex_unlock(&mu); return; }
    if (!P.loaded || P.game != G || P.player != player || tick < P.tick || strncmp(P.name, (const char *)pd, 16)) {
        P.game = G; P.player = player; P.tick = tick; P.next = tick + 10; P.last_save = 0;
        memset(P.name, 0, sizeof P.name); memcpy(P.name, (const void *)pd, 16); load_side(); P.loaded = 1;
    }
    P.tick = tick; refresh_pending(b, G); acknowledge(b, G, player);
    uint64_t inv = rd(player + 0x90); int changed = test_driver(b, G, player, tick), enabled = dm_custom_get("pot_bag");
    uint32_t mode = *(uint32_t *)(player + 0xc);
    if (!P.blocked && mode != 0 && mode != 17 && inv && !rd(inv + 0x40) && (int32_t)(tick - P.next) >= 0) {
        P.next = tick + 5;
        if (enabled || atomic_load(&store_requested)) {
            int collected = 0;
            for (uint64_t it = rd(inv + 0x10); it; ) {
                uint64_t d = rd(it + 0x10), next = d ? rd(d + 0xb0) : 0;
                if (d && *(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0 && collect(b, G, player, it, 0)) { changed = collected = 1; break; }
                it = next;
            }
            if (!collected) atomic_store(&store_requested, 0);
        }
        // 一批收纳落盘后才补腰带，防止同一瓶同时有「入囊」和「出囊」两条未确认记录。
        if (!P.np && P.nlive) {
            int columns[3] = {0}, available[3] = {0}; uint64_t belt[16] = {0};
            for (int i = 0; i < P.nlive; i++) available[family(dm_potbag_kind(P.live[i].code))]++;
            for (uint64_t it = rd(inv + 0x10); it; ) {
                uint64_t d = rd(it + 0x10), pp = rd(it + 0x38); if (!d) break;
                if (*(uint32_t *)(it + 0xc) == 2 && pp) { unsigned slot = *(uint32_t *)(pp + 0x10); if (slot < 16) belt[slot] = it; }
                it = rd(d + 0xb0);
            }
            for (int col = 0; col < 4; col++) if (belt[col]) { int k = family(dm_potbag_kind(code_of(b, belt[col]))); if (k >= 0) columns[k]++; }
            // 三类药都能供给自动喝药：若某类没有腰带列，把重复类别的一列收回囊里腾出位置。
            int missing = -1, empty = 0; for (int col = 0; col < 4; col++) empty += belt[col] == 0;
            for (int k = 0; k < 3; k++) if (available[k] && !columns[k]) { missing = k; break; }
            if (missing >= 0 && !empty) for (int col = 3; col >= 0; col--) {
                int k = family(dm_potbag_kind(code_of(b, belt[col]))); if (k < 0 || columns[k] < 2) continue;
                for (int row = 0; row < 4; row++) if (belt[row * 4 + col]) changed |= collect(b, G, player, belt[row * 4 + col], 0);
                break;
            }
            int capacity = belt_capacity(b, player), occupied = 0;
            for (int i = 0; i < capacity; i++) occupied += belt[i] != 0;
            if (!P.np && occupied < capacity) {
                // 每类先有一列，再补满；一批最多 16 瓶，只提交一次。满列同档只试一瓶。
                for (int k = 0; k < 3; k++) if (available[k] && !columns[k]) {
                    int placed = 0;
                    for (int kind = k == 2 ? 11 : k * 5 + 4; kind >= k * 5 && !placed; kind--)
                        for (int i = 0; i < P.nlive; i++) if (dm_potbag_kind(P.live[i].code) == kind) { placed = take_belt(b, G, player, i); break; }
                    changed |= placed;
                }
                for (int kind = 11; kind >= 0; kind--) for (int tries = 0; tries < 16; tries++) {
                    int index = -1; for (int i = 0; i < P.nlive; i++) if (dm_potbag_kind(P.live[i].code) == kind) { index = i; break; }
                    if (index < 0 || !take_belt(b, G, player, index)) break;
                    changed = 1;
                }
            }
        }
    }
    int save = changed || (P.np && tick - P.last_save >= 25); if (save) P.last_save = tick;
    pthread_mutex_unlock(&mu); if (save) dm_request_save();
}

static _Atomic int view_requested;
static int tip_open;
static uint64_t ui_find(uint64_t root, const char *name, int depth) {
    if (!root || depth > 6) return 0;
    const char *n = *(const char **)(root + 8); if (n && !strcmp(n, name)) return root;
    uint64_t ch = rd(root + 0x58), count = rd(root + 0x60); if (count > 256) return 0;
    for (uint64_t i = 0; ch && i < count; i++) { uint64_t w = ui_find(rd(ch + i * 8), name, depth + 1); if (w) return w; }
    return 0;
}
static void ui_text(uint8_t *b, uint64_t w, const char *s) {
    if (!dm_widget_is(b, w, DM_VT_TEXT)) return;
    size_t n = strlen(s); if (rd(w + 0x90) == n && !memcmp((const void *)rd(w + 0x88), s, n)) return;
    ((void (*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n);
    ((void (*)(uint64_t))(b + 0xadc434))(w);
}
static void ui_move(uint64_t w, int x, int y) {
    if (!w) return;
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return;
    r[0] = x; r[1] = y; ((void (*)(uint64_t))rd(rd(w) + 0x10))(w);
}
void dm_potbag_on_uimsg(const char *cat, const char *act, const char *arg) {
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    if (!strcmp(arg, "TouchPotbag")) atomic_store(&view_requested, 1);
    if (!strcmp(arg, "TouchStore") || !strcmp(arg, "TouchPadStore")) atomic_store(&store_requested, 1);
}
void dm_potbag_step(uint8_t *b) {
    if (!b) return;
    uint64_t root = rd((uint64_t)b + 0x2f05458), inv = root ? dm_ui_top(root, "PlayerInventoryExpansionLayout") : 0;
    if (!inv) { tip_open = 0; atomic_store(&view_requested, 0); return; }
    uint64_t box = ui_find(inv, "TouchPouchBox", 0); if (!box) return;
    if (atomic_exchange(&view_requested, 0)) tip_open = !tip_open;
    int counts[12], total = 0; dm_potbag_counts(counts); for (int i = 0; i < 12; i++) total += counts[i];
    char number[24]; snprintf(number, sizeof number, "%d", total); ui_text(b, ui_find(box, "TouchPotbagCount", 0), number);
    uint64_t text = ui_find(box, "TouchPotbagTip", 0), bg = ui_find(box, "TouchPotbagTipBg", 0);
    if (!tip_open) { ui_move(text, 99999, 0); ui_move(bg, 99999, 0); return; }
    char s[1800]; int n = snprintf(s, sizeof s, "%s\n%s", dm_str(b, "dmPotbagTitle"), dm_str(b, "dmPotbagHint"));
    if (!total) n += snprintf(s + n, sizeof s - (size_t)n, "\n%s", dm_str(b, "dmPotbagEmpty"));
    for (int i = 0; i < 12; i++) if (counts[i]) {
        char code[4] = { i < 5 ? 'h' : i < 10 ? 'm' : 'r', i < 10 ? 'p' : 'v', i < 10 ? (char)('1' + i % 5) : i == 10 ? 's' : 'l', 0 };
        struct { const char *s; uint64_t n; } key = { code, 3 };
        const char *name = dm_untag(((const char *(*)(void *))(b + 0x7f5630))(&key));
        n += snprintf(s + n, sizeof s - (size_t)n, "\n%s ×%d", name && *name ? name : code, counts[i]);
    }
    ui_text(b, text, s);
    int h = text ? *(int32_t *)(text + 0x7c) : 0; if (h < 120) h = 120; if (h > 1100) h = 1100;
    int y = 1240 - h; ui_move(text, 340, y); if (bg) { *(int32_t *)(bg + 0x78) = 900; *(int32_t *)(bg + 0x7c) = h + 60; } ui_move(bg, 300, y - 30);
}
