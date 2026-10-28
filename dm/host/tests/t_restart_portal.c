// Real bookmark/ownership/map validation and restart lifecycle, with native engine calls isolated.
#include "../dm_restart_portal.c"
#include <assert.h>

static int enabled = 1, made, notices, fail_make;
static unsigned char g1[0x20000], g2[0x20000], g3[0x20000], player_mem[0x200], pd[0x200], path[0x100];
static unsigned char portal_mem[0x200], portal_path[0x40], owner[0x20], field_room[0x100], field_ex[0x200], field_lv[0x300];
static unsigned char town_room[0x100], town_ex[0x200], town_lv[0x300];
static uint64_t target_level;
#define PTR(x) ((uint64_t)(uintptr_t)(x))
#define U64(p,o) (*(uint64_t *)((uint8_t *)(p)+(o)))
#define U32(p,o) (*(uint32_t *)((uint8_t *)(p)+(o)))
int dm_custom_get(const char *key) { assert(!strcmp(key, "restart_portal")); return enabled; }
const char *dm_str(uint8_t *base, const char *key) { (void)base; return key; }
void dm_game_notice(uint8_t *base, const char *msg) { (void)base; assert(msg); notices++; }
static uint64_t fake_level(uint8_t *b, uint64_t game, const rp_bookmark *p) { (void)b; (void)game; assert(p->level == 2 && p->x == 5050 && p->y == 5051); return target_level; }
static int fake_make(uint8_t *b, uint64_t game, uint64_t pl, uint64_t lv, const rp_bookmark *p) {
    (void)b; assert(pl == PTR(player_mem) && lv == PTR(field_lv)); assert(p->level == 2); made++;
    if (fail_make) return 0;
    U32(portal_mem,8) = 202; U32(pd,0xa0) = 202; U64(game,0x2a30+8*(202&127)) = PTR(portal_mem);
    return 1;
}
static void init_game(unsigned char *g, unsigned tick) { memset(g,0,0x20000); g[0x104]=1;g[0x106]=3;U32(g,0x170)=tick; }
static void step(unsigned char *g, unsigned t) { U32(g,0x170)=t;dm_restart_portal_tick(g,PTR(g),PTR(player_mem)); }
static void reset(void) {
    memset(&R,0,sizeof R); enabled=1;made=notices=fail_make=0;
    memset(player_mem,0,sizeof player_mem);memset(pd,0,sizeof pd);memset(path,0,sizeof path);
    memset(portal_mem,0,sizeof portal_mem);memset(portal_path,0,sizeof portal_path);memset(owner,0,sizeof owner);
    memset(field_room,0,sizeof field_room);memset(field_ex,0,sizeof field_ex);memset(field_lv,0,sizeof field_lv);
    memset(town_room,0,sizeof town_room);memset(town_ex,0,sizeof town_ex);memset(town_lv,0,sizeof town_lv);
    init_game(g1,100);init_game(g2,1);init_game(g3,1);
    strcpy((char *)pd,"Tester");U64(player_mem,0x10)=PTR(pd);U32(player_mem,8)=7;U32(player_mem,0xc)=5;U64(player_mem,0x38)=PTR(path);
    U64(path,0x20)=PTR(town_room);U64(town_room,0x18)=PTR(town_ex);U64(town_ex,0x90)=PTR(town_lv);U32(town_lv,0x1f8)=1;
    U32(portal_mem,0)=2;U32(portal_mem,4)=59;U32(portal_mem,8)=101;U64(portal_mem,0x38)=PTR(portal_path);U64(portal_mem,0xd0)=PTR(owner);U32(owner,8)=7;
    U64(portal_path,0)=PTR(field_room);U32(portal_path,0x10)=5050;U32(portal_path,0x14)=5051;
    U64(field_room,0x18)=PTR(field_ex);U64(field_ex,0x90)=PTR(field_lv);U32(field_lv,0x1f8)=2;U64(field_lv,0x10)=PTR(field_ex);
    U32(field_ex,0x60)=1000;U32(field_ex,0x64)=1000;U32(field_ex,0x68)=20;U32(field_ex,0x6c)=20;
    U32(pd,0xa0)=101;U64(g1,0x2a30+8*(101&127))=PTR(portal_mem);
    target_level=PTR(field_lv);ops.level=fake_level;ops.make=fake_make;
}
static void armed(void) { reset();step(g1,100);assert(R.last.valid && R.last.level==2 && R.last.x==5050);assert(dm_restart_portal_prepare("Tester",1)); }
int main(void) {
    armed();step(g1,101);assert(made==0); // never creates in the old game
    step(g2,1);assert(made==0);step(g2,11);assert(made==1 && notices==1 && R.last.valid && !R.armed.valid);
    step(g2,30);assert(made==1); // exactly once per restart
    U64(g2,0x2a30+8*(202&127))=0;U32(pd,0xa0)=0xffffffffu;step(g2,31); // used portal or unloaded remote room
    assert(R.last.valid && dm_restart_portal_prepare("Tester",1));step(g3,1);step(g3,11);assert(made==2); // repeated boss runs

    armed();dm_restart_portal_cancel();step(g2,1);step(g2,11);assert(!made && !R.last.valid); // normal exit/aborted restart
    reset();U32(owner,8)=8;step(g1,100);assert(!R.last.valid && !dm_restart_portal_prepare("Tester",1)); // another player's portal
    reset();U32(pd,0xa0)=0xffffffffu;step(g1,100);assert(!dm_restart_portal_prepare("Tester",1)); // no portal
    armed();assert(!dm_restart_portal_prepare("Other",1) && !R.armed.valid);assert(!dm_restart_portal_prepare("Tester",2));
    armed();strcpy((char *)pd,"Other");step(g2,1);step(g2,11);assert(!made && !R.armed.valid); // character isolation
    armed();g2[0x104]=2;step(g2,1);step(g2,11);assert(!made && !R.armed.valid); // difficulty isolation
    armed();g2[0x106]=2;step(g2,1);step(g2,11);assert(!made && !R.armed.valid); // data version isolation
    armed();enabled=0;step(g2,1);step(g2,11);assert(!made && !R.last.valid && !R.armed.valid);
    armed();U32(field_ex,0x60)++;step(g2,1);step(g2,11);assert(!made && notices==1 && !R.armed.valid); // changed map
    armed();U32(town_lv,0x1f8)=40;step(g2,1);step(g2,11);assert(!made && !R.armed.valid); // no automatic act changes
    armed();target_level=0;step(g2,1);step(g2,11);assert(!made && R.armed.valid);step(g2,252);assert(!made && !R.armed.valid && notices==1);
    armed();U32(player_mem,0xc)=17;step(g2,1);step(g2,11);assert(!made);U32(player_mem,0xc)=5;step(g2,16);assert(made==1); // wait for live player
    armed();fail_make=1;step(g2,1);step(g2,11);step(g2,20);assert(made==1 && !R.armed.valid && notices==1); // no endless retry after native failure
    // Same allocation reused for the new game: the server tick rewind is a new generation.
    armed();step(g1,1);step(g1,11);assert(made==1);
    // Layout fingerprint ignores linked-list order, but detects changed room geometry.
    reset();unsigned char ex2[0x200]={0};U32(ex2,0x60)=1020;U32(ex2,0x64)=1000;U32(ex2,0x68)=10;U32(ex2,0x6c)=10;
    U64(field_ex,0x48)=PTR(ex2);uint64_t h=layout_of(PTR(field_lv));U64(field_lv,0x10)=PTR(ex2);U64(ex2,0x48)=PTR(field_ex);U64(field_ex,0x48)=0;assert(layout_of(PTR(field_lv))==h);
    U32(ex2,0x68)++;assert(layout_of(PTR(field_lv))!=h);U64(field_room,0x18)=0;assert(object_level(PTR(portal_mem))==0); // null-safe during room unload
    puts("restart portal: OK (ownership, town capture, native boundary, new game, repeated runs, abort/normal exit, role/difficulty/version, map layout, timeout, death, pointer reuse)");
    return 0;
}
