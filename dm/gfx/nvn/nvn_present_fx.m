// Presentation-only 2D optical flow + native MetalFX interpolation and scaling.
// Adapted from ns2mac Castlevania present effects (2026-10-25).
// Planar depth and image optical flow are approximations for D2R; no native depth/motion/UI split.
// No game time, input polling or guest frame counter is advanced by this path.
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#include <stdatomic.h>
#include <math.h>
#include <TargetConditionals.h>
#include <pthread.h>
#include "nvn_present_fx.h"
#include "nvn_present_fx_shaders.h"
#define FXLOG(...) do { fprintf(stderr,"[dm] [present-fx] " __VA_ARGS__);fputc('\n',stderr); } while(0)
static struct {
    id<MTLDevice> dev; id<MTLCommandQueue> queue; id<MTLSharedEvent> ready;
    id<MTLLibrary> library; id<MTLComputePipelineState> copy,gamma,luma,flow,filter,guard,antialias;
    id<MTLRenderPipelineState> present;
    id<MTLFXFrameInterpolator> fi; id<MTLFXSpatialScaler> scaler;
    id<MTLTexture> color[2], mid, checked, depth, motion, perceptual[2], antialiased[2], output[2];
    id<MTLTexture> pyramid[2][4], vectors[2][4];
    NSUInteger w,h,ow,oh; int index,history; double previous;
    uint64_t encoded, generated; _Atomic uint64_t displayed_real,displayed_generated;
    _Atomic uint64_t gpu_ns; _Atomic uint64_t gpu_samples;
    dispatch_semaphore_t slots;
} fx;
static _Atomic int upscale=-1,failed;static int antialias;
static int env_framegen,env_upscale,supported,spatial_supported,flow_divisor;
static _Atomic int pending_upscale=-1;
__attribute__((weak)) int dm_framegen_setting(void){return 0;}
__attribute__((weak)) int dm_upscale_setting(void){return 0;}
__attribute__((weak)) int dm_fx_scene_tag(void){return 1;}
static double display_intervals[240];static unsigned display_n;static uint64_t display_previous;
static pthread_mutex_t display_mu=PTHREAD_MUTEX_INITIALIZER;
static double fx_target;
static int output_width,output_height,output_explicit,work_height;
static _Atomic int interpolate=-1,pending_interpolation=-1;
__attribute__((weak)) void front_fx_stats(double a,double b,double c){(void)a;(void)b;(void)c;}
int dm_present_fx_enabled(void) {
    static dispatch_once_t once;
    dispatch_once(&once,^{
        const char *u=getenv("DM_FX_UPSCALE"),*f=getenv("DM_FX_FRAMEGEN"),*sz=getenv("DM_FX_OUTPUT"),*a=getenv("DM_FX_AA");
        env_framegen=f!=NULL;env_upscale=u!=NULL;
        id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
        if(@available(macOS 26.0,iOS 26.0,*)){supported=[MTLFXFrameInterpolatorDescriptor supportsDevice:dev];}
        spatial_supported=[MTLFXSpatialScalerDescriptor supportsDevice:dev];
#if DM_NVN_M3
        supported=spatial_supported=0;
#endif
        upscale=(u?atoi(u):dm_upscale_setting()) && spatial_supported;
        interpolate=(f?atoi(f):dm_framegen_setting()) && supported;
        flow_divisor=4;
        const char *wh=getenv("DM_FX_WORK_HEIGHT");work_height=wh?atoi(wh):720;if(work_height<360||work_height>1080)work_height=720;
        antialias=a&&atoi(a);
        int w,h;if(sz&&sscanf(sz,"%dx%d",&w,&h)==2&&w>=320&&h>=180&&w<=3840&&h<=2160){output_width=w;output_height=h;output_explicit=1;}
        FXLOG("capabilities framegen=%d spatial=%d; requested native30/generated60=%d upscale=%d",supported,spatial_supported,interpolate,upscale);
    });
    if(!env_framegen){int wanted=dm_framegen_setting() && supported;if(wanted!=atomic_load(&interpolate))atomic_store(&pending_interpolation,wanted);}
    if(!env_upscale){int wanted=dm_upscale_setting() && spatial_supported;if(wanted!=upscale)atomic_store(&pending_upscale,wanted);}
    return !failed && (upscale||interpolate||antialias||atomic_load(&pending_interpolation)==1||atomic_load(&pending_upscale)==1);
}
int dm_present_fx_framegen_requested(void){dm_present_fx_enabled();int p=atomic_load(&pending_interpolation);return supported && !failed && (p<0?atomic_load(&interpolate):p);}
int dm_framegen_active(void){return dm_present_fx_framegen_requested();}
int mtl_framegen_real_fps(void){return dm_present_fx_framegen_requested()?30:0;}
void dm_present_fx_set_framegen(int on){atomic_store(&pending_interpolation,on!=0);FXLOG("frame generation requested %s",on?"on":"off");}
static void apply_request(void){
    int scaling=atomic_exchange(&pending_upscale,-1);
    if(scaling>=0 && scaling!=upscale){upscale=scaling;fx.w=0;fx.history=0;fx_target=0;}
    int requested=atomic_exchange(&pending_interpolation,-1);
    if(requested>=0 && requested!=interpolate){interpolate=requested&&supported;fx.history=0;fx_target=0;if(requested&&!fx.fi)fx.w=0;FXLOG("frame generation now %s; history reset",requested?"on":"off");}
}
static id<MTLTexture> texture(MTLPixelFormat format,NSUInteger w,NSUInteger h,MTLTextureUsage usage) {
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=usage;
    return [fx.dev newTextureWithDescriptor:d];
}
static void compute(id<MTLCommandBuffer> cb,id<MTLComputePipelineState> ps,NSArray<id<MTLTexture>> *textures,NSUInteger w,NSUInteger h,const void *p,NSUInteger bytes,const void *p1,NSUInteger bytes1) {
    id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];[e setComputePipelineState:ps];
    for(NSUInteger n=0;n<textures.count;n++)[e setTexture:textures[n] atIndex:n];
    if(bytes)[e setBytes:p length:bytes atIndex:0];if(bytes1)[e setBytes:p1 length:bytes1 atIndex:1];
    [e dispatchThreads:MTLSizeMake(w,h,1) threadsPerThreadgroup:MTLSizeMake(16,8,1)];[e endEncoding];
}
static void input_size(NSUInteger *w,NSUInteger *h) {
    if((upscale || interpolate) && !output_explicit){
        output_width=(int)*w;output_height=(int)*h;
        if(*h>(NSUInteger)work_height){*w=(NSUInteger)llround((double)*w*work_height/(*h));*h=work_height;}
    }
}
static int initialize(id<MTLDevice> dev,NSUInteger w,NSUInteger h) {
    if(fx.w==w&&fx.h==h&&fx.ow==((upscale||interpolate)&&output_width?(NSUInteger)output_width:w)&&fx.oh==((upscale||interpolate)&&output_height?(NSUInteger)output_height:h))return 1;
    if(fx.queue){id<MTLCommandBuffer> drain=[fx.queue commandBuffer];[drain commit];[drain waitUntilCompleted];}
    fx.dev=dev;fx.w=w;fx.h=h;fx.ow=(upscale||interpolate)&&output_width?output_width:w;fx.oh=(upscale||interpolate)&&output_height?output_height:h;fx.history=0;fx.index=0;fx.previous=0;
    if(!fx.queue){fx.queue=[dev newCommandQueue];fx.ready=[dev newSharedEvent];fx.slots=dispatch_semaphore_create(2);}
    NSError *error=nil;
    if(!fx.library){
        fx.library=[dev newLibraryWithSource:@(dm_present_fx_msl) options:nil error:&error];
        if(!fx.library){FXLOG("[RED] utility shaders: %s",error.description.UTF8String);return 0;}
        NSString *names[] = {@"fx_copy",@"fx_gamma",@"fx_luma",@"fx_flow",@"fx_flow_filter",@"fx_guard"};
        id<MTLComputePipelineState> states[6];
        for(int i=0;i<6;i++){states[i]=[dev newComputePipelineStateWithFunction:[fx.library newFunctionWithName:names[i]] error:&error];if(!states[i]){FXLOG("[RED] compute pipeline: %s",error.description.UTF8String);return 0;}}
        fx.copy=states[0];fx.gamma=states[1];fx.luma=states[2];fx.flow=states[3];fx.filter=states[4];fx.guard=states[5];
        fx.antialias=[dev newComputePipelineStateWithFunction:[fx.library newFunctionWithName:@"fx_antialias"] error:&error];if(!fx.antialias){FXLOG("[RED] antialias pipeline: %s",error.description.UTF8String);return 0;}
        MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];p.vertexFunction=[fx.library newFunctionWithName:@"fx_vertex"];p.fragmentFunction=[fx.library newFunctionWithName:@"fx_fragment"];p.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        fx.present=[dev newRenderPipelineStateWithDescriptor:p error:&error];if(!fx.present){FXLOG("[RED] present pipeline: %s",error.description.UTF8String);return 0;}
    }
    const MTLTextureUsage rw=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget;
    for(int i=0;i<2;i++){fx.color[i]=texture(MTLPixelFormatRGBA16Float,w,h,rw);fx.perceptual[i]=texture(MTLPixelFormatRGBA8Unorm,w,h,rw);fx.output[i]=texture(MTLPixelFormatRGBA8Unorm,fx.ow,fx.oh,rw);}
    for(int i=0;i<2;i++)fx.antialiased[i]=antialias?texture(MTLPixelFormatRGBA8Unorm,w,h,rw):nil;
    fx.mid=texture(MTLPixelFormatRGBA16Float,w,h,rw);fx.checked=texture(MTLPixelFormatRGBA16Float,w,h,rw);
    if(upscale || interpolate){
        if(![MTLFXSpatialScalerDescriptor supportsDevice:dev]){FXLOG("[RED] MetalFX spatial scaling unsupported");return 0;}
        MTLFXSpatialScalerDescriptor *d=[MTLFXSpatialScalerDescriptor new];d.inputWidth=w;d.inputHeight=h;d.outputWidth=fx.ow;d.outputHeight=fx.oh;
        d.colorTextureFormat=d.outputTextureFormat=MTLPixelFormatRGBA8Unorm;d.colorProcessingMode=MTLFXSpatialScalerColorProcessingModePerceptual;
        fx.scaler=[d newSpatialScalerWithDevice:dev];if(!fx.scaler){FXLOG("[RED] MetalFX spatial scaler creation failed");return 0;}
    }
    if(interpolate){
        if(![MTLFXFrameInterpolatorDescriptor supportsDevice:dev]){FXLOG("[RED] MetalFX interpolation unsupported; keeping spatial scaling");interpolate=0;}
        else {
            NSUInteger mw=MAX(32,(w+flow_divisor-1)/flow_divisor),mh=MAX(18,(h+flow_divisor-1)/flow_divisor);
            MTLFXFrameInterpolatorDescriptor *d=[MTLFXFrameInterpolatorDescriptor new];d.inputWidth=mw;d.inputHeight=mh;d.outputWidth=w;d.outputHeight=h;
            d.colorTextureFormat=d.outputTextureFormat=MTLPixelFormatRGBA16Float;d.depthTextureFormat=MTLPixelFormatDepth32Float;d.motionTextureFormat=MTLPixelFormatRG16Float;d.uiTextureFormat=MTLPixelFormatRGBA8Unorm;
            fx.fi=[d newFrameInterpolatorWithDevice:dev];if(!fx.fi){FXLOG("[RED] MetalFX interpolator creation failed");return 0;}
            fx.motion=texture(MTLPixelFormatRG16Float,mw,mh,rw);fx.depth=texture(MTLPixelFormatDepth32Float,mw,mh,fx.fi.depthTextureUsage|MTLTextureUsageRenderTarget);
            for(int level=0;level<4;level++){
                NSUInteger lw=MAX(1,mw>>level),lh=MAX(1,mh>>level);
                for(int i=0;i<2;i++)fx.pyramid[i][level]=texture(MTLPixelFormatR16Float,lw,lh,rw);
                for(int i=0;i<2;i++)fx.vectors[i][level]=texture(MTLPixelFormatRGBA16Float,lw,lh,rw);
            }
        }
    }
    FXLOG("work input %lux%lu -> output %lux%lu; real upscale=%d, interpolation=%d; image optical flow, planar depth approximation",(unsigned long)w,(unsigned long)h,(unsigned long)fx.ow,(unsigned long)fx.oh,upscale,interpolate);
    FXLOG("framegen backend=%s; flow field divisor=%d","MetalFX native",flow_divisor);
    FXLOG("perceptual edge antialiasing before scaling=%d",antialias);
    return fx.color[0]&&fx.color[1]&&fx.output[0]&&fx.output[1];
}
static NSArray<id<MTLTexture>> *encode_prepared(id<MTLCommandBuffer> cb,id<MTLTexture> source,const int crop[4],int flip,double dt) {
    int x=crop?crop[0]:0,y=crop?crop[1]:0,w=crop&&crop[2]?crop[2]:(int)source.width,h=crop&&crop[3]?crop[3]:(int)source.height;
    if(x<0||y<0||w<=0||h<=0||x+w>source.width||y+h>source.height){FXLOG("[RED] invalid source crop");return nil;}
    fx.index^=1;int current=fx.index,previous=current^1;
    float region[4]={x,y,w,h};w=(int)fx.w;h=(int)fx.h;uint32_t inverted=flip!=0;
    compute(cb,fx.copy,@[source,fx.color[current]],w,h,region,sizeof region,&inverted,sizeof inverted);
    static int scene0=-1;static int crop0[5];int scene=dm_fx_scene_tag(),geom[5]={x,y,w,h,flip};
    BOOL reset=!fx.history || !isfinite(dt) || dt<=0 || dt>=.1 || scene!=scene0 || memcmp(crop0,geom,sizeof geom);
    scene0=scene;memcpy(crop0,geom,sizeof geom);
    if(reset)fx.history=0;
    // MetalFX's first history pair is warm-up only on the measured M3 Max.
    BOOL pair=interpolate && fx.history>=2;
    if(interpolate){
        for(int level=0;level<4;level++)compute(cb,fx.luma,@[fx.color[current],fx.pyramid[current][level]],fx.pyramid[current][level].width,fx.pyramid[current][level].height,NULL,0,NULL,0);
        for(int direction=0;direction<2;direction++)for(int level=3;level>=0;level--){
            uint32_t first=level==3;id<MTLTexture> coarse=first?fx.vectors[direction][level]:fx.vectors[direction][level+1];
            int a=fx.history?(direction?current:previous):current,b=direction&&fx.history?previous:current;
            compute(cb,fx.flow,@[fx.pyramid[a][level],fx.pyramid[b][level],coarse,fx.vectors[direction][level]],fx.vectors[direction][level].width,fx.vectors[direction][level].height,&first,sizeof first,NULL,0);
        }
        compute(cb,fx.filter,@[fx.vectors[0][0],fx.motion],fx.motion.width,fx.motion.height,NULL,0,NULL,0);
        MTLRenderPassDescriptor *depth=[MTLRenderPassDescriptor renderPassDescriptor];depth.depthAttachment.texture=fx.depth;depth.depthAttachment.loadAction=MTLLoadActionClear;depth.depthAttachment.storeAction=MTLStoreActionStore;depth.depthAttachment.clearDepth=.5;
        [[cb renderCommandEncoderWithDescriptor:depth] endEncoding];
        fx.fi.colorTexture=fx.color[current];fx.fi.prevColorTexture=fx.history?fx.color[previous]:fx.color[current];fx.fi.depthTexture=fx.depth;fx.fi.motionTexture=fx.motion;fx.fi.outputTexture=fx.mid;fx.fi.uiTexture=nil;
        fx.fi.uiTextureComposited=NO;fx.fi.motionVectorScaleX=fx.fi.motionVectorScaleY=1;fx.fi.deltaTime=dt>0&&dt<.1?dt:1.f/60;
        fx.fi.nearPlane=.1;fx.fi.farPlane=100;fx.fi.fieldOfView=60;fx.fi.aspectRatio=(float)w/h;fx.fi.depthReversed=NO;fx.fi.jitterOffsetX=fx.fi.jitterOffsetY=0;fx.fi.shouldResetHistory=reset;
        [fx.fi encodeToCommandBuffer:cb];
        if(pair)compute(cb,fx.guard,@[fx.color[previous],fx.color[current],fx.mid,fx.vectors[0][0],fx.vectors[1][0],fx.motion,fx.checked],w,h,NULL,0,NULL,0);
    }
    NSMutableArray<id<MTLTexture>> *out=[NSMutableArray new];
    for(int image=pair?0:1;image<2;image++){
        if(image==1 && !upscale){[out addObject:source];continue;} // Real frames retain native pixels and HUD.
        compute(cb,fx.gamma,@[image==0?fx.checked:fx.color[current],fx.perceptual[image]],w,h,NULL,0,NULL,0);
        id<MTLTexture> prepared=fx.perceptual[image];
        if(antialias){compute(cb,fx.antialias,@[prepared,fx.antialiased[image]],w,h,NULL,0,NULL,0);prepared=fx.antialiased[image];}
        if(upscale || interpolate){fx.scaler.colorTexture=prepared;fx.scaler.outputTexture=fx.output[image];fx.scaler.inputContentWidth=w;fx.scaler.inputContentHeight=h;[fx.scaler encodeToCommandBuffer:cb];[out addObject:fx.output[image]];}
        else [out addObject:prepared];
    }
    if(fx.history<3)fx.history++;fx.encoded++;fx.generated+=pair;
    return out;
}
NSArray<id<MTLTexture>> *dm_present_fx_encode(id<MTLDevice> dev,id<MTLCommandBuffer> cb,id<MTLTexture> source,const int crop[4],int flip,double dt) {
    if(!dm_present_fx_enabled())return nil;
    apply_request();
    NSUInteger w=crop&&crop[2]?crop[2]:source.width,h=crop&&crop[3]?crop[3]:source.height;
    input_size(&w,&h);
    if(!initialize(dev,w,h)){failed=1;return nil;}
    return encode_prepared(cb,source,crop,flip,dt);
}
id<MTLTexture> dm_present_fx_debug_motion(void){return fx.motion;}
static void encode_screen(id<MTLCommandBuffer> cb,id<MTLTexture> image,id<MTLTexture> source,id<CAMetalDrawable> drawable,const int crop[4],int flip,BOOL generated) {
    MTLRenderPassDescriptor *d=[MTLRenderPassDescriptor renderPassDescriptor];d.colorAttachments[0].texture=drawable.texture;d.colorAttachments[0].loadAction=MTLLoadActionClear;d.colorAttachments[0].storeAction=MTLStoreActionStore;d.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
    id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:d];[e setRenderPipelineState:fx.present];[e setFragmentTexture:image atIndex:0];
    float rect[4]={0,0,1,1};if(crop && crop[2]>0 && crop[3]>0){rect[0]=(float)crop[0]/source.width;rect[1]=(float)crop[1]/source.height;rect[2]=(float)crop[2]/source.width;rect[3]=(float)crop[3]/source.height;}
    uint32_t flags=(image==source?1:0)|(generated?2:0)|(flip?4:0);
    [e setFragmentTexture:source atIndex:1];[e setFragmentTexture:fx.motion?:source atIndex:2];
    [e setFragmentBytes:rect length:sizeof rect atIndex:0];[e setFragmentBytes:&flags length:sizeof flags atIndex:1];
    double iw=fx.ow,ih=fx.oh,scale=fmin((double)drawable.texture.width/iw,(double)drawable.texture.height/ih);
    [e setViewport:(MTLViewport){(drawable.texture.width-iw*scale)*.5,(drawable.texture.height-ih*scale)*.5,iw*scale,ih*scale,0,1}];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
}
int dm_present_fx_submit(id<MTLDevice> dev,id<MTL4CommandQueue> producer,id<MTLTexture> source,CAMetalLayer *layer,const int crop[4],int flip,double period,uint64_t ticket,void (^done)(void),void (^realPresented)(double)) {
    if(!dm_present_fx_enabled())return 0;
    apply_request();
    NSUInteger w=crop&&crop[2]?crop[2]:source.width,h=crop&&crop[3]?crop[3]:source.height;
    input_size(&w,&h);
    if(!initialize(dev,w,h)){failed=1;return 0;}
    dispatch_semaphore_wait(fx.slots,DISPATCH_TIME_FOREVER);
    // A separate shared event orders the Metal 4 producer before the Metal 3
    // effects queue. Never reuse the game's global fence sequence for this.
    [producer signalEvent:fx.ready value:ticket];
    id<MTLCommandBuffer> process=[fx.queue commandBuffer];[process encodeWaitForEvent:fx.ready value:ticket];
    double now=CACurrentMediaTime(),dt=fx.previous?now-fx.previous:period;fx.previous=now;
    // Latch requests only before initialize(), before this queue has an
    // uncommitted command buffer. A UI toggle arriving now waits one frame;
    // reinitializing here could drain behind our own uncommitted buffer.
    NSArray *images=encode_prepared(process,source,crop,flip,dt);
    if(!images){[process addCompletedHandler:^(id<MTLCommandBuffer> c){(void)c;dispatch_semaphore_signal(fx.slots);done();}];[process commit];return 1;}
    [process addCompletedHandler:^(id<MTLCommandBuffer> c){if(c.error)FXLOG("[RED] effects command buffer: %s",c.error.description.UTF8String);else{atomic_fetch_add(&fx.gpu_ns,(uint64_t)((c.GPUEndTime-c.GPUStartTime)*1e9));atomic_fetch_add(&fx.gpu_samples,1);}}];
    [process commit];
    double step=period>0?period:1.0/60, spacing=images.count==2?step*.5:step;
    double clock=CACurrentMediaTime();
    double base=fx_target>0 && fabs(clock-fx_target)<2*step?fx_target:clock;
    for(NSUInteger i=0;i<images.count;i++){
        id<CAMetalDrawable> d=[layer nextDrawable];
        if(!d){FXLOG("[RED] drawable unavailable; dropping this output pair");id<MTLCommandBuffer> release=[fx.queue commandBuffer];[release addCompletedHandler:^(id<MTLCommandBuffer> c){(void)c;dispatch_semaphore_signal(fx.slots);done();}];[release commit];return 1;}
        id<MTLCommandBuffer> display=[fx.queue commandBuffer];encode_screen(display,images[i],source,d,crop,flip,images.count==2&&i==0);
        BOOL generated=images.count==2&&i==0,last=i+1==images.count;double time=base+(i+1)*spacing;
        [d addPresentedHandler:^(id<MTLDrawable> draw){double t=draw.presentedTime;if(t>0){pthread_mutex_lock(&display_mu);uint64_t ns=(uint64_t)(t*1e9);if(display_previous && ns>display_previous)display_intervals[display_n++%240]=(ns-display_previous)/1e6;display_previous=ns;pthread_mutex_unlock(&display_mu);if(generated)atomic_fetch_add(&fx.displayed_generated,1);else{atomic_fetch_add(&fx.displayed_real,1);realPresented(t);}}}];
        [display presentDrawable:d atTime:time];
        if(last)[display addCompletedHandler:^(id<MTLCommandBuffer> c){if(c.error)FXLOG("[RED] presentation command buffer: %s",c.error.description.UTF8String);dispatch_semaphore_signal(fx.slots);done();}];
        [display commit];fx_target=time;
    }
    if(fx.encoded%120==0){
        static double last;static uint64_t real0,generated0,frames0;
        uint64_t samples=atomic_load(&fx.gpu_samples),real=atomic_load(&fx.displayed_real),generated=atomic_load(&fx.displayed_generated);
        double t=CACurrentMediaTime(),elapsed=last?t-last:0,gameFPS=elapsed>0?(fx.encoded-frames0)/elapsed:0,realFPS=elapsed>0?(real-real0)/elapsed:0,generatedFPS=elapsed>0?(generated-generated0)/elapsed:0;
        FXLOG("game frames=%llu, encoded middle frames=%llu; displayed real=%llu generated=%llu; game %.1f FPS, displayed %.1f real + %.1f generated; effects GPU mean %.3f ms",(unsigned long long)fx.encoded,(unsigned long long)fx.generated,(unsigned long long)real,(unsigned long long)generated,gameFPS,realFPS,generatedFPS,samples?atomic_load(&fx.gpu_ns)/1e6/samples:0);
        pthread_mutex_lock(&display_mu);unsigned n=MIN(display_n,240);double iv[240];memcpy(iv,display_intervals,n*sizeof(double));pthread_mutex_unlock(&display_mu);
        for(unsigned i=1;i<n;i++){double x=iv[i];unsigned j=i;while(j&&iv[j-1]>x){iv[j]=iv[j-1];j--;}iv[j]=x;}
        if(n)FXLOG("actual display interval p50 %.2f / p95 %.2f ms; native pixels %lux%lu, final %lux%lu",iv[n/2],iv[(n-1)*95/100],(unsigned long)w,(unsigned long)h,(unsigned long)fx.ow,(unsigned long)fx.oh);
        if(last){void front_fx_stats(double,double,double);front_fx_stats(gameFPS,realFPS,generatedFPS);}last=t;real0=real;generated0=generated;frames0=fx.encoded;
    }
    return 1;
}
