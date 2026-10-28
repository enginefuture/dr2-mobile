#include "dm_mapguide.h"
#include "dm_i18n.h"
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LEVELS 256
#define POINTS 128
static struct { char key[128]; int to[8], warp[8]; } levels[LEVELS];
static unsigned char waypoints[1024];
typedef struct { int x, y, to, kind; } guide_point;
static struct { uint64_t level, room; int id, n, rooms; guide_point point[POINTS]; } scan;
static uint64_t ptr(uint64_t a) { return a ? *(uint64_t *)a : 0; }
static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec; }

static void load_data(void) {
    static int loaded; if (loaded) return; loaded = 1;
    char path[1100], line[2048]; int n = 0;
    FILE *f = dm_overlay_file("map_guidance.tsv", path, sizeof path) ? fopen(path, "r") : NULL;
    if (!f) { LOG("地图指引：缺少 map_guidance.tsv"); return; }
    while (fgets(line, sizeof line, f)) {
        char *s = NULL, *tag = strtok_r(line, "\t\r\n", &s), *id = strtok_r(NULL, "\t\r\n", &s);
        if (!tag || !id) continue;
        int k = atoi(id);
        if (!strcmp(tag, "W") && k >= 0 && k < 1024) waypoints[k] = 1;
        if (strcmp(tag, "L") || k <= 0 || k >= LEVELS) continue;
        char *key = strtok_r(NULL, "\t\r\n", &s); if (!key) continue;
        snprintf(levels[k].key, sizeof levels[k].key, "%s", key);
        for (int i = 0; i < 8; i++) {
            char *a = strtok_r(NULL, "\t\r\n", &s), *w = strtok_r(NULL, "\t\r\n", &s);
            levels[k].to[i] = a ? atoi(a) : 0; levels[k].warp[i] = w ? atoi(w) : -1;
        }
        n++;
    }
    fclose(f); LOG("地图指引：载入 %d 个区域的名字与出口数据", n);
}
static void add(int x, int y, int to, int kind) {
    for (int i = 0; i < scan.n; i++) if (scan.point[i].to == to && scan.point[i].kind == kind && abs(scan.point[i].x-x) < 20 && abs(scan.point[i].y-y) < 20) return;
    if (scan.n < POINTS) scan.point[scan.n++] = (guide_point){x,y,to,kind};
}
static void edge_point(uint64_t a, uint64_t b, int *x, int *y) {
    int ax=*(int *)(a+0x60)*5, ay=*(int *)(a+0x64)*5, aw=*(int *)(a+0x68)*5, ah=*(int *)(a+0x6c)*5;
    int bx=*(int *)(b+0x60)*5, by=*(int *)(b+0x64)*5, bw=*(int *)(b+0x68)*5, bh=*(int *)(b+0x6c)*5;
    *x=ax+aw/2; *y=ay+ah/2;
    int lo=ay>by?ay:by, hi=ay+ah<by+bh?ay+ah:by+bh;
    if ((ax+aw==bx || bx+bw==ax) && lo < hi) { *x=ax+aw==bx?bx:ax; *y=(lo+hi)/2; return; }
    lo=ax>bx?ax:bx; hi=ax+aw<bx+bw?ax+aw:bx+bw;
    if ((ay+ah==by || by+bh==ay) && lo < hi) { *x=(lo+hi)/2; *y=ay+ah==by?by:ay; }
}
static int collision(uint64_t ex,int x,int y) {
    uint64_t room=ptr(ex+0x58),g=room?ptr(room+0x38):0,tiles=g?ptr(g+0x20):0;
    if(!tiles)return 0xffff;
    int rx=*(int *)(room+0x80),ry=*(int *)(room+0x84),w=*(int *)(room+0x88),h=*(int *)(room+0x8c),stride=*(int *)(g+8);
    if(x<rx || y<ry || x>=rx+w || y>=ry+h || stride<w || w<=0 || h<=0 || w>2048 || h>2048)return 0xffff;
    return ((uint16_t *)tiles)[(y-ry)*stride+x-rx];
}
static void outdoor_edges(uint8_t *b,int version,uint64_t r) {
    uint64_t adj=ptr(r+0x10),n=ptr(r+0x18); if(!adj || n>64)return;
    int ax=*(int *)(r+0x60)*5,ay=*(int *)(r+0x64)*5,aw=*(int *)(r+0x68)*5,ah=*(int *)(r+0x6c)*5;
    for(uint64_t i=0;i<n;i++) {
        uint64_t t=ptr(adj+i*8),lv=t?ptr(t+0x90):0;int to=lv?*(int *)(lv+0x1f8):0;
        if(to<=0 || to>=LEVELS || to==scan.id || !levels[to].key[0])continue;
        int bx=*(int *)(t+0x60)*5,by=*(int *)(t+0x64)*5,bw=*(int *)(t+0x68)*5,bh=*(int *)(t+0x6c)*5;
        int vertical=ax+aw==bx || bx+bw==ax, horizontal=ay+ah==by || by+bh==ay;
        if(!vertical && !horizontal)continue;
        int lo=vertical?(ay>by?ay:by):(ax>bx?ax:bx),hi=vertical?(ay+ah<by+bh?ay+ah:by+bh):(ax+aw<bx+bw?ax+aw:bx+bw);
        if(lo>=hi)continue;
        if(!ptr(t+0x58))((void(*)(int,uint64_t))(b+0x52dcc0))(version,t);
        int edge=vertical?(ax+aw==bx?bx:ax):(ay+ah==by?by:ay),begin=-1,best=0,mid=0;
        for(int q=lo;q<=hi;q++) {
            int x=vertical?edge:q,y=vertical?q:edge,open=q<hi && !((collision(r,x-(vertical&&ax<bx),y-(!vertical&&ay<by)) | collision(t,x-(vertical&&bx<ax),y-(!vertical&&by<ay)))&0x409);
            if(open && begin<0)begin=q;
            if(!open && begin>=0){if(q-begin>best){best=q-begin;mid=(q+begin)/2;}begin=-1;}
        }
        if(best>=3)add(vertical?edge:mid,vertical?mid:edge,to,DM_MAP_EXIT);
    }
}
static void scan_room(uint64_t r) {
    int rx=*(int *)(r+0x60)*5, ry=*(int *)(r+0x64)*5;
    for (uint64_t p=ptr(r+0x98), k=0; p && k<256; p=ptr(p+0x10),k++) {
        unsigned type=*(unsigned *)(p+0x20), cls=*(unsigned *)(p+4);
        if (type==2 && cls<1024 && waypoints[cls]) add(rx+*(int *)(p+8), ry+*(int *)(p+0x24), scan.id, DM_MAP_WAYPOINT);
    }
    for (uint64_t e=ptr(r+0x78), k=0; e && k<32; e=ptr(e+8),k++) {
        uint64_t target=ptr(e), level=target?ptr(target+0x90):0;
        int to=level?*(int *)(level+0x1f8):0;
        if (!to || to==scan.id || to>=LEVELS || !levels[to].key[0]) continue;
        int x,y; edge_point(r,target,&x,&y);
        if (scan.id==46 && to>=66 && to<=72) {
            static const int at[7][2]={{35,35},{29,6},{30,6},{24,5},{13,32},{14,25},{15,32}};
            x=rx+at[to-66][0]; y=ry+at[to-66][1];
        } else {
            for (uint64_t p=ptr(r+0x98), j=0; p && j<256; p=ptr(p+0x10),j++) {
                if (*(unsigned *)(p+0x20) != 5) continue;
                int cls=*(int *)(p+4), match=cls==to;
                for (int t=0; t<8; t++) if (levels[scan.id].to[t]==to && levels[scan.id].warp[t]>=0 && (cls==levels[scan.id].warp[t] || cls==levels[scan.id].warp[t]+28)) match=1;
                if (match) { x=rx+*(int *)(p+8); y=ry+*(int *)(p+0x24); break; }
            }
        }
        add(x,y,to,DM_MAP_EXIT);
    }
}
// Area-level annotations already appear in the native area title; don't repeat them in the compact guide.
static void clean_name(char *out,size_t n,const char *name) {
    snprintf(out,n,"%s",name?name:"");
    char *tail=strrchr(out,'[');if(!tail || !strchr(tail,']'))return;
    for(char *p=tail+1;*p && *p!=']';p++)if(!((*p>='0' && *p<='9') || *p=='/' || *p=='-' || *p==' '))return;
    if(tail[1]<'0' || tail[1]>'9')return;
    *tail=0;while(tail>out && tail[-1]==' ')*--tail=0;
}
static void append(uint8_t *b, const guide_point *p, int true_tomb, dm_map_goal_view *v) {
    if (v->n>=DM_MAP_GOAL_MAX) return;
    int i=v->n++, tomb=scan.id==46 && p->to>=66 && p->to<=72;
    v->x[i]=p->x; v->y[i]=p->y; v->kind[i]=p->kind; v->target[i]=p->to;
    const char *name=dm_str(b, levels[p->to].key); if (!name || !*name) name=levels[p->to].key;
    char plain[128];clean_name(plain,sizeof plain,name);name=plain;
    if (p->kind==DM_MAP_WAYPOINT) name=dm_str(b,"dmMapWaypoint");
    if (tomb) {
        v->art[i]=64+p->to-66;
        if (p->to==true_tomb) { v->kind[i]=DM_MAP_TRUE_TOMB; snprintf(v->name[i],sizeof v->name[i],"%s · %s",dm_str(b,"dmMapTrueTomb"),name); }
        else snprintf(v->name[i],sizeof v->name[i],"%s %d",name,p->to-65);
    } else snprintf(v->name[i],sizeof v->name[i],"%s",name?name:"");
}
void dm_mapguide_collect(uint8_t *b, uint64_t player, uint64_t level, int version, dm_map_goal_view *v) {
    if (!b || !player || !level) { memset(&scan,0,sizeof scan); return; }
    load_data(); int id=*(int *)(level+0x1f8); if (id<=0 || id>=LEVELS) return;
    if (scan.level!=level) { memset(&scan,0,sizeof scan); scan.level=level; scan.id=id; scan.room=ptr(level+0x10); }
    if (scan.room && version>=0) {
        uint64_t t=ns();
        while (scan.room && ns()-t<1500000 && scan.rooms<2000) {
            uint64_t r=scan.room;
            if (!ptr(r+0x58)) ((void (*)(int,uint64_t))(b+0x52dcc0))(version,r);
            scan_room(r); outdoor_edges(b,version,r); scan.room=ptr(r+0x48); scan.rooms++;
        }
        if (scan.rooms>=2000) scan.room=0;
        if (!scan.room) { LOG("地图指引：关卡 %d 扫描 %d 间房，找到 %d 个出口与小站",id,scan.rooms,scan.n); for(int i=0;i<scan.n;i++) LOG("地图指引点：%d → %d 类型 %d 坐标 %d,%d",id,scan.point[i].to,scan.point[i].kind,scan.point[i].x,scan.point[i].y); }
    }
    if (scan.room) return;
    uint64_t act=ptr(level+0x1c8); int true_tomb=id==46 && act?*(int *)(act+0x120):0;
    for (int i=0; i<scan.n; i++) {
        guide_point *p=&scan.point[i]; double dx=p->x-v->px,dy=p->y-v->py,d=dx*dx+dy*dy; int best=1;
        for (int j=0; j<scan.n; j++) if (j!=i && scan.point[j].to==p->to && scan.point[j].kind==p->kind) {
            double a=scan.point[j].x-v->px,c=scan.point[j].y-v->py,q=a*a+c*c;
            if (q<d || (q==d && j<i)) { best=0; break; }
        }
        if (best) append(b,p,true_tomb,v);
    }
}
