#pragma once
// =============================================================
//  BACKROOMS - Audio engine (miniaudio, self-contained)
//  ------------------------------------------------------------
//  Fully procedural synthesis - NO external sound files. Every
//  sound is generated into a PCM buffer at startup and played
//  through miniaudio (WinMM/WASAPI on Windows, ALSA/Pulse on
//  Linux). The engine supports:
//    - looping ambience beds (deep room tone + air handling)
//    - independent electrical hum / buzz per active light region
//    - ventilation whoosh
//    - distant intermittent mechanical clanks
//    - surface-dependent footsteps (carpet / concrete / tile)
//    - camera servo motor whirr (positional volume)
//    - light buzz/flicker zaps
//  Spatialisation is a simple distance + stereo-pan model driven
//  by the listener transform updated every frame.
// =============================================================
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <atomic>

#include "miniaudio.h"

namespace audio {

constexpr int   SR = 44100; // sample rate
constexpr float PI = 3.14159265358979323846f;

// ---- small deterministic noise helpers (synthesis only) ----
struct Rnd { uint32_t s; Rnd(uint32_t x):s(x?x:1){}
    float f(){ s^=s<<13; s^=s>>17; s^=s<<5; return (s&0xffffff)/16777215.0f; }
    float bi(){ return f()*2.0f-1.0f; } };

// A baked PCM clip (mono, float).
struct Clip { std::vector<float> d; };

// ---- One active voice playing a clip ----
struct Voice {
    const Clip* clip=nullptr;
    double pos=0.0;        // sample cursor (double for pitch)
    float  pitch=1.0f;
    float  volL=0.0f, volR=0.0f;     // target gains
    float  curL=0.0f, curR=0.0f;     // smoothed gains
    bool   loop=false;
    bool   active=false;
    int    id=-1;          // caller handle for looping voices
};

// =============================================================
//  Procedural clip synthesis
// =============================================================
inline Clip synthAmbience(){
    // deep, oppressive room tone: layered low rumble + faint air hiss
    int N=SR*8; Clip c; c.d.resize(N); Rnd rnd(7);
    float lp=0, lp2=0;
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float white=rnd.bi();
        lp += (white-lp)*0.0008f;          // very low rumble
        lp2+= (white-lp2)*0.02f;           // air hiss band
        float rumble = lp*2.4f;
        float hiss   = lp2*0.05f;
        // slow swell so it breathes
        float swell=0.6f+0.4f*sinf(t*0.13f);
        c.d[i]=(rumble*swell + hiss)*0.5f;
    }
    // loop-smoothing crossfade
    int xf=SR/2;
    for(int i=0;i<xf;i++){ float a=(float)i/xf; c.d[i]=c.d[i]*a + c.d[N-xf+i]*(1.0f-a); }
    return c;
}

inline Clip synthHum(float baseHz){
    // Natural, warm mains hum from a fluorescent ballast. Smoother and
    // softer than before: dominant low fundamental + 2nd harmonic, with
    // the buzzy upper harmonics and the fast tremolo greatly reduced so
    // it reads as a gentle background hum rather than an electrical rasp.
    int N=SR*3; Clip c; c.d.resize(N); Rnd rnd(99);
    float lp=0.0f;
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float v = 0.62f*sinf(2*PI*baseHz*t)
                + 0.26f*sinf(2*PI*baseHz*2*t)
                + 0.07f*sinf(2*PI*baseHz*3*t);
        // a whisper of filtered noise (not raw white) for body, low-passed
        float nz=rnd.bi();
        lp += (nz-lp)*0.015f;
        v += lp*0.05f;
        // very slow, gentle amplitude breathing (no fast tremolo)
        v *= 0.95f+0.05f*sinf(t*1.3f);
        c.d[i]=v*0.16f;
    }
    int xf=1024; for(int i=0;i<xf;i++){ float a=(float)i/xf; c.d[i]=c.d[i]*a+c.d[N-xf+i]*(1.0f-a); }
    return c;
}

inline Clip synthVent(){
    // ventilation: filtered noise whoosh with slow modulation
    int N=SR*5; Clip c; c.d.resize(N); Rnd rnd(31);
    float bp=0, lp=0;
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float w=rnd.bi();
        lp += (w-lp)*0.04f;
        bp = lp - lp*0.5f;
        float mod=0.7f+0.3f*sinf(t*0.4f);
        c.d[i]=bp*0.30f*mod;
    }
    int xf=SR/3; for(int i=0;i<xf;i++){ float a=(float)i/xf; c.d[i]=c.d[i]*a+c.d[N-xf+i]*(1.0f-a); }
    return c;
}

