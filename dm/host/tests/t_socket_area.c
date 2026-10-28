#include "../hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
module_t g_mod[MAX_MOD];
int g_nmod;
static char table_path[512];
int dm_overlay_file(const char *rel, char *out, size_t n) {
    if (strcmp(rel, "inventory_footprints.tsv")) return 0;
    snprintf(out, n, "%s", table_path); return 1;
}
void svc_socket_area(uint64_t *);
int main(void) {
    char path[] = "/tmp/dm-socket-area-XXXXXX";
    int fd = mkstemp(path); if (fd < 0) return 1;
    FILE *f = fdopen(fd, "w"); if (!f) return 1;
    fputs("12\t2\t4\n13\t1\t1\n", f); fclose(f);
    snprintf(table_path, sizeof table_path, "%s", path);
    uint32_t item[2] = {4, 12}; uint64_t r[32] = {0};
    r[19] = (uint64_t)item; r[8] = 1; r[9] = 1; svc_socket_area(r);
    if (r[8] != 8) return 2;
    item[1] = 13; r[8] = r[9] = 1; svc_socket_area(r); if (r[8] != 1) return 3;
    item[1] = 999; r[8] = 2; r[9] = 3; svc_socket_area(r); if (r[8] != 6) return 4;
    item[1] = 12; r[8] = 0; r[9] = 1; svc_socket_area(r); if (r[8] != 0) return 5;
    item[0] = 0; r[8] = 2; r[9] = 3; svc_socket_area(r); if (r[8] != 6) return 6;
    unlink(path); puts("socket area tests passed"); return 0;
}
