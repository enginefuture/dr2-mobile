// Exercise the real GameController input path without requiring physical hardware.
#include <assert.h>
#include <stdio.h>
#include "../Sources/dm_gamepad.m"

static uint64_t sent;
static int portals, empty_slot, navigation;
void (*g_dm_pad_layout_hook)(void);
int dm_custom_get(const char *key) { (void)key; return DM_PAD_LAYOUT_AUTO; }
void dm_pad_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    (void)lx; (void)ly; (void)rx; (void)ry; sent = b;
}
void dm_padmode_request(int pad) { (void)pad; }
void dm_padmode_request_glyph(int glyph) { (void)glyph; }
int dm_front_navigation(void) { return navigation; }
int dm_freetp_zr_free(int zl) { (void)zl; return empty_slot; }
void dm_tp_request(void) { portals++; }

int main(void) {
    @autoreleasepool {
        for (navigation = 0; navigation <= 1; navigation++) {
            for (empty_slot = 0; empty_slot <= 1; empty_slot++) {
                for (int alternate = 0; alternate <= 1; alternate++) {
                    uint64_t modifier = alternate ? DM_BTN_ZL : 0;
                    dm_gamepad_inject(0, 0, 0, 0, 0);
                    dm_gamepad_inject(modifier | DM_BTN_ZR, 0, 0, 0, 0);
                    assert(portals == 0);
                    assert(sent == (modifier | DM_BTN_ZR));
                    dm_gamepad_inject(modifier | DM_BTN_ZR, 0, 0, 0, 0);
                    assert(portals == 0 && sent == (modifier | DM_BTN_ZR));
                    dm_gamepad_inject(0, 0, 0, 0, 0);
                    assert(sent == 0);
                }
            }
        }
        dm_gamepad_inject_n(1, DM_BTN_ZR, 0, 0, 0, 0);
        dm_gamepad_inject_n(2, DM_BTN_A, 0, 0, 0, 0);
        assert(sent == (DM_BTN_ZR | DM_BTN_A) && portals == 0);
        dm_gamepad_drop_n(1);
        assert(sent == DM_BTN_A);
        dm_gamepad_reset();
        assert(sent == 0);
        dm_gamepad_drop_n(2);
        puts("gamepad portal: OK (binding states, modifiers, panels, held/released input, multiple controllers)");
    }
    return 0;
}
