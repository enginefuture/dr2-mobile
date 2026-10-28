// Run the actual Apple input path with synthetic GameController profiles.
#include <assert.h>
#include <stdio.h>
#include "../Sources/dm_gamepad.m"

@interface LayoutController : NSObject
@property(nonatomic, strong) NSString *productCategory;
@property(nonatomic, strong) GCExtendedGamepad *extendedGamepad;
@end
@implementation LayoutController
@end

static uint64_t sent;
static int selected, requested_glyph = -1;
void (*g_dm_pad_layout_hook)(void);
int dm_custom_get(const char *key) { assert(!strcmp(key, "pad_layout")); return selected; }
void dm_pad_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { (void)lx; (void)ly; (void)rx; (void)ry; sent = b; }
void dm_padmode_request(int pad) { (void)pad; }
void dm_padmode_request_glyph(int glyph) { requested_glyph = glyph; }

static GCController *controller(NSString *category) {
    LayoutController *c = [LayoutController new];
    c.productCategory = category;
    c.extendedGamepad = [GCController controllerWithExtendedGamepad].extendedGamepad;
    return (GCController *)(id)c;
}
static void faces(GCController *c, int button) {
    NSArray<GCControllerButtonInput *> *buttons = @[c.extendedGamepad.buttonA, c.extendedGamepad.buttonB, c.extendedGamepad.buttonX, c.extendedGamepad.buttonY];
    for (int k = 0; k < 4; k++) [buttons[k] setValue:k == button ? 1 : 0];
    publish(c, c.extendedGamepad);
}
int main(void) {
    @autoreleasepool {
        NSArray<GCController *> *pads = @[controller(@"DualSense"), controller(@"Xbox One"), controller(@"Switch Pro Controller"), controller(@"Generic")];
        const uint64_t direct[4] = { DM_BTN_A, DM_BTN_B, DM_BTN_X, DM_BTN_Y };
        const uint64_t xbox[4] = { DM_BTN_A, DM_BTN_B, DM_BTN_Y, DM_BTN_X };
        const uint64_t xbox_nintendo[4] = { DM_BTN_B, DM_BTN_A, DM_BTN_X, DM_BTN_Y };
        const uint64_t reverse[4] = { DM_BTN_B, DM_BTN_A, DM_BTN_Y, DM_BTN_X };
        for (selected = DM_PAD_LAYOUT_AUTO; selected < DM_PAD_LAYOUT_N; selected++) {
            for (int p = 0; p < 4; p++) {
                GCController *c = pads[p];
                int expected_glyph = selected == DM_PAD_LAYOUT_XBOX ? DM_GLYPH_XBOX : selected == DM_PAD_LAYOUT_PS ? DM_GLYPH_PS : selected == DM_PAD_LAYOUT_SWITCH ? DM_GLYPH_PAD : p == 0 ? DM_GLYPH_PS : p == 1 ? DM_GLYPH_XBOX : DM_GLYPH_PAD;
                const uint64_t *expect = expected_glyph == DM_GLYPH_XBOX ? (p == 2 ? xbox_nintendo : xbox) : ((selected == DM_PAD_LAYOUT_PS && p == 2) || (selected == DM_PAD_LAYOUT_SWITCH && p != 2)) ? reverse : direct;
                assert(glyph_of(c) == expected_glyph);
                for (int k = 0; k < 4; k++) {
                    faces(c, -1); faces(c, k);
                    assert(sent == expect[k]); assert(requested_glyph == expected_glyph);
                }
                faces(c, -1); drop((__bridge const void *)c);
            }
        }
        GCController *ps = pads[0];
        selected = DM_PAD_LAYOUT_PS; faces(ps, 2); assert(sent == DM_BTN_X);
        selected = DM_PAD_LAYOUT_XBOX; refresh_layout(@[ps]);
        assert(sent == 0 && requested_glyph == DM_GLYPH_XBOX); // old held key released, glyph changes while neutral
        publish(ps, ps.extendedGamepad); assert(sent == 0); // must release before the new mapping can act
        faces(ps, -1); faces(ps, 2); assert(sent == DM_BTN_Y);
        faces(ps, -1); drop((__bridge const void *)ps);
        faces(ps, 3); assert(sent == DM_BTN_X && requested_glyph == DM_GLYPH_XBOX); // reconnect retains override
        faces(ps, -1);
        [ps.extendedGamepad.rightTrigger setValue:1]; publish(ps, ps.extendedGamepad); assert(sent == DM_BTN_ZR);
        [ps.extendedGamepad.rightTrigger setValue:0]; publish(ps, ps.extendedGamepad);
        selected = DM_PAD_LAYOUT_AUTO; refresh_layout(@[ps]); assert(requested_glyph == DM_GLYPH_PS);
        selected = 99; assert(glyph_of(ps) == DM_GLYPH_PS);
        refresh_layout(@[]); // disconnect with no active controller
        puts("gamepad layout: OK (4 identities × 4 layouts × 4 face buttons, glyphs, held input, reconnect, trigger, Auto)");
    }
    return 0;
}
