#pragma once
// =============================================================
//  Procedural PBR texture / material generation (CPU side).
//  High-resolution (512px) tileable maps generated with layered
//  value-noise / fBm, worley cells, and feature masks. Every
//  surface type is visually DISTINCT (albedo + roughness + AO +
//  metallic separation) and gets a derived normal map so the
//  geometry reads with real surface relief.
//
//  Materials:
//    makeWallpaper()  - classic mono-yellow damp Backrooms wall
//    makeBaseboard()  - darker scuffed skirting strip
//    makeCarpet()     - short office carpet, distinct from walls
//    makeCeiling()    - acoustic drop-ceiling tiles w/ grid + sag
//    makeConcrete()   - bare service-area concrete
//    makeTileFloor()  - wet linoleum / tile (maintenance areas)
//    makeFlat()       - solid PBR color (props)
//    makeSign(text..) - emissive/printed sign & warning decals
//  Output: Material { albedo, normal, mrt } where mrt =
//    R: roughness   G: ambient-occlusion   B: metallic
// =============================================================
#include <glad/gl.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>

namespace tex {

static inline float clampf(float x,float a,float b){ return x<a?a:(x>b?b:x); }
static inline float smoothstep(float e0,float e1,float x){
    float t=(x-e0)/(e1-e0); t=clampf(t,0.f,1.f); return t*t*(3.f-2.f*t);
}
static inline float lerp(float a,float b,float t){ return a+(b-a)*t; }

// ---------------- value noise + fBm (tileable) ----------------
static float hash2(int x,int y){
    int n=x*374761393 + y*668265263;
    n=(n^(n>>13))*1274126177;
    return ((n^(n>>16))&0x7fffffff)/2147483647.0f;
}
static float vnoise(float x,float y,int period){
    int xi=(int)floorf(x), yi=(int)floorf(y);
    float xf=x-xi, yf=y-yi;
    float u=xf*xf*(3-2*xf), v=yf*yf*(3-2*yf);
    auto h=[&](int a,int b){ return hash2((a%period+period)%period,(b%period+period)%period); };
    float a=h(xi,yi), b=h(xi+1,yi), c=h(xi,yi+1), d=h(xi+1,yi+1);
    return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v;
}
static float fbmT(float x,float y,int oct,int period){
    float val=0,amp=0.5f,f=1.0f; int p=period;
    for(int i=0;i<oct;i++){ val+=amp*vnoise(x*f,y*f,p); f*=2; amp*=0.5f; p*=2; }
    return val;
}
// worley/cellular distance (tileable-ish) for tiles & blotches
static float worley(float x,float y,int period){
    int xi=(int)floorf(x), yi=(int)floorf(y);
    float best=9e9f;
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
        int cx=xi+dx, cy=yi+dy;
        float fx=cx + hash2((cx%period+period)%period, (cy%period+period)%period);
        float fy=cy + hash2((cy%period+period)%period+99, (cx%period+period)%period+7);
        float d=(fx-x)*(fx-x)+(fy-y)*(fy-y);
        if(d<best) best=d;
    }
    return sqrtf(best);
}

struct Image { int w,h; std::vector<uint8_t> px; }; // RGB

// build a normal map from a heightfield (wrapping)
static Image normalFromHeight(const std::vector<float>& hgt,int w,int hh,float strength){
    Image img; img.w=w; img.h=hh; img.px.resize((size_t)w*hh*3);
    for(int y=0;y<hh;y++) for(int x=0;x<w;x++){
        int xl=(x-1+w)%w, xr=(x+1)%w, yu=(y-1+hh)%hh, yd=(y+1)%hh;
        float dx=(hgt[y*w+xl]-hgt[y*w+xr])*strength;
        float dy=(hgt[yu*w+x]-hgt[yd*w+x])*strength;
        float nz=1.0f;
        float len=sqrtf(dx*dx+dy*dy+nz*nz);
        int i=(y*w+x)*3;
        img.px[i+0]=(uint8_t)((dx/len*0.5f+0.5f)*255);
        img.px[i+1]=(uint8_t)((dy/len*0.5f+0.5f)*255);
        img.px[i+2]=(uint8_t)((nz/len*0.5f+0.5f)*255);
    }
    return img;
}

static GLuint upload(const Image& img,bool srgb=false){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0, srgb?GL_SRGB:GL_RGB, img.w,img.h,0,GL_RGB,GL_UNSIGNED_BYTE,img.px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    GLfloat aniso=8.0f; glTexParameterf(GL_TEXTURE_2D,0x84FE,aniso); // GL_TEXTURE_MAX_ANISOTROPY
    return t;
}

// RGBA image (for decals with transparency, e.g. handwriting)
struct ImageA { int w,h; std::vector<uint8_t> px; }; // RGBA
static GLuint uploadA(const ImageA& img,bool srgb=true){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0, srgb?GL_SRGB_ALPHA:GL_RGBA, img.w,img.h,0,GL_RGBA,GL_UNSIGNED_BYTE,img.px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t;
}

struct Material { GLuint albedo=0, normal=0, mrt=0; bool decal=false; };

