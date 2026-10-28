#include "../hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static char path[512];
int dm_overlay_file(const char *rel, char *out, size_t n) {
    if (strcmp(rel, "inventory_footprints.tsv")) return 0;
    snprintf(out, n, "%s", path); return 1;
}
void svc_equip_size(uint64_t *);
int main(void) {
    char fpath[]="/tmp/dm-equip-size-XXXXXX"; int fd=mkstemp(fpath); if(fd<0)return 1;
    FILE *f=fdopen(fd,"w"); if(!f)return 1; fputs("12\t2\t3\n",f); fclose(f); snprintf(path,sizeof path,"%s",fpath);
    uint32_t item[4]={4,12,0,1}; unsigned char record[448]={0}; record[0x11e]=1; record[0x11f]=1;
    uint64_t regs[32]={0}; regs[8]=(uint64_t)record; regs[19]=(uint64_t)item; svc_equip_size(regs);
    if(regs[21]!=2 || regs[8]!=3) return 2;
    item[3]=0; regs[8]=(uint64_t)record; svc_equip_size(regs); if(regs[21]!=1 || regs[8]!=1)return 3;
    item[3]=1; item[1]=999; regs[8]=(uint64_t)record; svc_equip_size(regs); if(regs[21]!=1 || regs[8]!=1)return 4;
    unlink(fpath); puts("equipment icon size tests passed"); return 0;
}