inline Clip synthFootstep(int surface){
    // surface: 0 carpet, 1 concrete, 2 tile
    int N=SR/4; Clip c; c.d.resize(N); Rnd rnd(surface*131+5);
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float env=expf(-t*(surface==0?34.0f:18.0f));
        float body, click;
        if(surface==0){ // carpet: soft, muffled thud
            body = sinf(2*PI*70*t)*env;
            click= rnd.bi()*env*0.25f;
            c.d[i]=(body*0.7f+click)*0.5f;
        } else if(surface==1){ // concrete: harder, mid crack
            body = sinf(2*PI*120*t)*env;
            click= rnd.bi()*expf(-t*60.0f)*0.6f;
            c.d[i]=(body*0.6f+click)*0.7f;
        } else { // tile: sharp clack with ring
            body = sinf(2*PI*210*t)*env;
            float ring=sinf(2*PI*900*t)*expf(-t*40.0f)*0.3f;
            click= rnd.bi()*expf(-t*90.0f)*0.7f;
            c.d[i]=(body*0.5f+ring+click)*0.7f;
        }
    }
    return c;
}

inline Clip synthServo(){
    // camera servo motor: tonal whirr + gear texture, loopable
    int N=SR*2; Clip c; c.d.resize(N); Rnd rnd(57);
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float gear = sinf(2*PI*240*t) + 0.5f*sinf(2*PI*480*t);
        float tex  = rnd.bi()*0.15f;
        c.d[i]=(gear*0.10f+tex*0.06f);
    }
    int xf=256; for(int i=0;i<xf;i++){ float a=(float)i/xf; c.d[i]=c.d[i]*a+c.d[N-xf+i]*(1.0f-a); }
    return c;
}

inline Clip synthClank(){
    // distant mechanical clank / pipe knock
    int N=SR; Clip c; c.d.resize(N); Rnd rnd(303);
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float env=expf(-t*7.0f);
        float metal = sinf(2*PI*180*t)+0.6f*sinf(2*PI*430*t)+0.3f*sinf(2*PI*770*t);
        c.d[i]=metal*env*0.18f + rnd.bi()*expf(-t*30.0f)*0.05f;
    }
    return c;
}

inline Clip synthZap(){
    // light flicker electrical snap
    int N=SR/6; Clip c; c.d.resize(N); Rnd rnd(811);
    for(int i=0;i<N;i++){
        float t=(float)i/SR;
        float env=expf(-t*55.0f);
        c.d[i]=(rnd.bi()*0.6f + sinf(2*PI*1400*t)*0.3f)*env;
    }
    return c;
}

// =============================================================
//  Engine
// =============================================================
class Engine {
public:
    static constexpr int MAXV = 48;

    bool init(){
        amb_   = synthAmbience();
        hum_   = synthHum(58.0f);
        vent_  = synthVent();
        servo_ = synthServo();
        clank_ = synthClank();
        zap_   = synthZap();
        foot_[0]= synthFootstep(0);
        foot_[1]= synthFootstep(1);
        foot_[2]= synthFootstep(2);
        for(auto&v:voices_) v.active=false;

        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate        = SR;
        cfg.dataCallback      = &Engine::cb;
        cfg.pUserData         = this;
        if(ma_device_init(nullptr,&cfg,&device_)!=MA_SUCCESS) { ok_=false; return false; }
        if(ma_device_start(&device_)!=MA_SUCCESS){ ma_device_uninit(&device_); ok_=false; return false; }
        ok_=true;
        return true;
    }
    void shutdown(){ if(ok_){ ma_device_uninit(&device_); ok_=false; } }
    bool ok() const { return ok_; }

    void setMasterVolume(float v){ master_.store(std::clamp(v,0.0f,1.0f)); }
    void setMuted(bool m){ muted_.store(m); }

