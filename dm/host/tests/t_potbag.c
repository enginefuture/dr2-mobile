// Real journal, file format and commit barrier; the native item functions are verified in the engine replay.
#include "../dm_potbag.c"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static char wd[1024]; static uint64_t commits; static int poison;
void (*g_dm_pre_commit_hook[6])(const char *);
const char *dm_save_workdir(void) { return wd; }
uint64_t dm_fs_commit_count(void) { return commits; }
int dm_request_save(void) { return 0; }
void dm_fs_save_poison(int err) { poison = err; }
int dm_custom_get(const char *key) { (void)key; return 1; }
int dm_item_write(uint8_t *b, uint64_t it, uint8_t *out) { (void)b;(void)it;(void)out;abort(); }
uint64_t dm_item_make(uint8_t *b,uint64_t g,uint64_t r,const uint8_t *bits,int n,int *s) { (void)b;(void)g;(void)r;(void)bits;(void)n;(void)s;abort(); }
uint64_t dm_diag_srv_item(uint32_t id) { (void)id; return 0; }
int dm_ui_top_chk_on(void) { return 0; }
void dm_ui_top_chk(uint64_t r,const char *s,uint64_t w) { (void)r;(void)s;(void)w; }
const char *dm_untag(const char *s) { return s; }
const char *dm_str(uint8_t *b,const char *s) { (void)b;return s; }
static uint32_t code(const char *s) { uint32_t v=0;memcpy(&v,s,3);return v; }
static pb_item item(const char *s,unsigned char seed) { pb_item u={0};u.code=code(s);u.len=12;for(int i=0;i<u.len;i++)u.bits[i]=(uint8_t)(seed+i);return u; }
static void blob(const uint8_t *data,size_t n) { char p[1200];path_for(p,sizeof p,wd,"d2s");FILE*f=fopen(p,"wb");assert(f);assert(fwrite(data,1,n,f)==n);assert(!fclose(f)); }
static void add_pending(pb_item u,int put,int ground,uint32_t id) {
    pb_pending *p=&P.pending[P.np++];memset(p,0,sizeof *p);p->item=u;p->put=put;p->ground=ground;p->id=id;p->plen=u.len;memcpy(p->proof,u.bits,u.len);
}
static void ack(void) { acknowledge(NULL,0,0); }
int main(void) {
    char tmp[]="/tmp/dm-potbag-XXXXXX";char *dir=mkdtemp(tmp);assert(dir);strlcpy(wd,dir,sizeof wd);
    char p[1200];snprintf(p,sizeof p,"%s/Tester",wd);assert(!mkdir(p,0700));
    memset(&P,0,sizeof P);strcpy(P.name,"Tester");P.loaded=1;
    assert(g_dm_pre_commit_hook[5]==potbag_commit);
    unsigned char player[0x200]={0};assert(!dm_potbag_pickup((uint8_t*)1,1,(uint64_t)player,1));*(uint32_t*)(player+0xc)=5;assert(!dm_potbag_pickup((uint8_t*)1,1,(uint64_t)player,1));dm_potbag_on_uimsg(NULL,NULL,NULL);
    for(int i=1;i<=5;i++){char s[]="hp1";s[2]=(char)('0'+i);assert(dm_potbag_kind(code(s))==i-1);s[0]='m';assert(dm_potbag_kind(code(s))==i+4);}
    assert(dm_potbag_kind(code("rvs"))==10 && dm_potbag_kind(code("rvl"))==11 && dm_potbag_kind(code("vps"))<0);
    pb_item red=item("hp5",31),purple=item("rvl",101);uint8_t empty[32]={0};
    // A queued stow cannot be persisted while the old character still owns the bottle.
    P.live[P.nlive++]=red;add_pending(red,1,0,0);blob(red.bits,red.len);potbag_commit(wd);assert(!P.ndisk&&!P.pending[0].applied);
    blob(empty,sizeof empty);potbag_commit(wd);assert(P.ndisk==1&&P.pending[0].applied&&P.np==1);ack();assert(P.np==1);
    commits++;ack();assert(!P.np);load_side();assert(P.nlive==1&&same(&P.live[0],&red));
    // Released items must exist in the pending character save, and a successful file write alone never allows use.
    P.nlive=0;add_pending(red,0,0,99);blob(empty,sizeof empty);potbag_commit(wd);assert(P.ndisk==1&&!P.pending[0].applied&&!dm_potbag_ready(99));
    uint8_t packet[36]={0x26};uint32_t id=99;memcpy(packet+10,&id,4);assert(dm_potbag_packet_blocked(packet,sizeof packet));packet[0]=0x18;memcpy(packet+1,&id,4);assert(dm_potbag_packet_blocked(packet,sizeof packet));packet[0]=0x42;assert(!dm_potbag_packet_blocked(packet,sizeof packet));
    blob(red.bits,red.len);potbag_commit(wd);assert(P.ndisk==0&&!dm_potbag_ready(99));potbag_commit(wd);assert(P.ndisk==0);ack();assert(!dm_potbag_ready(99));
    commits++;ack();assert(dm_potbag_ready(99));load_side();assert(!P.nlive);
    // Ground pickup can be committed without an inventory cell, tied to the same character transaction.
    P.live[P.nlive++]=purple;add_pending(purple,1,1,0);blob(empty,sizeof empty);potbag_commit(wd);commits++;ack();assert(P.ndisk==1);
    // A failed side-file write rejects the transaction; retry does not apply a transfer twice.
    P.live[P.nlive++]=red;add_pending(red,1,0,0);path_for(p,sizeof p,wd,"swpot.tmp");assert(!mkdir(p,0700));poison=0;potbag_commit(wd);
    assert(poison==EIO&&P.ndisk==2&&P.np==1&&!P.pending[0].commit);ack();assert(P.np==1);assert(!rmdir(p));
    poison=0;potbag_commit(wd);assert(!poison&&P.ndisk==2);commits++;ack();load_side();assert(P.nlive==2);
    int c[12];dm_potbag_counts(c);assert(c[4]==1&&c[11]==1);
    // Death/cancellation during an uncommitted refill rejects a stale character snapshot before refunding.
    P.nlive=1;P.live[0]=purple;add_pending(red,0,0,100);refresh_pending(NULL,0);assert(P.pending[0].cancelled);
    blob(red.bits,red.len);poison=0;potbag_commit(wd);assert(poison==EAGAIN&&P.ndisk==2&&!P.pending[0].applied);
    blob(empty,sizeof empty);poison=0;potbag_commit(wd);assert(!poison&&P.ndisk==2&&P.nlive==1);commits++;ack();assert(P.nlive==2&&!P.np);
    // Corruption fails closed and leaves the original file untouched.
    path_for(p,sizeof p,wd,"swpot");FILE*f=fopen(p,"r+b");assert(f);fputc(0,f);fclose(f);load_side();assert(P.blocked&&!P.nlive);
    f=fopen(p,"rb");assert(f&&fgetc(f)==0);fclose(f);
    char command[1300];snprintf(command,sizeof command,"rm -rf '%s'",wd);assert(!system(command));
    puts("potbag: OK (12 types, two-phase stow/release, commit barrier, packet guard, ground pickup, failed write/retry, counts, corruption)");
}
