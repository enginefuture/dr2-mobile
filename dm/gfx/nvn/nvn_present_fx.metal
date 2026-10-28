#include <metal_stdlib>
using namespace metal;
constexpr sampler linearSampler(coord::normalized, address::clamp_to_edge, filter::linear);
float3 toLinear(float3 x) { return select(x/12.92, pow((x+.055)/1.055,float3(2.4)), x>float3(.04045)); }
float3 toGamma(float3 x) { x=max(x,0.0);return select(x*12.92,1.055*pow(x,float3(1.0/2.4))-.055,x>float3(.0031308)); }
float colourDistance(half3 a,half3 b) { return length(toGamma(float3(a))-toGamma(float3(b))); }
kernel void fx_copy(texture2d<float,access::sample> source [[texture(0)]], texture2d<half,access::write> target [[texture(1)]], constant float4 &crop [[buffer(0)]], constant uint &flip [[buffer(1)]], uint2 p [[thread_position_in_grid]]) {
    if(p.x>=target.get_width()||p.y>=target.get_height())return;
    float2 uv=(float2(p)+.5)/float2(target.get_width(),target.get_height());if(flip)uv.y=1-uv.y;
    uv=(crop.xy+uv*crop.zw)/float2(source.get_width(),source.get_height());
    float4 c=source.sample(linearSampler,uv);target.write(half4(half3(toLinear(c.rgb)),1),p);
}
kernel void fx_gamma(texture2d<half,access::read> source [[texture(0)]],texture2d<half,access::write> target [[texture(1)]],uint2 p [[thread_position_in_grid]]) {
    if(p.x>=target.get_width()||p.y>=target.get_height())return;
    target.write(half4(half3(toGamma(float3(source.read(p).rgb))),1),p);
}
// Spatial scaling expects an antialiased input (Apple WWDC22/10103).
// Edge orientation and bounded endpoint search follow the FXAA method:
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
// Work in perceptual colour, leave flat regions untouched.
float fx_luminance(float3 c) { return dot(c,float3(.299,.587,.114)); }
kernel void fx_antialias(texture2d<float,access::sample> source [[texture(0)]],texture2d<float,access::write> target [[texture(1)]],uint2 p [[thread_position_in_grid]]) {
    float2 size=float2(target.get_width(),target.get_height());if(any(float2(p)>=size))return;
    float2 texel=1/size,uv=(float2(p)+.5)*texel;
    float4 center=source.sample(linearSampler,uv);float m=fx_luminance(center.rgb);
    float n=fx_luminance(source.sample(linearSampler,uv+float2(0,-texel.y)).rgb);
    float s=fx_luminance(source.sample(linearSampler,uv+float2(0,texel.y)).rgb);
    float e=fx_luminance(source.sample(linearSampler,uv+float2(texel.x,0)).rgb);
    float w=fx_luminance(source.sample(linearSampler,uv+float2(-texel.x,0)).rgb);
    float lo=min(m,min(min(n,s),min(e,w))),hi=max(m,max(max(n,s),max(e,w))),range=hi-lo;
    if(range<max(.0312,hi*.125)){target.write(center,p);return;}
    float nw=fx_luminance(source.sample(linearSampler,uv+texel*float2(-1,-1)).rgb);
    float ne=fx_luminance(source.sample(linearSampler,uv+texel*float2(1,-1)).rgb);
    float sw=fx_luminance(source.sample(linearSampler,uv+texel*float2(-1,1)).rgb);
    float se=fx_luminance(source.sample(linearSampler,uv+texel*float2(1,1)).rgb);
    float horizontal=abs(nw+sw-2*w)+2*abs(n+s-2*m)+abs(ne+se-2*e);
    float vertical=abs(nw+ne-2*n)+2*abs(w+e-2*m)+abs(sw+se-2*s);
    bool alongX=horizontal>=vertical;
    float a=alongX?n:w,b=alongX?s:e,ga=abs(a-m),gb=abs(b-m);
    float2 normal=alongX?float2(0,texel.y):float2(texel.x,0);
    if(ga>=gb)normal=-normal;
    float adjacent=ga>=gb?a:b,average=(adjacent+m)*.5,gradient=max(ga,gb)*.25;
    float2 tangent=alongX?float2(texel.x,0):float2(0,texel.y),edge=uv+normal*.5;
    float left=1,right=1,dl=0,dr=0;bool foundLeft=false,foundRight=false;
    for(int i=0;i<8;i++){
        if(!foundLeft){dl=fx_luminance(source.sample(linearSampler,edge-tangent*left).rgb)-average;foundLeft=abs(dl)>=gradient;if(!foundLeft)left+=1;}
        if(!foundRight){dr=fx_luminance(source.sample(linearSampler,edge+tangent*right).rgb)-average;foundRight=abs(dr)>=gradient;if(!foundRight)right+=1;}
        if(foundLeft&&foundRight)break;
    }
    float distance=min(left,right),delta=left<right?dl:dr;
    float edgeOffset=((delta<0)!=(m<average))?(.5-distance/(left+right)):0;
    float subpixel=clamp(abs((2*(n+s+e+w)+nw+ne+sw+se)/12-m)/range,0.f,1.f);
    subpixel=subpixel*subpixel*(3-2*subpixel);subpixel=subpixel*subpixel*.5;
    target.write(source.sample(linearSampler,uv+normal*max(edgeOffset,subpixel)),p);
}
kernel void fx_luma(texture2d<half,access::sample> source [[texture(0)]],texture2d<half,access::write> target [[texture(1)]],uint2 p [[thread_position_in_grid]]) {
    if(p.x>=target.get_width()||p.y>=target.get_height())return;
    float2 uv=(float2(p)+.5)/float2(target.get_width(),target.get_height());
    // Match perceptual contrast. Linear-light error hides highly visible errors
    // in this title's dark corridors and biases the search toward zero motion.
    target.write(half4(half(dot(toGamma(float3(source.sample(linearSampler,uv).rgb)),float3(.2126,.7152,.0722)))),p);
}
// Backward optical flow, coarse to fine. Each level measures in its own
// pixels; MetalFX receives pixels of the final motion texture, as documented.
kernel void fx_flow(texture2d<half,access::sample> previous [[texture(0)]],texture2d<half,access::sample> current [[texture(1)]],texture2d<half,access::sample> coarse [[texture(2)]],texture2d<half,access::write> output [[texture(3)]],constant uint &first [[buffer(0)]],uint2 p [[thread_position_in_grid]]) {
    uint2 size=uint2(output.get_width(),output.get_height());if(any(p>=size))return;
    float2 uv=(float2(p)+.5)/float2(size), step=1.0/float2(size);
    float2 base=first?float2(0):float2(coarse.sample(linearSampler,uv).xy)*float2(size)/float2(coarse.get_width(),coarse.get_height());
    base=round(base);float best=1e20;float2 result=0;int radius=first?4:2;
    for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++) {
        float2 v=base+float2(dx,dy);float error=0;
        for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
            float2 q=uv+float2(x,y)*step;
            float a=current.sample(linearSampler,q).r,b=previous.sample(linearSampler,q+v*step).r;
            error+=abs(a-b);
        }
        error+=.00002*dot(v,v);
        if(error<best){best=error;result=v;}
    }
    output.write(half4(half2(result),half(best/9),0),p);
}
// Reject local flow outliers without erasing the independently moving player.
kernel void fx_flow_filter(texture2d<half,access::read> source [[texture(0)]],texture2d<half,access::write> target [[texture(1)]],uint2 p [[thread_position_in_grid]]) {
    int2 size=int2(target.get_width(),target.get_height());if(any(int2(p)>=size))return;
    float a[9],b[9];int n=0;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){half2 v=source.read(uint2(clamp(int2(p)+int2(x,y),int2(0),size-1))).xy;a[n]=v.x;b[n++]=v.y;}
    for(int i=1;i<9;i++){float x=a[i],y=b[i];int j=i-1;while(j>=0&&a[j]>x){a[j+1]=a[j];j--;}a[j+1]=x;j=i-1;while(j>=0&&b[j]>y){b[j+1]=b[j];j--;}b[j+1]=y;}
    target.write(half4(a[4],b[4],0,0),p);
}
// Independently check forward/backward flow and the two reprojected colours.
// Newly exposed or ambiguously matched pixels retain the real current image.
// This deliberately sacrifices interpolation coverage at occlusion boundaries.
kernel void fx_guard(texture2d<half,access::sample> previous [[texture(0)]],texture2d<half,access::sample> current [[texture(1)]],texture2d<half,access::read> generated [[texture(2)]],texture2d<half,access::sample> backward [[texture(3)]],texture2d<half,access::sample> forward [[texture(4)]],texture2d<half,access::sample> motion [[texture(5)]],texture2d<half,access::write> output [[texture(6)]],uint2 p [[thread_position_in_grid]]) {
    float2 size=float2(output.get_width(),output.get_height());if(any(float2(p)>=size))return;
    float2 uv=(float2(p)+.5)/size,ms=float2(motion.get_width(),motion.get_height());
    float2 b=float2(motion.sample(linearSampler,uv).xy);
    half4 raw=backward.sample(linearSampler,uv),f=forward.sample(linearSampler,uv+b/ms);
    float consistency=length(b+float2(f.xy));
    half4 a=current.sample(linearSampler,uv-.5*b/ms),z=previous.sample(linearSampler,uv+.5*b/ms);
    float mismatch=colourDistance(a.rgb,z.rgb);
    bool valid=consistency<.75 && raw.z<.025 && f.z<.025 && mismatch<.08;
    half4 present=current.sample(linearSampler,uv),past=previous.sample(linearSampler,uv),mid=generated.read(p);
    // Low-texture regions can have ambiguous block matches even when MetalFX
    // reconstructs the right colour. Do not reject that valid reconstruction.
    float endpointError=min(colourDistance(mid.rgb,present.rgb),colourDistance(mid.rgb,past.rgb));
    if(endpointError<.04){output.write(mid,p);return;}
    if(!valid){output.write(present,p);return;}
    half4 expected=(a+z)*.5h;
    // A flow-consistent warp bounds the interpolator's refinement. It must not
    // create colours belonging to unrelated distant regions of the image.
    output.write(colourDistance(mid.rgb,expected.rgb)<.08?mid:expected,p);
}
struct ScreenVertex {float4 position [[position]];float2 uv;};
vertex ScreenVertex fx_vertex(uint n [[vertex_id]]) {
    float2 uv=float2((n<<1)&2,n&2);return {float4(uv*float2(2,-2)+float2(-1,1),0,1),uv};
}
fragment float4 fx_fragment(ScreenVertex in [[stage_in]],texture2d<float> image [[texture(0)]],texture2d<float> native [[texture(1)]],texture2d<float> motion [[texture(2)]],constant float4 &crop [[buffer(0)]],constant uint &flags [[buffer(1)]]) {
    float2 uv=in.uv,rawUV=uv;if(flags&4)rawUV.y=1-rawUV.y;
    float4 real=native.sample(linearSampler,crop.xy+rawUV*crop.zw);
    if(flags&1)return real;
    // Preserve native-resolution static UI on generated frames. Motion is an
    // image estimate: uncertain/occluded pixels were already rejected by guard.
    if((flags&2) && (uv.y>.86 || length(motion.sample(linearSampler,uv).xy)<.05))return real;
    float4 generated=image.sample(linearSampler,uv);
    // The low-resolution guard cannot see errors introduced by upscaling or
    // stale/mixed static-flow tiles. Bound final visible colour error against
    // the full-resolution real image; uncertain edges retain native pixels.
    if((flags&2) && length(generated.rgb-real.rgb)>.06)return real;
    return generated;
}
