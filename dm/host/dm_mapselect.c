#include "dm_mapselect.h"
#include "dm_input.h"
#include "dm_uiscreen.h"
#include "dm.h"
#include <stdatomic.h>
static _Atomic unsigned pending;
static _Atomic int count;
static int last_level, chosen;
static unsigned chosen_key;
static int priority(int kind) { return kind==DM_MAP_TRUE_TOMB?0:kind==DM_MAP_OBJECTIVE?1:kind==DM_MAP_EXIT?2:3; }
static unsigned key(const dm_map_goal_view *v,int i) { return ((unsigned)v->kind[i]<<16)|v->target[i]; }
void dm_mapselect_next(void) { if(atomic_load(&count)>1)atomic_fetch_add(&pending,1); }
void dm_mapselect_prepare(dm_map_goal_view *v,int level) {
    v->level=level; v->selected=0;
    if(!level || v->n<=0) { atomic_store(&count,0);atomic_store(&pending,0);if(!level){last_level=0;chosen_key=0;}return; }
    int order[DM_MAP_GOAL_MAX];for(int i=0;i<v->n;i++)order[i]=i;
    for(int i=1;i<v->n;i++)for(int j=i;j>0;j--) {
        int a=order[j-1],b=order[j],pa=priority(v->kind[a]),pb=priority(v->kind[b]);
        if(pa<pb || (pa==pb && v->target[a]<=v->target[b]))break;
        order[j-1]=b;order[j]=a;
    }
    dm_map_goal_view old=*v;
    for(int i=0;i<v->n;i++){int j=order[i];v->x[i]=old.x[j];v->y[i]=old.y[j];v->art[i]=old.art[j];v->kind[i]=old.kind[j];v->target[i]=old.target[j];memcpy(v->name[i],old.name[j],sizeof v->name[i]);}
    chosen=0;
    if(last_level==level) { for(int i=0;i<v->n;i++)if(key(v,i)==chosen_key){chosen=i;break;} }
    else atomic_store(&pending,0);
    last_level=level;unsigned steps=atomic_exchange(&pending,0);chosen=(chosen+steps%v->n)%v->n;
    chosen_key=key(v,chosen);v->selected=chosen;atomic_store(&count,v->n);
    static unsigned logged;static int logged_level;
    if(logged!=chosen_key || logged_level!=level){logged=chosen_key;logged_level=level;LOG("地图目标：关卡 %d，%d/%d，目标 %u，%s",level,chosen+1,v->n,v->target[chosen],v->name[chosen]);}
}
uint64_t dm_mapselect_filter(uint64_t buttons) {
    static int held,consumed;
    int down=!!(buttons&DM_BTN_LS),edge=down&&!held;held=down;
    if(!down){consumed=0;return buttons;}
    if(edge && atomic_load(&count)>0) {
        int dm_game_paused(void),dm_hud_stack_msg(const char *,const char *),dm_ui_map_geom(float *); float cell;
        if(dm_uiscreen_ingame() && dm_ui_map_geom(&cell)>0 && !dm_game_paused() && dm_hud_stack_msg("","")==0){dm_mapselect_next();consumed=1;}
    }
    return consumed ? buttons&~(uint64_t)DM_BTN_LS : buttons;
}
