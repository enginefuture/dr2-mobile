// Drive the real Custom page radio, Apply/Discard/Default and settings file.
#include <assert.h>
#include <unistd.h>
#include "../../host/dm_custom.c"

module_t g_mod[MAX_MOD]; int g_nmod;
static int pad_mode, hooks; static uint64_t keys;
const char *dm_save_commitdir(void) { return NULL; }
int dm_lang_group(void) { return DM_LG_EN; }
void dm_3d_quality_mode_set(int mode) { (void)mode; }
int dm_3d_quality_mode_get(void) { return 0; }
float dm_3d_scale_cur(void) { return 1; }
int dm_padmode_current(void) { return pad_mode; }
uint64_t dm_padmode_page_keys(uint64_t mask) { uint64_t k = keys & mask; keys = 0; return k; }
uint64_t dm_guest_frame(void) { return 1; }
void dm_ui_wheel_on(uint64_t w, int dir) { (void)w; (void)dir; }
uint32_t dm_ui_gen(void) { return 1; }
uint64_t dm_wref_get(dm_wref *r) { return r->w; }
int dm_ui_top_chk_on(void) { return 0; }
void dm_ui_top_chk(uint64_t root, const char *name, uint64_t got) { (void)root; (void)name; (void)got; }
static void changed(void) { hooks++; }
static uint64_t widget(const char *name) {
    uint8_t *w = calloc(1, 0xb00); assert(w);
    *(uint64_t *)w = (uint64_t)g_mod[0].base + DM_VT_TOGGLE;
    *(const char **)(w + 8) = name; w[0x50] = 1;
    return (uint64_t)w;
}
static void setting_file(const char *dir, const char *body) {
    char path[1100]; snprintf(path, sizeof path, "%s/sw_custom.txt", dir);
    FILE *f = fopen(path, "w"); assert(f); assert(fputs(body, f) >= 0); assert(!fclose(f));
    atomic_store(&g_loaded, 0);
}
int main(void) {
    char dir[] = "/tmp/dm-layout-XXXXXX"; assert(mkdtemp(dir)); dm_custom_init(dir);
    uint8_t base[16] = {0}; g_nmod = 1; strcpy(g_mod[0].name, "main"); g_mod[0].base = base;
    uint64_t root = widget("root"), page = widget("VideoOptionsPanel"), bx[DM_PAD_LAYOUT_N];
    uint64_t vt[96] = {0}; vt[0] = (uint64_t)base + 0xae5f28; *(uint64_t *)page = (uint64_t)vt;
    *(uint64_t *)(root + 0x58) = (uint64_t)&page; *(uint64_t *)(root + 0x60) = 1;
    for (int i = 0; i < DM_PAD_LAYOUT_N; i++) bx[i] = widget(k_layout_box[i]);
    *(uint64_t *)(page + 0x58) = (uint64_t)bx; *(uint64_t *)(page + 0x60) = DM_PAD_LAYOUT_N;
    g_dm_pad_layout_hook = changed;
    dm_custom_ui_pump(root); assert(TOGGLED(bx[0]) && dm_custom_get("pad_layout") == 0);
    set_toggle(bx[1], 1); dm_custom_ui_pump(root);
    assert(TOGGLED(bx[1]) && !TOGGLED(bx[0]) && *(uint8_t *)(page + 0x1a0));
    assert(dm_custom_get("pad_layout") == 0 && hooks == 0);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    assert(dm_custom_get("pad_layout") == 1 && hooks == 1);
    set_toggle(bx[2], 1); dm_custom_ui_pump(root);
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL); dm_custom_ui_pump(root);
    assert(dm_custom_get("pad_layout") == 1 && TOGGLED(bx[1]) && !TOGGLED(bx[2]) && hooks == 1);
    // A controller can focus the second row and cycle all four choices.
    pad_mode = 1; keys = DM_BTN_DOWN; dm_custom_ui_pump(root);
    keys = DM_BTN_A; dm_custom_ui_pump(root); assert(atomic_load(&g_pend[K_PAD_LAYOUT]) == 2);
    keys = DM_BTN_A; dm_custom_ui_pump(root); assert(atomic_load(&g_pend[K_PAD_LAYOUT]) == 3);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); assert(hooks == 2);
    pad_mode = 0; *(uint8_t *)(page + 0x50) = 0; dm_custom_ui_pump(root);
    atomic_store(&g_loaded, 0); assert(dm_custom_get("pad_layout") == 3); // numeric value survives reload
    dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL);
    assert(dm_custom_get("pad_layout") == 3 && atomic_load(&g_pend[K_PAD_LAYOUT]) == 0);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); assert(dm_custom_get("pad_layout") == 0 && hooks == 3);
    setting_file(dir, "auto_mana=0\npad_layout=2\n"); assert(dm_custom_get("pad_layout") == 2 && dm_custom_get("auto_mana") == 0);
    const char *invalid[] = { "pad_layout=99\n", "pad_layout=-1\n", "pad_layout=abc\n", "pad_layout=2junk\n", "auto_mana=1\n" };
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) { setting_file(dir, invalid[i]); assert(dm_custom_get("pad_layout") == 0); }
    char path[1100]; snprintf(path, sizeof path, "%s/sw_custom.txt", dir); unlink(path); rmdir(dir);
    for (int i = 0; i < DM_PAD_LAYOUT_N; i++) free((void *)bx[i]); free((void *)page); free((void *)root);
    puts("custom layout: OK (radio UI, controller navigation, Apply/Discard/Default, callback, persistence, invalid/old files)");
    return 0;
}
