#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
int dm_present_fx_enabled(void);
int dm_framegen_active(void);
int dm_present_fx_framegen_requested(void);
void dm_present_fx_set_framegen(int on);
// All calls are serialized on the present thread. Textures stay private to
// this path; the guest never receives an interpolated or enlarged texture.
NSArray<id<MTLTexture>> *dm_present_fx_encode(id<MTLDevice>, id<MTLCommandBuffer>, id<MTLTexture>, const int crop[4], int flip, double dt);
int dm_present_fx_submit(id<MTLDevice>, id<MTL4CommandQueue>, id<MTLTexture>, CAMetalLayer *, const int crop[4], int flip, double period, uint64_t ticket, void (^done)(void), void (^realPresented)(double));
id<MTLTexture> dm_present_fx_debug_motion(void);
