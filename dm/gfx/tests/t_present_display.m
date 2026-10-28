// Exercise the production final-display shader, independently of interpolation:
// real pixels, raw crop, generated static HUD, and moving generated content.
#import "../nvn/nvn_present_fx.m"
#include <assert.h>
@interface TestDrawable : NSObject<CAMetalDrawable>
@property(strong) id<MTLTexture> texture;
@end
@implementation TestDrawable
- (CAMetalLayer *)layer{return nil;}
- (NSUInteger)drawableID{return 0;}
- (CFTimeInterval)presentedTime{return 0;}
- (void)present{}
- (void)presentAtTime:(CFTimeInterval)t{(void)t;}
- (void)presentAfterMinimumDuration:(CFTimeInterval)t{(void)t;}
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)h{(void)h;}
@end
static id<MTLTexture> image(id<MTLDevice> dev,MTLPixelFormat fmt,int w,int h,const void *bytes,int stride){
 MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
 id<MTLTexture> t=[dev newTextureWithDescriptor:d];if(bytes)[t replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:bytes bytesPerRow:stride];return t;
}
int main(void){@autoreleasepool{
 setenv("DM_FX_FRAMEGEN","1",1);setenv("DM_FX_UPSCALE","0",1);
 id<MTLDevice> dev=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> q=[dev newCommandQueue];
 uint8_t raw[32*32*4],pink[sizeof raw];uint16_t mv[32*32*2];
 for(int i=0;i<32*32;i++){raw[i*4]=118;raw[i*4+1]=188;raw[i*4+2]=231;raw[i*4+3]=255;pink[i*4]=255;pink[i*4+1]=0;pink[i*4+2]=255;pink[i*4+3]=255;}
 id<MTLTexture> src=image(dev,MTLPixelFormatRGBA8Unorm,32,32,raw,128),mid=image(dev,MTLPixelFormatRGBA8Unorm,32,32,pink,128);
 TestDrawable *draw=[TestDrawable new];draw.texture=image(dev,MTLPixelFormatBGRA8Unorm,32,32,NULL,0);int crop[4]={0,0,32,32};
 id<MTLCommandBuffer> warm=[q commandBuffer];dm_present_fx_encode(dev,warm,src,crop,0,1./30);[warm commit];[warm waitUntilCompleted];assert(!warm.error);
 for(int mode=0;mode<4;mode++){
  for(int i=0;i<32*32*2;i++)mv[i]=(mode>=2?0x3c00:0);fx.motion=image(dev,MTLPixelFormatRG16Float,32,32,mv,128);
  if(mode==3){for(int i=0;i<32*32;i++){pink[i*4]=125;pink[i*4+1]=188;pink[i*4+2]=231;pink[i*4+3]=255;}[mid replaceRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0 withBytes:pink bytesPerRow:128];}
  id<MTLCommandBuffer> cb=[q commandBuffer];encode_screen(cb,mode?mid:src,src,draw,crop,0,mode!=0);[cb commit];[cb waitUntilCompleted];assert(!cb.error);
  uint8_t pixels[sizeof raw];[draw.texture getBytes:pixels bytesPerRow:128 fromRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0];
  for(int y=0;y<32;y++)for(int x=0;x<32;x++){uint8_t *p=pixels+(y*32+x)*4;if(mode!=3||y>=28){assert(abs(p[0]-231)<=1&&abs(p[1]-188)<=1&&abs(p[2]-118)<=1);}else assert(abs(p[0]-231)<=1&&abs(p[1]-188)<=1&&abs(p[2]-125)<=1);}
 }
 puts("PRESENT_DISPLAY PASS: real pixels preserved; static HUD native; corrupt output rejected; valid moving pixels used");
}}
