// Real room/preset layouts in synthetic memory: exits, waypoint filtering and all seven tombs.
#include "../dm_mapguide.c"
#include <assert.h>
int dm_overlay_file(const char *r,char *out,size_t n) { (void)r;(void)n;out[0]=0;return 0; }
const char *dm_str(uint8_t *b,const char *key) { (void)b;return key; }
static void setp(void *p,int o,void *v) { *(uintptr_t *)((char *)p+o)=(uintptr_t)v; }
static void seti(void *p,int o,int v) { *(int *)((char *)p+o)=v; }
int main(void) {
    _Alignas(8) char lv[0x300]={0}, rooms[2][0x100]={{0}}, dest[0x100]={0}, dl[0x300]={0}, edges[2][16]={{0}}, preset[2][0x30]={{0}};
    seti(lv,0x1f8,1); setp(lv,0x10,rooms[0]); setp(rooms[0],0x48,rooms[1]); seti(dl,0x1f8,2); setp(dest,0x90,dl);
    strcpy(levels[1].key,"town"); strcpy(levels[2].key,"field"); waypoints[119]=1;
    for(int i=0;i<2;i++) { setp(rooms[i],0x58,(void *)1); seti(rooms[i],0x60,i*20); seti(rooms[i],0x68,10); seti(rooms[i],0x6c,10); setp(rooms[i],0x78,edges[i]); setp(edges[i],0,dest); }
    seti(dest,0x60,10); seti(dest,0x68,10); seti(dest,0x6c,10);
    setp(rooms[0],0x98,preset[0]); setp(preset[0],0x10,preset[1]); seti(preset[0],0x20,2); seti(preset[0],4,119); seti(preset[0],8,15); seti(preset[0],0x24,20);
    seti(preset[1],0x20,1); seti(preset[1],4,119); seti(preset[1],8,45); seti(preset[1],0x24,40); // A monster with the same class is not a waypoint.
    dm_map_goal_view v={.px=120,.py=20}; dm_mapguide_collect((void *)1,1,(uintptr_t)lv,0,&v);
    while(scan.room) dm_mapguide_collect((void *)1,1,(uintptr_t)lv,0,&v);
    assert(v.n==2); int exit=-1,wp=-1;
    for(int i=0;i<v.n;i++) { if(v.kind[i]==DM_MAP_EXIT)exit=i; if(v.kind[i]==DM_MAP_WAYPOINT)wp=i; }
    assert(exit>=0 && wp>=0 && v.x[exit]==100 && v.x[wp]==15 && v.y[wp]==20);
    v=(dm_map_goal_view){.px=0,.py=0};dm_mapguide_collect((void *)1,1,(uintptr_t)lv,0,&v);assert(v.x[0]==50 || v.x[1]==50);
    dm_mapguide_collect(NULL,0,0,0,&v);assert(!scan.level);
    _Alignas(8) char tomb[7][0x100]={{0}}, tl[7][0x300]={{0}}, te[7][16]={{0}},act[0x200]={0};
    memset(rooms,0,sizeof rooms);setp(rooms[0],0x58,(void *)1);setp(lv,0x10,rooms[0]);seti(lv,0x1f8,46);setp(lv,0x1c8,act);seti(act,0x120,69);setp(rooms[0],0x78,te[0]);
    for(int i=0;i<7;i++) { seti(tl[i],0x1f8,66+i);strcpy(levels[66+i].key,"Tal Rasha's Tomb");setp(tomb[i],0x90,tl[i]);setp(te[i],0,tomb[i]);if(i<6)setp(te[i],8,te[i+1]); }
    v=(dm_map_goal_view){0};dm_mapguide_collect((void *)1,1,(uintptr_t)lv,0,&v);assert(v.n==7);
    int real=0;for(int i=0;i<7;i++){assert(v.art[i]==64+i);if(v.kind[i]==DM_MAP_TRUE_TOMB){real++;assert(v.art[i]==67 && strstr(v.name[i],"dmMapTrueTomb"));}}
    assert(real==1);
    v.n=DM_MAP_GOAL_MAX;dm_mapguide_collect((void *)1,1,(uintptr_t)lv,0,&v);assert(v.n==DM_MAP_GOAL_MAX);
    char label[128];clean_name(label,sizeof label,"塔拉夏古墓[17/49/80]");assert(!strcmp(label,"塔拉夏古墓"));
    clean_name(label,sizeof label,"Cold Plains [2/36/68]");assert(!strcmp(label,"Cold Plains"));
    clean_name(label,sizeof label,"Room [Quest]");assert(!strcmp(label,"Room [Quest]"));
    puts("t_mapguide: native-shaped exit/waypoint/tomb/nearest/reset/capacity checks passed");return 0;
}