    // one-shot positional play (returns nothing)
    void playAt(const Clip& clip, float gain, float pan, float pitch=1.0f){
        lock();
        int idx=allocVoice(-1);
        if(idx>=0){
            Voice&v=voices_[idx];
            v.clip=&clip; v.pos=0; v.pitch=pitch; v.loop=false; v.active=true;
            v.volL=gain*(0.5f-0.5f*pan); v.volR=gain*(0.5f+0.5f*pan);
            v.curL=v.volL; v.curR=v.volR; // snap one-shots in
        }
        unlock();
    }
    void footstep(int surface, float gain, float pan){
        surface=std::clamp(surface,0,2);
        playAt(foot_[surface], gain, pan, 0.92f+0.16f*((float)(rng_.f())));
    }
    void zap(float gain,float pan){ playAt(zap_,gain,pan); }
    void clank(float gain,float pan){ playAt(clank_,gain,pan,0.8f+0.4f*rng_.f()); }

    // looping handle voices identified by id (ambience, vent, hum-bus, servo)
    void setLoop(int id, const Clip& clip, float gain, float pan, float pitch=1.0f){
        lock();
        int idx=findVoice(id);
        if(idx<0){
            idx=allocVoice(id);
            if(idx>=0){ voices_[idx].clip=&clip; voices_[idx].pos=rng_.f()*clip.d.size();
                        voices_[idx].loop=true; voices_[idx].active=true; }
        }
        if(idx>=0){
            Voice&v=voices_[idx];
            v.pitch=pitch;
            v.volL=gain*(0.5f-0.5f*pan);
            v.volR=gain*(0.5f+0.5f*pan);
        }
        unlock();
    }
    void stopLoop(int id){
        lock(); int idx=findVoice(id); if(idx>=0){ voices_[idx].volL=0; voices_[idx].volR=0; } unlock();
    }

    const Clip& ambience()const{return amb_;}
    const Clip& hum()const{return hum_;}
    const Clip& vent()const{return vent_;}
    const Clip& servo()const{return servo_;}

private:
    ma_device device_{};
    bool ok_=false;
    std::atomic<float> master_{0.85f};
    std::atomic<bool>  muted_{false};
    Voice voices_[MAXV];
    Clip amb_,hum_,vent_,servo_,clank_,zap_,foot_[3];
    Rnd  rng_{2024};
    std::atomic_flag lk_ = ATOMIC_FLAG_INIT;

    void lock(){ while(lk_.test_and_set(std::memory_order_acquire)){} }
    void unlock(){ lk_.clear(std::memory_order_release); }

    int findVoice(int id){ for(int i=0;i<MAXV;i++) if(voices_[i].active&&voices_[i].id==id) return i; return -1; }
    int allocVoice(int id){
        for(int i=0;i<MAXV;i++) if(!voices_[i].active){ voices_[i]=Voice{}; voices_[i].active=true; voices_[i].id=id; return i; }
        return -1;
    }

    static void cb(ma_device* dev, void* out, const void*, ma_uint32 frames){
        Engine* e=(Engine*)dev->pUserData;
        e->render((float*)out, frames);
    }
    void render(float* out, ma_uint32 frames){
        float master = muted_.load()?0.0f:master_.load();
        for(ma_uint32 f=0; f<frames; f++){ out[f*2]=0; out[f*2+1]=0; }
        lock();
        for(int i=0;i<MAXV;i++){
            Voice&v=voices_[i];
            if(!v.active||!v.clip) continue;
            const std::vector<float>& d=v.clip->d;
            size_t n=d.size();
            for(ma_uint32 f=0; f<frames; f++){
                // smooth gains
                v.curL += (v.volL-v.curL)*0.002f;
                v.curR += (v.volR-v.curR)*0.002f;
                size_t i0=(size_t)v.pos; 
                if(i0>=n){
                    if(v.loop){ v.pos=fmod(v.pos,(double)n); i0=(size_t)v.pos; }
                    else { v.active=false; break; }
                }
                size_t i1=(i0+1<n)?i0+1:(v.loop?0:i0);
                float frac=(float)(v.pos-(double)i0);
                float s=d[i0]*(1.0f-frac)+d[i1]*frac;
                out[f*2]   += s*v.curL*master;
                out[f*2+1] += s*v.curR*master;
                v.pos += v.pitch;
            }
            // retire near-silent one-shots that finished
            if(!v.loop && (size_t)v.pos>=n) v.active=false;
        }
        unlock();
        // soft clip to avoid harsh distortion
        for(ma_uint32 f=0; f<frames*2; f++){
            float x=out[f];
            out[f]= x/(1.0f+fabsf(x)*0.6f);
        }
    }
};

// loop voice ids
enum { LOOP_AMB=1000, LOOP_VENT=1001, LOOP_HUM=1002, LOOP_SERVO=1003 };

} // namespace audio
