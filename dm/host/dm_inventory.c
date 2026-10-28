// The grid's two icon draw calls only. Equipment slots and the held-item preview keep native sizes.
// Original 3cf280 -> slot position (+c0), pixel rect (+108), inherited scale (390b38), icon painter (3b93c4).
#include "hle.h"
#include <math.h>
#include <pthread.h>
static unsigned char footprint[1024][2];
static pthread_once_t footprint_once = PTHREAD_ONCE_INIT;
static void read_footprints(void) {
    char path[1100]; FILE *f=dm_overlay_file("inventory_footprints.tsv",path,sizeof path)?fopen(path,"r"):NULL;
    if(!f)return;
    unsigned id,w,h;while(fscanf(f,"%u\t%u\t%u",&id,&w,&h)==3)if(id<1024 && w>0 && w<=8 && h>0 && h<=8){footprint[id][0]=w;footprint[id][1]=h;}
    fclose(f);
}
static void load_footprints(void) { pthread_once(&footprint_once, read_footprints); }
typedef struct { uint64_t xy,wh; } icon_rect;
// Replace the two table-byte reads in the original icon painter. Only equipped
// items need native dimensions; grid icons retain the compact 1x1 footprint.
void svc_equip_size(uint64_t *regs) {
    const uint8_t *rec = (const uint8_t *)regs[8];
    const uint8_t *item = (const uint8_t *)regs[19];
    unsigned w = rec[0x11e], h = rec[0x11f];
    if (item && *(const uint32_t *)item == 4 && *(const uint32_t *)(item + 0xc) == 1) {
        load_footprints();
        unsigned cls = *(const uint32_t *)(item + 4);
        if (cls < 1024 && footprint[cls][0] && footprint[cls][1]) {
            w = footprint[cls][0]; h = footprint[cls][1];
        }
    }
    regs[21] = w; regs[8] = h;
}
// At the three socket-cap multiplications x19 is the item and w8/w9 are
// the dimensions from the compact table. Preserve the original fallback.
void svc_socket_area(uint64_t *regs) {
    uint32_t area = (uint32_t)regs[8] * (uint32_t)regs[9];
    uint64_t item = regs[19];
    if (area && item && *(uint32_t *)item == 4) {
        load_footprints();
        unsigned cls = *(unsigned *)(item + 4);
        if (cls < 1024 && footprint[cls][0] && footprint[cls][1])
            area = footprint[cls][0] * footprint[cls][1];
    }
    regs[8] = area;
}
void svc_inventory_icon(uint64_t *regs) {
    uint8_t *b=NULL;for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main"))b=g_mod[i].base;
    uint64_t grid=regs[0],item=regs[1]; if(!b || !grid || !item)return;
    load_footprints(); unsigned cls=*(unsigned *)(item+4);
    int w=cls<1024?footprint[cls][0]:0,h=cls<1024?footprint[cls][1]:0;
    if(*(unsigned *)item!=4 || !w || !h || *(uint64_t *)grid!=(uint64_t)b+0x2089ab8) { ((void(*)(uint64_t,uint64_t))(b+0x3cf280))(grid,item);return; }
    uint64_t vt=*(uint64_t *)grid;
    uint64_t pos=((uint64_t(*)(uint64_t,uint64_t))*(uint64_t *)(vt+0xc0))(grid,item);
    icon_rect rect=((icon_rect(*)(uint64_t,uint64_t,uint64_t))*(uint64_t *)(vt+0x108))(grid,0x100000001ull,pos);
    float scale=((float(*)(uint64_t))(b+0x390b38))(grid);
    int cw=*(int *)(grid+0x5b8),ch=*(int *)(grid+0x5bc);
    float s=fminf((cw-6.0f)/(98*w),(ch-6.0f)/(98*h));
    if(!(s>0 && s<=1 && scale>0)) { ((void(*)(uint64_t,uint64_t))(b+0x3cf280))(grid,item);return; }
    int x=(int32_t)rect.xy+(int)lroundf((cw-98*w*s)*scale*.5f),y=(int32_t)(rect.xy>>32)+(int)lroundf((ch-98*h*s)*scale*.5f);
    uint64_t xy=(uint32_t)x|((uint64_t)(uint32_t)y<<32); uint8_t opt[8]={0};
    ((void(*)(uint64_t,uint64_t,void*,float))(b+0x3b93c4))(item,xy,opt,scale*s);
}