static inline void setPix(Image& im,int i,float r,float g,float b){
    im.px[i+0]=(uint8_t)(clampf(r,0,1)*255);
    im.px[i+1]=(uint8_t)(clampf(g,0,1)*255);
    im.px[i+2]=(uint8_t)(clampf(b,0,1)*255);
}

// =============================================================
//  BACKROOMS YELLOW WALLPAPER  (the iconic surface)
// =============================================================
static Material makeWallpaper(){
    const int S=1024;                               // higher-res for crisp detail
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    const float TAU=6.2831853f;
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        // ---- raised wainscot / dado panelling relief --------------------
        // a chair-rail band crosses the wall ~40% up; below = recessed panels
        float rail = smoothstep(0.38f,0.40f,fy)*smoothstep(0.46f,0.44f,fy);   // bump band
        // vertical recessed panels in the lower dado (every ~1/4)
        float pcol = fx*4.0f; float pc = pcol - floorf(pcol);
        float panelEdge = (smoothstep(0.06f,0.10f,pc)*smoothstep(0.94f,0.90f,pc));
        float panelLow  = (fy<0.40f) ? (1.0f-panelEdge) : 0.0f;              // recessed inside
        float dadoBevel = (fy<0.40f) ? (panelEdge) : 0.0f;
        // ---- fine wallpaper weave + seams -------------------------------
        float seam = powf(0.5f+0.5f*cosf(fx*TAU*3.0f),40.0f)*0.4f;
        float fineV = 0.5f+0.5f*sinf(fx*TAU*64.0f);          // tight vertical weave
        float fineH = 0.5f+0.5f*sinf(fy*TAU*64.0f);          // cross weave
        float weave = fineV*0.55f + fineH*0.45f;
        float grain = fbmT(fx*8.f, fy*8.f, 6, 8);
        float micro = fbmT(fx*64.f, fy*64.f, 3, 64);
        float macro = fbmT(fx*2.4f+5.f, fy*2.4f, 5, 4);      // big tonal blotching
        // ---- damp / water stains travelling down from the top ----------
        float stainF = fbmT(fx*3.f+11.f, fy*2.f, 5, 4);
        float drip   = smoothstep(0.55f,0.85f,stainF) * smoothstep(0.0f,0.55f,fy)*0.95f;
        float blotch = smoothstep(0.60f,0.80f, fbmT(fx*5.f, fy*5.f+3.f,5,8));
        float mold   = smoothstep(0.70f,0.88f, fbmT(fx*9.f+2.f, fy*9.f, 4, 16)) * smoothstep(0.0f,0.3f,fy);
        // ---- iconic mustard / biscuit base ------------------------------
        float base = 0.88f + 0.05f*weave - 0.10f*seam + 0.06f*macro;
        float r = base*0.85f;
        float g = base*0.745f;
        float b = base*0.40f;
        // recessed panels slightly darker, bevels catch light (brighter)
        r*=1.0f-0.12f*panelLow + 0.10f*dadoBevel;
        g*=1.0f-0.12f*panelLow + 0.10f*dadoBevel;
        b*=1.0f-0.14f*panelLow + 0.10f*dadoBevel;
        // grain + micro speckle dirt
        r*=0.90f+0.16f*grain; g*=0.90f+0.16f*grain; b*=0.88f+0.20f*grain;
        r*=0.97f+0.06f*micro; g*=0.97f+0.06f*micro; b*=0.96f+0.08f*micro;
        // water stains -> browner & darker
        float st = clampf(drip*0.7f + blotch*0.5f, 0.f, 1.f);
        r=lerp(r, r*0.60f+0.06f, st);
        g=lerp(g, g*0.50f+0.04f, st);
        b=lerp(b, b*0.38f, st);
        // greenish-black mold tint
        r=lerp(r, r*0.42f, mold*0.7f);
        g=lerp(g, g*0.50f, mold*0.7f);
        b=lerp(b, b*0.40f, mold*0.7f);
        setPix(alb,(y*S+x)*3, r,g,b);
        // ---- height: panel relief dominates, then weave/grain ----------
        float h = grain*0.26f + weave*0.05f - seam*0.5f + micro*0.05f;
        h += rail*0.85f;                 // raised chair-rail
        h -= panelLow*0.35f;             // recessed panel interior
        h += dadoBevel*0.20f;            // raised bevel edges
        height[y*S+x]= h;
        // ---- roughness / ao --------------------------------------------
        float rough = clampf(0.72f + 0.18f*grain + 0.12f*st + 0.10f*mold, 0.28f, 1.f);
        float ao    = clampf(0.84f + 0.16f*(1.f-st) - 0.08f*seam - 0.10f*panelLow - 0.12f*mold, 0.f, 1.f);
        mrt.px[(y*S+x)*3+0]=(uint8_t)(rough*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(ao*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,3.0f);     // stronger relief
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  SCUFFED BASEBOARD / SKIRTING  (darker bottom strip wall)
// =============================================================
static Material makeBaseboard(){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float grain=fbmT(fx*10.f,fy*10.f,4,16);
        float scuff=smoothstep(0.5f,0.8f, fbmT(fx*6.f,fy*14.f,3,8));
        float r=0.30f+0.10f*grain, g=0.27f+0.09f*grain, b=0.19f+0.07f*grain;
        r=lerp(r,r*0.6f,scuff); g=lerp(g,g*0.6f,scuff); b=lerp(b,b*0.6f,scuff);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=grain*0.3f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.55f+0.2f*grain,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(0.85f*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,2.0f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  SHORT OFFICE CARPET (floor) - distinct olive/mustard, fibrous
// =============================================================
static Material makeCarpet(){
    const int S=512;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        // tight directional loop fibers
        float fiberA = 0.5f+0.5f*sinf((fx*220.f) + 4.f*fbmT(fx*10.f,fy*10.f,3,16));
        float fiberB = 0.5f+0.5f*sinf((fy*200.f) + 4.f*fbmT(fx*9.f,fy*9.f,3,16));
        float fiber  = (fiberA*0.6f+fiberB*0.4f);
        float speck  = fbmT(fx*60.f,fy*60.f,3,64);
        float blotch = smoothstep(0.5f,0.82f, fbmT(fx*3.5f,fy*3.5f,4,4));
        // large slow discoloration patches (sun-fade / age variation)
        float discolor = fbmT(fx*1.7f+20.f,fy*1.7f,4,2);
        // dark organic stains (spills, mildew)
        float darkStain = smoothstep(0.66f,0.86f, fbmT(fx*6.f+7.f,fy*6.f,4,8));
        // bleached / worn-through patches showing backing
        float worn = smoothstep(0.74f,0.9f, fbmT(fx*2.4f+30.f,fy*2.4f,4,4));
        // darker, greener mustard so it reads different from walls
        float base=0.55f+0.30f*fiber;
        float r=base*0.62f, g=base*0.55f, b=base*0.24f;
        r*=0.94f+0.10f*speck; g*=0.94f+0.10f*speck; b*=0.92f+0.12f*speck;
        // discoloration: shift hue/brightness in patches
        r*=0.85f+0.25f*discolor; g*=0.86f+0.24f*discolor; b*=0.80f+0.30f*discolor;
        // dirty traffic paths
        r=lerp(r,r*0.6f,blotch*0.8f); g=lerp(g,g*0.58f,blotch*0.8f); b=lerp(b,b*0.5f,blotch*0.8f);
        // dark stains -> brown/black
        r=lerp(r,r*0.32f+0.03f,darkStain); g=lerp(g,g*0.28f+0.02f,darkStain); b=lerp(b,b*0.25f,darkStain);
        // worn patches -> grey backing
        r=lerp(r,0.30f,worn*0.7f); g=lerp(g,0.29f,worn*0.7f); b=lerp(b,0.26f,worn*0.7f);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=fiber*0.5f+speck*0.1f - worn*0.3f;
        // roughness varies with wear: worn/stained areas slicker
        float rough=clampf(0.97f - worn*0.35f - darkStain*0.2f, 0.4f, 1.0f);
        mrt.px[(y*S+x)*3+0]=(uint8_t)(rough*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.78f+0.22f*(1.f-blotch)-0.2f*darkStain,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,3.2f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  ACOUSTIC DROP CEILING - white tiles, dark grid, sag + holes
// =============================================================
static Material makeCeiling(){
    const int S=512;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    const int TILE=S/2; // 2x2 tiles
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        int gx=x%TILE, gy=y%TILE;
        float gw=3.0f; // grid line width px
        bool grid = (gx<gw||gy<gw||gx>TILE-gw||gy>TILE-gw);
        // perforated acoustic dots
        float holes=worley(fx*40.f,fy*40.f,40);
        float dot = smoothstep(0.18f,0.06f,holes);
        // subtle sag toward tile center -> darker AO
        float cx=(gx/(float)TILE-0.5f), cy=(gy/(float)TILE-0.5f);
        float sag = 1.0f-0.35f*smoothstep(0.0f,0.5f,sqrtf(cx*cx+cy*cy));
        float yellowStain = smoothstep(0.6f,0.85f, fbmT(fx*4.f,fy*4.f,3,4));
        float base=0.86f*sag - 0.22f*dot;
        float r=base*0.97f, g=base*0.95f, b=base*0.88f;
        // age yellowing
        r=lerp(r,r*1.0f,1); g=lerp(g,g*0.95f,yellowStain); b=lerp(b,b*0.78f,yellowStain);
        if(grid){ r*=0.30f; g*=0.30f; b*=0.30f; }
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]= grid? -0.7f : (sag*0.2f - dot*0.25f);
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(grid?0.6f:0.88f,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(grid?0.45f:sag,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,2.8f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  BARE SERVICE-AREA CONCRETE (maintenance / utility rooms)
// =============================================================
static Material makeConcrete(){
    const int S=512;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*6.f,fy*6.f,5,8);
        float fine=fbmT(fx*40.f,fy*40.f,3,40);
        float crack=smoothstep(0.02f,0.0f, worley(fx*8.f,fy*8.f,8)-0.04f);
        float stain=smoothstep(0.55f,0.85f, fbmT(fx*3.f+5.f,fy*3.f,4,4));
        float g=0.42f+0.12f*n+0.05f*fine;
        float r=g*1.02f, gg=g*1.0f, b=g*0.97f;
        r=lerp(r,r*0.5f,crack); gg=lerp(gg,gg*0.5f,crack); b=lerp(b,b*0.5f,crack);
        r=lerp(r,r*0.7f,stain*0.6f); gg=lerp(gg,gg*0.72f,stain*0.6f); b=lerp(b,b*0.68f,stain*0.6f);
        setPix(alb,(y*S+x)*3,r,gg,b);
        height[y*S+x]= n*0.3f+fine*0.1f - crack*0.8f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.80f+0.15f*n,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.85f-0.3f*crack,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,2.6f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  WET LINOLEUM / TILE FLOOR (maintenance) - reflective grout grid
// =============================================================
static Material makeTileFloor(){
    const int S=512;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    const int TILE=S/4;
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        int gx=x%TILE, gy=y%TILE;
        float gw=4.0f;
        bool grout=(gx<gw||gy<gw);
        float n=fbmT(fx*12.f,fy*12.f,4,16);
        float dirt=smoothstep(0.5f,0.85f, fbmT(fx*4.f,fy*4.f,3,8));
        float base=0.55f+0.10f*n;
        float r=base*0.92f, g=base*0.90f, b=base*0.80f;
        r=lerp(r,r*0.7f,dirt); g=lerp(g,g*0.7f,dirt); b=lerp(b,b*0.68f,dirt);
        if(grout){ r*=0.45f; g*=0.45f; b*=0.42f; }
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]= grout? -0.6f : n*0.1f;
        // wet -> low roughness on tiles, rough grout
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(grout?0.7f:0.28f+0.2f*dirt,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(grout?0.5f:0.9f,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,2.4f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  GENERIC FLAT PBR MATERIAL (props - metal, wood, plastic, ...)
//  Small noise so it's never a perfectly flat plastic look.
// =============================================================
static Material makeFlat(float r,float g,float b,float rough,float metal){
    const int S=64;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image nrm{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*10.f,fy*10.f,3,16);
        int i=(y*S+x)*3;
        setPix(alb,i, r*(0.9f+0.18f*n), g*(0.9f+0.18f*n), b*(0.9f+0.18f*n));
        height[y*S+x]=n*0.3f;
        mrt.px[i+0]=(uint8_t)(clampf(rough*(0.9f+0.2f*n),0,1)*255);
        mrt.px[i+1]=255;
        mrt.px[i+2]=(uint8_t)(metal*255);
    }
    nrm=normalFromHeight(height,S,S,1.0f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  CORRUGATED CARDBOARD (boxes) - tan with flute lines + tape
// =============================================================
static Material makeCardboard(){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float flute = 0.5f+0.5f*sinf(fy*6.2831853f*28.0f); // corrugation
        float fiber = fbmT(fx*30.f,fy*30.f,3,32);
        float stain = smoothstep(0.55f,0.85f, fbmT(fx*4.f,fy*4.f,4,4));
        float r=0.56f+0.10f*flute*0.3f+0.08f*fiber;
        float g=0.42f+0.08f*flute*0.3f+0.06f*fiber;
        float b=0.26f+0.05f*flute*0.3f+0.04f*fiber;
        r=lerp(r,r*0.7f,stain); g=lerp(g,g*0.7f,stain); b=lerp(b,b*0.7f,stain);
        // packing tape strip across the middle
        if(fy>0.46f&&fy<0.54f){ r=lerp(r,0.62f,0.5f); g=lerp(g,0.6f,0.5f); b=lerp(b,0.55f,0.5f); }
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=flute*0.15f+fiber*0.1f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.88f+0.1f*fiber,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.85f-0.3f*stain,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,1.8f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  BRUSHED / WORN METAL (barrels, pipes) - anisotropic streaks
// =============================================================
static Material makeMetal(float baseR,float baseG,float baseB,bool rusty){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float brush = 0.5f+0.5f*sinf(fy*900.f + 6.f*fbmT(fx*4.f,fy*4.f,3,4));
        float n=fbmT(fx*8.f,fy*8.f,4,8);
        float rust = rusty? smoothstep(0.45f,0.8f, fbmT(fx*6.f,fy*6.f,4,8)) : 0.0f;
        float r=baseR*(0.85f+0.2f*brush*0.3f+0.1f*n);
        float g=baseG*(0.85f+0.2f*brush*0.3f+0.1f*n);
        float b=baseB*(0.85f+0.2f*brush*0.3f+0.1f*n);
        // rust patches -> orange/brown, non-metal
        r=lerp(r,0.42f,rust); g=lerp(g,0.22f,rust); b=lerp(b,0.12f,rust);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=n*0.15f+rust*0.2f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(lerp(0.30f,0.85f,rust)+0.1f*n,0,1)*255);
        mrt.px[(y*S+x)*3+1]=255;
        mrt.px[(y*S+x)*3+2]=(uint8_t)((1.0f-rust)*255); // metallic except rust
    }
    Image nrm=normalFromHeight(height,S,S,1.5f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  TINY 5x7 BITMAP FONT for procedural signs / labels / numbers
// =============================================================
static const char* glyph(char c){
    switch(c){
    case 'A': return "01110""10001""10001""11111""10001""10001""10001";
    case 'B': return "11110""10001""11110""10001""10001""10001""11110";
    case 'C': return "01111""10000""10000""10000""10000""10000""01111";
    case 'D': return "11110""10001""10001""10001""10001""10001""11110";
    case 'E': return "11111""10000""11110""10000""10000""10000""11111";
    case 'F': return "11111""10000""11110""10000""10000""10000""10000";
    case 'G': return "01111""10000""10000""10111""10001""10001""01111";
    case 'H': return "10001""10001""11111""10001""10001""10001""10001";
    case 'I': return "11111""00100""00100""00100""00100""00100""11111";
    case 'J': return "00111""00010""00010""00010""10010""10010""01100";
    case 'K': return "10001""10010""11100""10010""10001""10001""10001";
    case 'L': return "10000""10000""10000""10000""10000""10000""11111";
    case 'M': return "10001""11011""10101""10001""10001""10001""10001";
    case 'N': return "10001""11001""10101""10011""10001""10001""10001";
    case 'O': return "01110""10001""10001""10001""10001""10001""01110";
    case 'P': return "11110""10001""10001""11110""10000""10000""10000";
    case 'Q': return "01110""10001""10001""10001""10101""10010""01101";
    case 'R': return "11110""10001""10001""11110""10100""10010""10001";
    case 'S': return "01111""10000""10000""01110""00001""00001""11110";
    case 'T': return "11111""00100""00100""00100""00100""00100""00100";
    case 'U': return "10001""10001""10001""10001""10001""10001""01110";
    case 'V': return "10001""10001""10001""10001""10001""01010""00100";
    case 'W': return "10001""10001""10001""10101""10101""11011""10001";
    case 'X': return "10001""01010""00100""00100""00100""01010""10001";
    case 'Y': return "10001""01010""00100""00100""00100""00100""00100";
    case 'Z': return "11111""00010""00100""01000""10000""10000""11111";
    case '0': return "01110""10011""10101""10101""11001""10001""01110";
    case '1': return "00100""01100""00100""00100""00100""00100""01110";
    case '2': return "01110""10001""00001""00110""01000""10000""11111";
    case '3': return "11110""00001""00001""01110""00001""00001""11110";
    case '4': return "00010""00110""01010""10010""11111""00010""00010";
    case '5': return "11111""10000""11110""00001""00001""10001""01110";
    case '6': return "01110""10000""11110""10001""10001""10001""01110";
    case '7': return "11111""00001""00010""00100""01000""01000""01000";
    case '8': return "01110""10001""10001""01110""10001""10001""01110";
    case '9': return "01110""10001""10001""01111""00001""00001""01110";
    case '-': return "00000""00000""00000""11111""00000""00000""00000";
    case '.': return "00000""00000""00000""00000""00000""00110""00110";
    case '!': return "00100""00100""00100""00100""00100""00000""00100";
    case '/': return "00001""00010""00100""00100""01000""10000""10000";
    case ' ': default: return "00000""00000""00000""00000""00000""00000""00000";
    }
}

// Generate a sign/label texture. bg/fg colors, multi-line text via '\n'.
// If 'aged' adds grime so it fits the abandoned theme.
static Material makeSign(const char* text, float bgR,float bgG,float bgB,
                         float fgR,float fgG,float fgB, bool aged=true){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S, 0.f);
    // background
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*8.f,fy*8.f,3,8);
        float r=bgR*(0.92f+0.12f*n), g=bgG*(0.92f+0.12f*n), b=bgB*(0.92f+0.12f*n);
        // border frame
        float bd = (fx<0.04f||fx>0.96f||fy<0.04f||fy>0.96f)?0.5f:1.0f;
        r*=bd; g*=bd; b*=bd;
        setPix(alb,(y*S+x)*3,r,g,b);
        mrt.px[(y*S+x)*3+0]=(uint8_t)(0.6f*255);
        mrt.px[(y*S+x)*3+1]=255; mrt.px[(y*S+x)*3+2]=0;
    }
    // count lines
    std::vector<std::string> lines; std::string cur;
    for(const char* p=text;*p;p++){ if(*p=='\n'){ lines.push_back(cur); cur.clear(); } else cur+=*p; }
    lines.push_back(cur);
    int nLines=(int)lines.size();
    int gw=5, gh=7;
    // fit text region inside frame
    int regionX0=(int)(S*0.10f), regionX1=(int)(S*0.90f);
    int regionY0=(int)(S*0.12f), regionY1=(int)(S*0.88f);
    int rowH=(regionY1-regionY0)/nLines;
    for(int li=0; li<nLines; li++){
        const std::string& ln=lines[li];
        int n=(int)ln.size(); if(n==0) continue;
        int cellW=(regionX1-regionX0)/std::max(1,n);
        int px=(int)(cellW*0.18f), py=(int)(rowH*0.16f);
        int cw=cellW-2*px, ch=rowH-2*py;
        float sx=cw/(float)gw, sy=ch/(float)gh;
        for(int ci=0; ci<n; ci++){
            const char* gl=glyph(ln[ci]);
            int ox=regionX0+ci*cellW+px;
            int oy=regionY0+li*rowH+py;
            for(int yy=0; yy<gh; yy++) for(int xx=0; xx<gw; xx++){
                if(gl[yy*gw+xx]!='1') continue;
                int x0=ox+(int)(xx*sx), x1=ox+(int)((xx+1)*sx);
                int y0=oy+(int)(yy*sy), y1=oy+(int)((yy+1)*sy);
                for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++){
                    if(x<0||x>=S||y<0||y>=S) continue;
                    setPix(alb,(y*S+x)*3, fgR,fgG,fgB);
                    height[y*S+x]=0.6f;
                }
            }
        }
    }
    // aging grime
    if(aged){
        for(int y=0;y<S;y++) for(int x=0;x<S;x++){
            float fx=(float)x/S, fy=(float)y/S;
            float grime=smoothstep(0.55f,0.85f, fbmT(fx*5.f,fy*5.f,4,8));
            int i=(y*S+x)*3;
            alb.px[i+0]=(uint8_t)(alb.px[i+0]*(1.f-grime*0.5f));
            alb.px[i+1]=(uint8_t)(alb.px[i+1]*(1.f-grime*0.5f));
            alb.px[i+2]=(uint8_t)(alb.px[i+2]*(1.f-grime*0.5f));
        }
    }
    Image nrm=normalFromHeight(height,S,S,1.5f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  HANDWRITTEN WALL MARKINGS (transparent decal)
//  Simulates marker / spray / brush writing directly on the wall.
//  No background plate: output is RGBA so it blends onto the wall.
//  Strokes are jittered, uneven-weight, with bleed + drips so they
//  read like a panicked human wrote them, not a printed sign.
// =============================================================
static inline void stampStroke(ImageA& im, std::vector<float>& cov,
                               float x0,float y0,float x1,float y1,
                               float w, uint32_t& seed){
    int S=im.w;
    auto rnd=[&](){ seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return (seed&0xffffff)/16777215.0f; };
    int steps=(int)(sqrtf((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0))*S)+2;
    for(int s=0;s<=steps;s++){
        float t=(float)s/steps;
        // slight wobble so the line is hand-drawn, not ruler-straight
        float wob=(rnd()-0.5f)*0.012f;
        float px=lerp(x0,x1,t)+wob, py=lerp(y0,y1,t)+wob;
        // pressure varies along the stroke (thicker middle, thin tails)
        float pressure=0.55f+0.45f*sinf(t*3.14159f)*(0.8f+0.4f*rnd());
        float rad=w*pressure*S;
        int cx=(int)(px*S), cy=(int)(py*S);
        int ir=(int)ceilf(rad)+1;
        for(int dy=-ir;dy<=ir;dy++)for(int dx=-ir;dx<=ir;dx++){
            int X=cx+dx, Y=cy+dy; if(X<0||X>=S||Y<0||Y>=S) continue;
            float d=sqrtf((float)dx*dx+(float)dy*dy);
            float a=smoothstep(rad, rad*0.4f, d);          // soft edge
            a*= (0.82f+0.18f*rnd());                        // ink mottle
            if(a>cov[Y*S+X]) cov[Y*S+X]=a;
        }
    }
}

// draw a single glyph into coverage using stroke segments approximated
// from the 5x7 bitmap font but rendered as continuous wobbly ink.
static inline void inkGlyph(ImageA& im,std::vector<float>& cov,char c,
                            float ox,float oy,float gw,float gh,float weight,uint32_t& seed){
    const char* g=glyph(c);
    int GX=5,GY=7;
    // For each "on" cell, connect to neighbouring on-cells to form strokes.
    for(int y=0;y<GY;y++) for(int x=0;x<GX;x++){
        if(g[y*GX+x]!='1') continue;
        float px=ox+(x+0.5f)/GX*gw, py=oy+(y+0.5f)/GY*gh;
        // connect right & down where filled to make continuous ink
        if(x+1<GX && g[y*GX+x+1]=='1'){
            float qx=ox+(x+1.5f)/GX*gw; stampStroke(im,cov,px,py,qx,py,weight,seed);
        }
        if(y+1<GY && g[(y+1)*GX+x]=='1'){
            float qy=oy+(y+1.5f)/GY*gh; stampStroke(im,cov,px,py,px,qy,weight,seed);
        }
        // diagonal joins for nicer letterforms
        if(x+1<GX && y+1<GY && g[(y+1)*GX+x+1]=='1' && g[y*GX+x+1]!='1' && g[(y+1)*GX+x]!='1'){
            float qx=ox+(x+1.5f)/GX*gw, qy=oy+(y+1.5f)/GY*gh; stampStroke(im,cov,px,py,qx,qy,weight,seed);
        }
        // isolated dot
        stampStroke(im,cov,px,py,px,py,weight*0.7f,seed);
    }
}

// style: 0 black marker, 1 red paint, 2 charcoal/scratch, 3 chalk-white
static Material makeHandwriting(const char* text,int style,uint32_t seed=12345){
    const int S=256;
    ImageA alb{S,S,std::vector<uint8_t>((size_t)S*S*4,0)};
    std::vector<float> cov((size_t)S*S,0.0f);
    std::vector<float> height((size_t)S*S,0.0f);

    // split lines
    std::vector<std::string> lines; std::string cur;
    for(const char* p=text;*p;p++){ if(*p=='\n'){ lines.push_back(cur); cur.clear(); } else cur+=*p; }
    lines.push_back(cur);
    int nLines=(int)lines.size();

    float marginY=0.14f, marginX=0.10f;
    float usableH=1.0f-2*marginY;
    float rowH=usableH/nLines;
    float weight = (style==1? 0.020f : style==0? 0.013f : style==2? 0.010f : 0.012f);

    uint32_t s=seed;
    for(int li=0; li<nLines; li++){
        const std::string& ln=lines[li]; int n=(int)ln.size(); if(n==0) continue;
        // slight overall slant + baseline drift (rushed writing)
        float slant=((s=s*1664525u+1013904223u, (s>>16)&0xff)/255.0f-0.5f)*0.06f;
        float usableW=1.0f-2*marginX;
        float cellW=usableW/std::max(1,n);
        float gW=cellW*0.78f, gH=rowH*0.74f;
        for(int ci=0; ci<n; ci++){
            float drift=sinf((float)ci*0.9f+li)*0.012f;
            float ox=marginX+ci*cellW + (rowH)*slant*li;
            float oy=marginY+li*rowH + drift;
            inkGlyph(alb,cov,ln[ci],ox,oy,gW,gH,weight,s);
        }
    }

    // base ink colour per style
    float ir,ig,ib;
    switch(style){
        case 1: ir=0.46f; ig=0.04f; ib=0.03f; break;   // dried blood-red paint
        case 2: ir=0.06f; ig=0.06f; ib=0.07f; break;   // charcoal
        case 3: ir=0.82f; ig=0.82f; ib=0.80f; break;   // chalk
        default:ir=0.05f; ig=0.05f; ib=0.06f; break;    // black marker
    }
    uint32_t ns=seed*2654435761u+1;
    auto rnd=[&](){ ns^=ns<<13; ns^=ns>>17; ns^=ns<<5; return (ns&0xffffff)/16777215.0f; };
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float a=cov[y*S+x];
        if(a<=0.003f) continue;
        float fx=(float)x/S, fy=(float)y/S;
        // ink mottle / fade so strokes aren't flat
        float mott=0.75f+0.25f*fbmT(fx*30.f,fy*30.f,3,32);
        float fade=0.85f+0.15f*fbmT(fx*5.f,fy*5.f,3,8);
        float r=ir*mott, g=ig*mott, b=ib*mott;
        int i=(y*S+x)*4;
        alb.px[i+0]=(uint8_t)(clampf(r,0,1)*255);
        alb.px[i+1]=(uint8_t)(clampf(g,0,1)*255);
        alb.px[i+2]=(uint8_t)(clampf(b,0,1)*255);
        alb.px[i+3]=(uint8_t)(clampf(a*fade,0,1)*255);
        height[y*S+x]=a*0.4f;
    }
    Image nrm=normalFromHeight(height,S,S,0.8f);
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    for(size_t i=0;i<(size_t)S*S;i++){ mrt.px[i*3+0]= (style==1?150:200); mrt.px[i*3+1]=255; mrt.px[i*3+2]=0; }
    Material m; m.albedo=uploadA(alb,true); m.normal=upload(nrm,false); m.mrt=upload(mrt,false); m.decal=true;
    return m;
}

// =============================================================
//  VENTILATION GRILLE  (metal louvre panel for ceiling/wall)
// =============================================================
static Material makeVent(){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float louvre=0.5f+0.5f*sinf(fy*6.2831853f*12.0f);
        float slot=smoothstep(0.35f,0.55f,louvre); // dark gaps between blades
        float n=fbmT(fx*8.f,fy*8.f,3,8);
        float dust=smoothstep(0.55f,0.85f, fbmT(fx*4.f,fy*4.f,3,8));
        float base=0.34f+0.10f*n;
        float r=base, g=base*1.0f, b=base*1.05f;
        r*=slot; g*=slot; b*=slot;
        r=lerp(r,r*0.7f,dust*0.5f); g=lerp(g,g*0.7f,dust*0.5f); b=lerp(b,b*0.65f,dust*0.5f);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]= (louvre-0.5f)*0.6f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.45f+0.2f*n,0,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.7f-0.3f*slot+0.0f,0,1)*255+30);
        mrt.px[(y*S+x)*3+2]=(uint8_t)(0.85f*255);
    }
    Image nrm=normalFromHeight(height,S,S,2.2f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  MAINTENANCE / ACCESS PANEL  (riveted metal, screws in corners)
// =============================================================
static Material makePanel(){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*7.f,fy*7.f,4,8);
        float brush=0.5f+0.5f*sinf(fx*700.f+4.f*fbmT(fx*4.f,fy*4.f,3,4));
        float edge=(fx<0.05f||fx>0.95f||fy<0.05f||fy>0.95f)?0.7f:1.0f;
        float base=(0.40f+0.08f*n+0.05f*brush*0.3f)*edge;
        float r=base*0.96f, g=base*0.97f, b=base*1.02f;
        // 4 corner screws
        float h=0;
        auto screw=[&](float sx,float sy){
            float d=sqrtf((fx-sx)*(fx-sx)+(fy-sy)*(fy-sy));
            if(d<0.05f){ float k=smoothstep(0.05f,0.0f,d); r=lerp(r,0.2f,k); g=lerp(g,0.2f,k); b=lerp(b,0.22f,k); h-=k*0.4f; }
        };
        screw(0.12f,0.12f); screw(0.88f,0.12f); screw(0.12f,0.88f); screw(0.88f,0.88f);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]= n*0.2f + (edge<1.0f?-0.3f:0.0f) + h;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.35f+0.2f*n,0,1)*255);
        mrt.px[(y*S+x)*3+1]=255;
        mrt.px[(y*S+x)*3+2]=(uint8_t)(0.9f*255);
    }
    Image nrm=normalFromHeight(height,S,S,2.4f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  DOOR  — painted metal door with recessed panels, frame, worn
//  edges and a brushed handle plate. baseR/G/B set the paint
//  colour (purple teleport door / brown exit door). Glossy paint.
// =============================================================
static Material makeDoor(float baseR,float baseG,float baseB){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*9.f,fy*9.f,4,9);
        float grime=smoothstep(0.55f,0.92f, fbmT(fx*3.f,fy*3.f,4,4));
        // two recessed rectangular panels (upper + lower)
        float h=0.0f; float panel=1.0f;
        auto inset=[&](float x0,float y0,float x1,float y1){
            if(fx>x0&&fx<x1&&fy>y0&&fy<y1){
                float bx=std::min(std::min(fx-x0,x1-fx),std::min(fy-y0,y1-fy));
                float k=smoothstep(0.0f,0.05f,bx);
                h-=k*0.5f; panel=lerp(1.0f,0.82f,k);
            }
        };
        inset(0.16f,0.10f,0.84f,0.46f);
        inset(0.16f,0.54f,0.84f,0.90f);
        // outer frame bevel
        float frame=(fx<0.06f||fx>0.94f||fy<0.05f||fy>0.95f)?0.86f:1.0f;
        if(frame<1.0f) h+=0.25f;
        float shade=(0.78f+0.22f*n)*panel*frame;
        float r=baseR*shade, g=baseG*shade, b=baseB*shade;
        // grime / scuffs darken the paint near the bottom
        float wear=grime*(0.4f+0.6f*(1.0f-fy));
        r=lerp(r,r*0.55f,wear); g=lerp(g,g*0.55f,wear); b=lerp(b,b*0.55f,wear);
        // brushed-steel handle plate on the right side, mid height
        bool handle=(fx>0.80f&&fx<0.90f&&fy>0.46f&&fy<0.56f);
        float metal=0.0f, rough=0.30f+0.18f*n;
        if(handle){
            float br=0.5f+0.5f*sinf(fy*420.f);
            r=0.62f+0.12f*br; g=0.63f+0.12f*br; b=0.67f+0.12f*br;
            metal=1.0f; rough=0.22f; h+=0.3f;
        }
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=h + n*0.08f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(rough,0.04f,1)*255);
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.92f-0.4f*wear,0,1)*255);
        mrt.px[(y*S+x)*3+2]=(uint8_t)(metal*255);
    }
    Image nrm=normalFromHeight(height,S,S,2.6f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

// =============================================================
//  STATUE / FIGURE SKIN — pale, weathered, plaster-like surface
//  with grime streaks and cracks. Used for the disturbing
//  humanoid statues. Slightly emissive eyes handled separately.
// =============================================================
static Material makeStatue(){
    const int S=256;
    Image alb{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    Image mrt{S,S,std::vector<uint8_t>((size_t)S*S*3)};
    std::vector<float> height((size_t)S*S);
    for(int y=0;y<S;y++) for(int x=0;x<S;x++){
        float fx=(float)x/S, fy=(float)y/S;
        float n=fbmT(fx*7.f,fy*7.f,5,7);
        float pores=fbmT(fx*40.f,fy*40.f,3,40);
        // vertical grime streaks running down the figure
        float streak=smoothstep(0.6f,0.95f, fbmT(fx*5.f,fy*1.2f,4,5));
        // hairline cracks
        float crack=smoothstep(0.86f,0.9f, fbmT(fx*12.f,fy*12.f,4,12));
        float base=0.46f+0.12f*n+0.05f*pores;
        float r=base*1.02f, g=base*0.99f, b=base*0.93f;       // pale grey-bone
        r=lerp(r,r*0.5f,streak*0.7f); g=lerp(g,g*0.5f,streak*0.7f); b=lerp(b,b*0.45f,streak*0.7f);
        r=lerp(r,0.10f,crack); g=lerp(g,0.10f,crack); b=lerp(b,0.10f,crack);
        setPix(alb,(y*S+x)*3,r,g,b);
        height[y*S+x]=n*0.2f+pores*0.1f-crack*0.4f;
        mrt.px[(y*S+x)*3+0]=(uint8_t)(clampf(0.72f+0.2f*n,0,1)*255); // matte
        mrt.px[(y*S+x)*3+1]=(uint8_t)(clampf(0.85f-0.4f*streak,0,1)*255);
        mrt.px[(y*S+x)*3+2]=0;
    }
    Image nrm=normalFromHeight(height,S,S,2.2f);
    return { upload(alb,true), upload(nrm,false), upload(mrt,false) };
}

} // namespace tex
