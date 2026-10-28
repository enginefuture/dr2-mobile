#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "nvn_present_fx.h"
static int complexScene;
static uint8_t *realImage;
static void darkOcclusionGuard(id<MTLDevice> dev,id<MTLCommandQueue> q){
    // A black interpolation hole is only 0.057 away from 20% sRGB gray in
    // linear RGB. The former linear threshold incorrectly accepted it.
    NSError *error=nil;NSString *source=[NSString stringWithContentsOfFile:@"dm/gfx/nvn/nvn_present_fx.metal" encoding:NSUTF8StringEncoding error:&error];assert(source);
    id<MTLLibrary> lib=[dev newLibraryWithSource:source options:nil error:&error];assert(lib);
    id<MTLComputePipelineState> ps=[dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fx_guard"] error:&error];assert(ps);
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:16 height:16 mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    NSMutableArray *textures=[NSMutableArray new];_Float16 pixels[16*16*4];
    for(int n=0;n<7;n++){
        id<MTLTexture> t=[dev newTextureWithDescriptor:d];assert(t);
        for(int i=0;i<16*16;i++){for(int c=0;c<4;c++)pixels[4*i+c]=n<2&&c<3?.0331048:0;if((n==3||n==4))pixels[4*i+2]=1;}
        [t replaceRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0 withBytes:pixels bytesPerRow:16*8];[textures addObject:t];
    }
    id<MTLCommandBuffer> cb=[q commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];[e setComputePipelineState:ps];for(int i=0;i<7;i++)[e setTexture:textures[i] atIndex:i];[e dispatchThreads:MTLSizeMake(16,16,1) threadsPerThreadgroup:MTLSizeMake(16,8,1)];[e endEncoding];[cb commit];[cb waitUntilCompleted];assert(!cb.error);
    [textures[6] getBytes:pixels bytesPerRow:16*8 fromRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0];
    for(int i=0;i<16*16;i++)for(int c=0;c<3;c++)assert(fabs((float)pixels[i*4+c]-.0331048)<.0001);
    puts("dark occlusion: corrupted black midpoint rejected, real 20% sRGB gray retained PASS");
}
static void tiles(int x,int y,int shift,uint8_t *p){
    uint32_t a=(uint32_t)((x-shift+4096)/32)*0x45d9f3bu^(uint32_t)(y/32)*0x119de1f3u;a^=a>>16;
    p[0]=(a&1)?255:0;p[1]=(a&2)?255:0;p[2]=(a&4)?255:0;p[3]=255;
    if(x>=8&&x<80&&y>=8&&y<80){p[0]=255;p[1]=p[2]=0;}
}
static void pattern(int x,int y,int shift,uint8_t *p){
    if(realImage){int sx=(x-shift+19200)%1920;if(x<420&&y<210)sx=x;memcpy(p,realImage+(y*1920+sx)*4,4);return;}
    if(!complexScene){tiles(x,y,shift,p);return;}
    double t=shift/32.;tiles(x+(int)lround(t*8),y,0,p);
    int center=650+(int)lround(72*t),leg=(int)lround(24*sin(t*1.7));
    if(x>center-65&&x<center+25&&y>535&&y<765){p[0]=35;p[1]=95;p[2]=205;}
    if(x>center-38&&x<center+38&&y>525&&y<675){p[0]=220;p[1]=120;p[2]=65;}
    if(((x>center-25+leg&&x<center-5+leg)||(x>center+5-leg&&x<center+25-leg))&&y>=675&&y<780){p[0]=150;p[1]=55;p[2]=195;}
    int pole=1100-(int)lround(8*t);if(x>pole&&x<pole+45&&y>330&&y<900){p[0]=30;p[1]=55;p[2]=40;}
    if(x>=8&&x<80&&y>=8&&y<80){p[0]=255;p[1]=p[2]=0;}
}
int main(int argc,char **argv){@autoreleasepool{
    int nativeOutput=argc>1 && !strcmp(argv[1],"native");
    int cold=0,mobile=0;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"mobile"))mobile=1;else if(!strcmp(argv[i],"complex"))complexScene=1;else if(!strcmp(argv[i],"cold"))cold=1;else if(!strcmp(argv[i],"real")&&i+1<argc){
        NSURL *url=[NSURL fileURLWithPath:@(argv[++i])];CGImageSourceRef src=CGImageSourceCreateWithURL((__bridge CFURLRef)url,NULL);assert(src);CGImageRef img=CGImageSourceCreateImageAtIndex(src,0,NULL);assert(img&&CGImageGetWidth(img)==1920&&CGImageGetHeight(img)==1080);
        realImage=malloc(1920*1080*4);CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);CGContextRef ctx=CGBitmapContextCreate(realImage,1920,1080,8,1920*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);assert(ctx);CGContextDrawImage(ctx,CGRectMake(0,0,1920,1080),img);CGContextRelease(ctx);CGColorSpaceRelease(cs);CGImageRelease(img);CFRelease(src);
    }else if(strcmp(argv[i],"native"))abort();}
    setenv("DM_FX_UPSCALE",nativeOutput?"0":"1",1);setenv("DM_FX_FRAMEGEN",cold?"0":"1",1);
    assert(!mobile||!realImage);if(nativeOutput)unsetenv("DM_FX_OUTPUT");else setenv("DM_FX_OUTPUT",mobile?"2560x1440":"3840x2160",1);
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> q=[dev newCommandQueue];
    const int w=mobile?1280:1920,h=mobile?720:1080,ow=(nativeOutput?1:2)*w,oh=(nativeOutput?1:2)*h;const double dt=mobile?1./30:1./60;MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
    id<MTLTexture> input=[dev newTextureWithDescriptor:d];uint8_t *data=malloc(w*h*4);id<MTLBuffer> out=[dev newBufferWithLength:ow*oh*4 options:MTLResourceStorageModeShared];double lastError=1,gpu=0;
    int frames=complexScene?8:6,firstPair=cold?3:2;
    for(int frame=0;frame<frames;frame++){
        if(cold&&frame==1)dm_present_fx_set_framegen(1);
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)pattern(x,y,frame*32,data+(y*w+x)*4);
        [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:data bytesPerRow:w*4];
        id<MTLCommandBuffer> cb=[q commandBuffer];int crop[4]={0,0,w,h};NSArray *images=dm_present_fx_encode(dev,cb,input,crop,0,dt);assert(images.count==(frame>=firstPair?2:1));id<MTLTexture> image=images[0];assert(image.width==ow&&image.height==oh);
        id<MTLBlitCommandEncoder> b=[cb blitCommandEncoder];[b copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(ow,oh,1) toBuffer:out destinationOffset:0 destinationBytesPerRow:ow*4 destinationBytesPerImage:ow*oh*4];[b endEncoding];[cb commit];[cb waitUntilCompleted];assert(!cb.error);gpu+=(cb.GPUEndTime-cb.GPUStartTime)*1000;
        if(frame>=firstPair){double error=0,endpoint=0,blend=0;uint64_t count=0;const uint8_t *pixels=out.contents;
            for(int y=100;y<h-100;y+=2)for(int x=192;x<w-192;x+=2){int u=x-(frame*32-16)+4096;if(!complexScene && (u%32<5||u%32>26||y%32<5||y%32>26))continue;uint8_t expected[4],current[4],previous[4];pattern(x,y,frame*32-16,expected);pattern(x,y,frame*32,current);pattern(x,y,(frame-1)*32,previous);const uint8_t *p=pixels+((y*(ow/w)+(ow/w)/2)*ow+x*(ow/w)+(ow/w)/2)*4;for(int c=0;c<3;c++){error+=fabs((double)p[c]-expected[c])/255;endpoint+=fabs((double)current[c]-expected[c])/255;blend+=fabs(((double)current[c]+previous[c])*.5-expected[c])/255;count++;}}
            lastError=error/count;printf("frame %d midpoint MAE %.6f; copied %.6f; blend %.6f\n",frame,lastError,endpoint/count,blend/count);
            if(realImage)assert(lastError<endpoint/count && lastError<blend/count && endpoint/count>.003);
            else if(complexScene)assert(lastError<endpoint/count && lastError<blend/count && endpoint/count>.02);
            else assert(lastError<.15&&endpoint/count>.15&&blend/count>.15);
            if(!realImage){const uint8_t *ui=pixels+(32*ow+32)*4;assert(ui[0]>245&&ui[1]<10&&ui[2]<10);}
        }
    }
    // A long capture gap resets interpolation. Non-binary sRGB tones detect
    // an accidental second gamma conversion that the moving pattern misses.
    for(int i=0;i<w*h;i++){data[4*i]=118;data[4*i+1]=188;data[4*i+2]=231;data[4*i+3]=255;}
    [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:data bytesPerRow:w*4];
    id<MTLCommandBuffer> reset=[q commandBuffer];int crop[4]={0,0,w,h};NSArray *fresh=dm_present_fx_encode(dev,reset,input,crop,0,.25);assert(fresh.count==1);
    id<MTLBlitCommandEncoder> bl=[reset blitCommandEncoder];[bl copyFromTexture:fresh[0] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(ow,oh,1) toBuffer:out destinationOffset:0 destinationBytesPerRow:ow*4 destinationBytesPerImage:ow*oh*4];[bl endEncoding];[reset commit];[reset waitUntilCompleted];assert(!reset.error);
    const uint8_t *tone=(const uint8_t*)out.contents+((oh/2)*ow+ow/2)*4;assert(abs(tone[0]-118)<=2&&abs(tone[1]-188)<=2&&abs(tone[2]-231)<=2);
    printf("sRGB tone %u,%u,%u; history reset PASS\n",tone[0],tone[1],tone[2]);
    dm_present_fx_set_framegen(0);
    id<MTLCommandBuffer> off=[q commandBuffer];assert(dm_present_fx_encode(dev,off,input,crop,0,dt).count==1);[off commit];[off waitUntilCompleted];assert(!off.error);
    dm_present_fx_set_framegen(1);
    for(int frame=0;frame<3;frame++){id<MTLCommandBuffer> on=[q commandBuffer];assert(dm_present_fx_encode(dev,on,input,crop,0,dt).count==(frame==2?2:1));[on commit];[on waitUntilCompleted];assert(!on.error);}
    puts("same-process OFF -> ON and warm-up suppression PASS");
    darkOcclusionGuard(dev,q);
    assert(lastError<.15);printf("PRESENT_FX PASS %dx%d -> %dx%d at %.0f real FPS; real midpoint, static HUD; GPU mean %.3f ms\n",w,h,ow,oh,1./dt,gpu/frames);
    free(realImage);free(data);return 0;
}}
