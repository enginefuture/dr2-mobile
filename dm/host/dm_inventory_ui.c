// Spacious standalone gamepad inventory, with the same native grid and saved coordinates.
// Restore compact geometry before stash / vendor / cube use; never replace item units or visibility.
#include "dm_inventory_ui.h"
#include "dm_cursor.h"
#include "dm_widget.h"
#include "hle.h"
typedef struct { dm_wref ref; int x,y,cw,ch,wide; } slot;
static slot slots[64];
static dm_wref panel_ref;
static int nslot, was_visible;
static uint64_t find(uint64_t w,const char *name,int depth) {
    if(!w || depth>12)return 0;
    const char *s=*(const char **)(w+8);if(s && !strcmp(s,name))return w;
    uint64_t ch=*(uint64_t *)(w+0x58),n=*(uint64_t *)(w+0x60);if(!ch || n>512)return 0;
    for(uint64_t i=0;i<n;i++){uint64_t r=find(*(uint64_t *)(ch+i*8),name,depth+1);if(r)return r;}
    return 0;
}
static slot *remember(uint64_t w) {
    for(int i=0;i<nslot;i++)if(dm_wref_get(&slots[i].ref)==w)return &slots[i];
    for(int i=0;i<=nslot && i<64;i++)if(i==nslot || !dm_wref_get(&slots[i].ref)) {
        if(i==nslot)nslot++;slot *s=&slots[i];memset(s,0,sizeof *s);dm_wref_set(&s->ref,w);s->x=*(int *)(w+0x70);s->y=*(int *)(w+0x74);return s;
    }
    return NULL;
}
static void move(uint64_t w,slot *s,int wide,int dx,int dy) {
    int x=s->x+(wide?dx:0),y=s->y+(wide?dy:0);
    if(*(int *)(w+0x70)!=x || *(int *)(w+0x74)!=y){*(int *)(w+0x70)=x;*(int *)(w+0x74)=y;((void(*)(uint64_t))*(uint64_t *)(*(uint64_t *)w+0x10))(w);}
}
static void image(uint8_t *b,uint64_t w,const char *path) {
    struct { char *p;uint64_t n,cap;char buf[160]; } s;size_t n=strlen(path);if(n>=sizeof s.buf)return;
    memcpy(s.buf,path,n+1);s.p=s.buf;s.n=n;s.cap=(sizeof s.buf-1)|(1ull<<63);
    ((void(*)(uint64_t,void *))(b+0xac1704))(w,&s);
}
static int gear(const char *s) {
    return !strncmp(s,"slot_",5) || !strncmp(s,"background_",11) || !strncmp(s,"weaponswap_",11) || !strncmp(s,"text_i",6) || !strncmp(s,"WeaponSwap",10) || !strcmp(s,"Belt");
}
void dm_inventory_ui_pump(uint8_t *b,uint64_t root) {
    if(!b || !root || dm_ui_unstable())return;
    uint64_t p=dm_ui_top(root,"PlayerInventoryExpansionLayout");if(!p)return;
    if(dm_wref_get(&panel_ref)!=p){memset(slots,0,sizeof slots);nslot=0;was_visible=0;dm_wref_set(&panel_ref,p);}
    int visible=*(uint8_t *)(p+0x50)!=0;
    if(!visible && !was_visible)return;
    was_visible=visible; // Restore compact geometry once when closing, then do no tree traversal.
    int dm_padmode_current(void);
    uint64_t overlay=dm_ui_top(root,"ControllerOverlay");
    int wide=dm_padmode_current() && overlay && *(uint8_t *)(overlay+0x50) && *(uint8_t *)(p+0x50) && !(dm_ui_panel_state()&(DM_PS_BANK|DM_PS_VENDOR|DM_PS_CUBE|DM_PS_IMBUE|DM_PS_VAULT|DM_PS_DEAD));
    uint64_t g=find(p,"grid",0),bg=find(p,"background",0);
    if(!dm_widget_is(b,g,DM_VT_SLOT_B) || !dm_widget_is(b,bg,DM_VT_IMAGE))return;
    // Only run against the generated 84-cell layout; unmodified game data remains untouched.
    if(*(int *)(g+0x628)!=14 || *(int *)(g+0x62c)!=6)return;
    slot *gs=remember(g),*bs=remember(bg);if(!gs || !bs)return;
    if(!gs->cw){gs->cw=*(int *)(g+0x5b8);gs->ch=*(int *)(g+0x5bc);}
    int cell=wide?112:gs->cw;
    if(*(int *)(g+0x5b8)!=cell || *(int *)(g+0x5bc)!=(wide?112:gs->ch)) {
        *(int *)(g+0x5b8)=cell;*(int *)(g+0x5bc)=wide?112:gs->ch;
        ((void(*)(uint64_t))*(uint64_t *)(*(uint64_t *)g+0x10))(g);
    }
    move(g,gs,wide,480-gs->x,360-gs->y);
    if(bs->wide!=wide){image(b,bg,wide?"Controller/Panel/2compact/Inventory84Wide":"Controller/Panel/2compact/Inventory84");bs->wide=wide;LOG("背包排版：%s，14×6，每格 %d",wide?"左装备、右背包":"并排紧凑",cell);}
    move(bg,bs,wide,-800,0);
    uint64_t ch=*(uint64_t *)(p+0x58),n=*(uint64_t *)(p+0x60);
    for(uint64_t i=0;ch && i<n && i<256;i++) {
        uint64_t w=*(uint64_t *)(ch+i*8);const char *name=w?*(const char **)(w+8):NULL;
        if(!name || !gear(name))continue;slot *s=remember(w);if(s)move(w,s,wide,-800,200);
    }
    static const char *footer[]={"gold_amount","gold_button","TouchPouch","TouchPouchIcon","TouchPouchCount","TouchPotbagBg","TouchPotbag","TouchPotbagLabel","TouchPotbagCount","TouchPouchStoreBg","TouchStore","TouchPouchStoreLabel"};
    for(unsigned i=0;i<sizeof footer/sizeof *footer;i++){uint64_t w=find(p,footer[i],0);if(!w)continue;slot *s=remember(w);if(s)move(w,s,wide,516,-174);}
}

void dm_inventory_ui_leave(void) {
    uint8_t *b=NULL;for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main"))b=g_mod[i].base;
    for(int i=0;i<nslot;i++) {
        slot *s=&slots[i];uint64_t w=dm_wref_get(&s->ref);if(!w)continue;
        if(s->wide && b)image(b,w,"Controller/Panel/2compact/Inventory84");
        if(s->cw){*(int *)(w+0x5b8)=s->cw;*(int *)(w+0x5bc)=s->ch;((void(*)(uint64_t))*(uint64_t *)(*(uint64_t *)w+0x10))(w);}
        move(w,s,0,0,0);
    }
    memset(slots,0,sizeof slots);nslot=0;was_visible=0;dm_wref_set(&panel_ref,0);
}
