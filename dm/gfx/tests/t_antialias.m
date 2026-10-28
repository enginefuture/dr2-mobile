#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <math.h>

int main(void){@autoreleasepool{
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> q=[dev newCommandQueue];NSError *error=nil;
    NSString *source=[NSString stringWithContentsOfFile:@"dm/gfx/nvn/nvn_present_fx.metal" encoding:NSUTF8StringEncoding error:&error];assert(source);
    id<MTLLibrary> lib=[dev newLibraryWithSource:source options:nil error:&error];if(!lib){fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
    id<MTLComputePipelineState> ps=[dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fx_antialias"] error:&error];assert(ps);
    enum{W=128,H=96};uint8_t in[W*H*4],out[W*H*4];
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    id<MTLTexture> a=[dev newTextureWithDescriptor:d],b=[dev newTextureWithDescriptor:d];assert(a&&b);
    double rawError=0,aaError=0;
    for(int scene=0;scene<6;scene++){
        double slope=(scene+1)*.137,offset=17.31;
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){uint8_t v=scene==5?118:(y+.5>slope*(x+.5)+offset?224:24);for(int c=0;c<3;c++)in[(y*W+x)*4+c]=v;in[(y*W+x)*4+3]=255;}
        [a replaceRegion:MTLRegionMake2D(0,0,W,H) mipmapLevel:0 withBytes:in bytesPerRow:W*4];
        id<MTLCommandBuffer> cb=[q commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];[e setComputePipelineState:ps];[e setTexture:a atIndex:0];[e setTexture:b atIndex:1];[e dispatchThreads:MTLSizeMake(W,H,1) threadsPerThreadgroup:MTLSizeMake(16,8,1)];[e endEncoding];[cb commit];[cb waitUntilCompleted];assert(!cb.error);
        [b getBytes:out bytesPerRow:W*4 fromRegion:MTLRegionMake2D(0,0,W,H) mipmapLevel:0];
        for(int y=3;y<H-3;y++)for(int x=3;x<W-3;x++){
            int index=(y*W+x)*4;assert(out[index+3]==255);
            if(scene==5){assert(out[index]==118&&out[index+1]==118&&out[index+2]==118);continue;}
            // Independent 32x32 area coverage of a continuous diagonal.
            if(fabs(y+.5-slope*(x+.5)-offset)>2)continue;
            int covered=0;for(int sy=0;sy<32;sy++)for(int sx=0;sx<32;sx++)covered+=y+(sy+.5)/32>slope*(x+(sx+.5)/32)+offset;
            double expected=24+200*covered/1024.;rawError+=fabs(in[index]-expected);aaError+=fabs(out[index]-expected);
        }
    }
    printf("diagonal coverage absolute error: raw %.3f -> AA %.3f; flat colour unchanged\n",rawError,aaError);assert(aaError<rawError*.9);
    puts("ANTIALIAS PASS (Metal API Validation)");return 0;
}}
