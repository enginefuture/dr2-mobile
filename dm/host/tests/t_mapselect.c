#include "../dm_mapselect.c"
#include <assert.h>
static int ingame=1,paused,panels,map=1;
int dm_ui_map_geom(float *cell){*cell=1;return map;}
int dm_uiscreen_ingame(void){return ingame;}
int dm_game_paused(void){return paused;}
int dm_hud_stack_msg(const char *a,const char *b){(void)a;(void)b;return panels;}
static dm_map_goal_view goals(void){dm_map_goal_view v={.n=3};v.kind[0]=DM_MAP_WAYPOINT;v.target[0]=46;v.kind[1]=DM_MAP_EXIT;v.target[1]=66;v.kind[2]=DM_MAP_TRUE_TOMB;v.target[2]=69;return v;}
int main(void){
 dm_map_goal_view v=goals();dm_mapselect_prepare(&v,46);assert(v.selected==0 && v.kind[0]==DM_MAP_TRUE_TOMB && v.target[1]==66);
 assert(dm_mapselect_filter(DM_BTN_LS|DM_BTN_A)==DM_BTN_A);
 for(int i=0;i<100;i++)assert(!(dm_mapselect_filter(DM_BTN_LS)&DM_BTN_LS));
 v=goals();dm_mapselect_prepare(&v,46);assert(v.selected==1 && v.target[v.selected]==66);
 dm_mapselect_filter(0);dm_mapselect_filter(DM_BTN_LS);v=goals();dm_mapselect_prepare(&v,46);assert(v.selected==2);
 dm_mapselect_filter(0);dm_mapselect_filter(DM_BTN_LS);v=goals();dm_mapselect_prepare(&v,46);assert(v.selected==0);
 // A reordered scan and a moving nearest exit keep the same destination selected.
 dm_mapselect_filter(0);dm_mapselect_filter(DM_BTN_LS);v=goals();v.x[1]=999;dm_mapselect_prepare(&v,46);assert(v.target[v.selected]==66);
 dm_mapselect_filter(0);panels=1;assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS);panels=0;
 assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS); // Held in a menu is never a new press on returning.
 dm_mapselect_filter(0);map=0;assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS);map=1;
 dm_mapselect_filter(0);paused=1;assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS);paused=0;
 dm_mapselect_filter(0);ingame=0;assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS);ingame=1;dm_mapselect_filter(0);
 v=goals();dm_mapselect_prepare(&v,2);assert(v.selected==0);
 v=(dm_map_goal_view){0};dm_mapselect_prepare(&v,0);assert(dm_mapselect_filter(DM_BTN_LS)==DM_BTN_LS);
 v=goals();v.n=1;dm_mapselect_prepare(&v,3);dm_mapselect_filter(0);dm_mapselect_filter(DM_BTN_LS);dm_mapselect_prepare(&v,3);assert(v.selected==0);
 puts("t_mapselect: single target, L3 edges, wrap, menu/pause/loading gates and stable identity passed");return 0;
}
