// A hidden panel may retain an arbitrarily deep child tree. It must never be
// walked on idle frames; closing still gets exactly one restoration pass.
#include "../dm_inventory_ui.c"
#include <assert.h>
module_t g_mod[MAX_MOD];int g_nmod;
uint32_t dm_ui_gen(void){return 1;}
uint64_t dm_wref_get(dm_wref *r){return r->w;}
int dm_ui_unstable(void){return 0;}
int dm_ui_top_chk_on(void){return 0;}
void dm_ui_top_chk(uint64_t r,const char *n,uint64_t got){(void)r;(void)n;(void)got;}
int dm_padmode_current(void){return 0;}
int dm_ui_panel_state(void){return 0;}
int main(void){
 uint64_t root[32]={0},panel[256]={0},children[]={(uint64_t)panel};uint8_t base[8]={0};
 root[0x58/8]=(uint64_t)children;root[0x60/8]=1;panel[1]=(uint64_t)"PlayerInventoryExpansionLayout";
 // Poisoned descendants make an accidental hidden-tree read fail under ASan.
 panel[0x58/8]=1;panel[0x60/8]=1;
 for(int i=0;i<1000;i++)dm_inventory_ui_pump(base,(uint64_t)root);
 panel[0x58/8]=panel[0x60/8]=0;*((uint8_t *)panel+0x50)=1;
 dm_inventory_ui_pump(base,(uint64_t)root);assert(was_visible);
 *((uint8_t *)panel+0x50)=0;dm_inventory_ui_pump(base,(uint64_t)root);assert(!was_visible);
 panel[0x58/8]=1;panel[0x60/8]=1;
 for(int i=0;i<1000;i++)dm_inventory_ui_pump(base,(uint64_t)root);
 dm_inventory_ui_leave();assert(!was_visible);
 puts("t_inventory_idle: hidden subtrees untouched; close pass and leave reset passed");
}
