#pragma once
// =============================================================
//  GLSL shaders (embedded so the build is self-contained).
//  Pipeline: shadow depth pass -> HDR geometry pass (PBR +
//  many flickering point lights + directional fill + spot
//  flashlight + atmospheric fog) -> bright extract -> gaussian
//  bloom -> composite (ACES tonemap, vignette, chromatic
//  aberration, grain, dithering).
// =============================================================

// ---------- Geometry pass vertex shader ----------
static const char* VS_SCENE = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightSpace;

out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 UV;
    vec4 FragPosLightSpace;
    mat3 TBN;
} vs;

void main(){
    vec4 world = uModel * vec4(aPos,1.0);
    vs.FragPos = world.xyz;
    mat3 nm = transpose(inverse(mat3(uModel)));
    vs.Normal = normalize(nm * aNormal);
    vs.UV = aUV;
    vs.FragPosLightSpace = uLightSpace * world;

    vec3 T = normalize(nm * aTangent);
    vec3 N = vs.Normal;
    T = normalize(T - dot(T,N)*N);
    vec3 B = cross(N,T);
    vs.TBN = mat3(T,B,N);

    gl_Position = uProj * uView * world;
}
)GLSL";

// ---------- Geometry pass fragment shader (PBR) ----------
static const char* FS_SCENE = R"GLSL(
#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 UV;
    vec4 FragPosLightSpace;
    mat3 TBN;
} fs;

uniform vec3 uViewPos;

uniform sampler2D uAlbedo;
uniform sampler2D uNormalMap;
uniform sampler2D uRoughMap;   // r=roughness g=ao b=metallic
uniform sampler2D uShadowMap;
uniform float uUVScale;
uniform vec2  uUVScale2;        // separate U,V tiling (walls)
uniform vec3  uTint;
uniform float uEmissive;
uniform int   uHasNormal;
uniform int   uUseUV2;

#define MAX_LIGHTS 32
uniform int   uNumLights;
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightFlicker[MAX_LIGHTS]; // 0..1 brightness multiplier

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uAmbientSky;   // up tint
uniform vec3 uAmbientGround;// down tint

uniform int  uFlashOn;
uniform vec3 uFlashPos;
uniform vec3 uFlashDir;

// fog
uniform vec3  uFogColor;
uniform float uFogDensity;

uniform int   uIsDecal;       // 1 = handwriting/decal w/ alpha
uniform float uExposureKey;   // scene brightness (graphics setting)

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float a){
    float a2=a*a;
    float NdotH=max(dot(N,H),0.0);
    float d=(NdotH*NdotH*(a2-1.0)+1.0);
    return a2/(PI*d*d+1e-5);
}
float GeometrySchlickGGX(float NdotV, float k){ return NdotV/(NdotV*(1.0-k)+k); }
float GeometrySmith(vec3 N, vec3 V, vec3 L, float k){
    return GeometrySchlickGGX(max(dot(N,V),0.0),k)*GeometrySchlickGGX(max(dot(N,L),0.0),k);
}
vec3 fresnelSchlick(float ct, vec3 F0){ return F0+(1.0-F0)*pow(clamp(1.0-ct,0.0,1.0),5.0); }

// 16-tap Poisson disk for smoother, cheaper soft shadows than a full
// 5x5 box. Rotated per-fragment to break up banding into noise.
const vec2 POISSON16[16] = vec2[](
    vec2(-0.94201624,-0.39906216), vec2( 0.94558609,-0.76890725),
    vec2(-0.09418410,-0.92938870), vec2( 0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232,-0.87912464),
    vec2(-0.38277543, 0.27676845), vec2( 0.97484398, 0.75648379),
    vec2( 0.44323325,-0.97511554), vec2( 0.53742981,-0.47373420),
    vec2(-0.26496911,-0.41893023), vec2( 0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2( 0.19984126, 0.78641367), vec2( 0.14383161,-0.14100790));

float rand21(vec2 co){ return fract(sin(dot(co,vec2(12.9898,78.233)))*43758.5453); }

float ShadowCalc(vec4 fragPosLS, vec3 N, vec3 L){
    vec3 proj = fragPosLS.xyz/fragPosLS.w;
    proj = proj*0.5+0.5;
    if(proj.z>1.0) return 0.0;
    // slope-scaled bias reduces both acne and peter-panning
    float ndl = max(dot(N,L),0.0);
    float bias = max(0.0022*(1.0-ndl), 0.0008);
    vec2 texel = 1.0/vec2(textureSize(uShadowMap,0));
    // per-fragment rotation of the Poisson kernel -> dithered penumbra
    float ang = rand21(proj.xy*1024.0)*6.2831853;
    float ca=cos(ang), sa=sin(ang);
    mat2 rot = mat2(ca,-sa,sa,ca);
    float radius = 1.7;
    float shadow=0.0;
    for(int i=0;i<16;i++){
        vec2 off = rot*POISSON16[i]*texel*radius;
        float pcf = texture(uShadowMap, proj.xy+off).r;
        shadow += (proj.z-bias > pcf)?1.0:0.0;
    }
    // edge fade so the shadow map border doesn't hard-cut
    float edge = smoothstep(0.0,0.06,proj.x)*smoothstep(1.0,0.94,proj.x)
               * smoothstep(0.0,0.06,proj.y)*smoothstep(1.0,0.94,proj.y);
    return (shadow/16.0)*edge;
}

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float rough, float metal, vec3 F0){
    vec3 H=normalize(V+L);
    float NDF=DistributionGGX(N,H,rough);
    float k=(rough+1.0); k=k*k/8.0;
    float G=GeometrySmith(N,V,L,k);
    vec3 F=fresnelSchlick(max(dot(H,V),0.0),F0);
    vec3 num=NDF*G*F;
    float den=4.0*max(dot(N,V),0.0)*max(dot(N,L),0.0)+1e-4;
    vec3 spec=num/den;
    vec3 kd=(vec3(1.0)-F)*(1.0-metal);
    float NdotL=max(dot(N,L),0.0);
    return (kd*albedo/PI + spec)*radiance*NdotL;
}

void main(){
    vec2 uv = uUseUV2==1 ? fs.UV*uUVScale2 : fs.UV*uUVScale;
    vec4 albedoA = texture(uAlbedo, uv);
    vec3 albedo = albedoA.rgb * uTint;
    float decalAlpha = 1.0;
    if(uIsDecal==1){
        decalAlpha = albedoA.a;
        if(decalAlpha < 0.04) discard;   // keep only the inked strokes
    }
    vec3 mrt = texture(uRoughMap, uv).rgb;
    float roughness = clamp(mrt.r,0.04,1.0);
    float ao = mrt.g;
    float metallic = mrt.b;

    vec3 N = normalize(fs.Normal);
    vec3 geoN = N;
    if(uHasNormal==1){
        vec3 nt = texture(uNormalMap, uv).rgb*2.0-1.0;
        N = normalize(fs.TBN * nt);
    }
    vec3 V = normalize(uViewPos - fs.FragPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);
    vec3 scatter = vec3(0.0);   // accumulated in-scatter toward camera (volumetric-ish)

    // ---- point lights (ceiling fluorescents) with smooth inverse-square ----
    for(int i=0;i<uNumLights && i<MAX_LIGHTS;i++){
        vec3 Lv = uLightPos[i]-fs.FragPos;
        float dist = length(Lv);
        float rad  = uLightRadius[i];
        if(dist>rad) continue;
        vec3 L = Lv/dist;
        // physically-plausible inverse-square with a smooth windowed cutoff
        float invSq = 1.0/(dist*dist + 0.6);
        float win = clamp(1.0 - pow(dist/rad,4.0), 0.0, 1.0); win*=win;
        float att = invSq*win*4.0;
        vec3 radiance = uLightColor[i]*att*uLightFlicker[i];
        Lo += brdf(N,V,L,radiance,albedo,roughness,metallic,F0);
        // cheap in-scatter: brighter when looking toward a near light through fog
        float vd = max(dot(V,L),0.0);
        scatter += uLightColor[i]*uLightFlicker[i]*win*invSq*pow(vd,6.0)*0.5;
    }

    // ---- directional fill + soft shadow ----
    {
        vec3 L=normalize(-uSunDir);
        float shadow=ShadowCalc(fs.FragPosLightSpace,N,L);
        Lo += (1.0-shadow)*brdf(N,V,L,uSunColor,albedo,roughness,metallic,F0);
    }

    // ---- flashlight spotlight (smooth cone + falloff) ----
    if(uFlashOn==1){
        vec3 Lv = uFlashPos - fs.FragPos;
        float dist = length(Lv);
        vec3 L = Lv/dist;
        float theta = dot(L, normalize(-uFlashDir));
        float inner=0.96, outer=0.78;
        float cone = clamp((theta-outer)/(inner-outer),0.0,1.0);
        cone = cone*cone*(3.0-2.0*cone);
        float att = cone/(1.0+0.045*dist*dist);
        Lo += brdf(N,V,L,vec3(5.0,4.8,4.4)*att,albedo,roughness,metallic,F0);
    }

    // ---- hemispheric ambient (sky/ground tint), uses geometric normal ----
    float hemi = geoN.y*0.5+0.5;
    vec3 amb = mix(uAmbientGround, uAmbientSky, hemi);
    vec3 ambient = (uAmbient + amb) * albedo * ao;

    // ---- environment specular reflection (approximate IBL) ----
    // Reflect the hemispheric room tone with a Fresnel-roughness term so
    // smooth surfaces (tiles, metal, wet floor) pick up the room. A blurred
    // (roughness-attenuated) lobe approximates pre-filtered env.
    float NdotV = max(dot(N,V),0.0);
    vec3  Fr = fresnelSchlick(NdotV, F0);
    float refHemi = R.y*0.5+0.5;
    // tri-tone env gradient: warm ceiling glow, neutral mid, darker floor
    vec3  envSky = mix(uAmbientSky, uAmbientSky*1.25+vec3(0.03,0.025,0.0), refHemi);
    vec3  envCol = mix(uAmbientGround, envSky, refHemi) * 3.0;
    float gloss  = 1.0 - roughness;
    float gloss3 = gloss*gloss*gloss;
    // grazing-angle reflection boost (RTX-like wet/polished look on floors)
    float graze = pow(1.0-NdotV, 5.0);
    vec3  envSpec = envCol * Fr * gloss3 * ao;
    envSpec += envCol * graze * gloss * ao * 0.6;
    // pick up nearby lamp colour in screen-space-style reflections on glossy floors
    for(int i=0;i<uNumLights && i<MAX_LIGHTS && i<10;i++){
        vec3 Lv = uLightPos[i]-fs.FragPos;
        float dist=length(Lv);
        if(dist>uLightRadius[i]) continue;
        vec3 L=Lv/dist;
        // sharp mirror lobe for crisp reflected highlights
        float spec = pow(max(dot(R,L),0.0), mix(8.0,256.0,gloss));
        float invSq=1.0/(dist*dist+0.6);
        float win = clamp(1.0 - pow(dist/uLightRadius[i],4.0), 0.0, 1.0);
        envSpec += uLightColor[i]*spec*invSq*win*gloss*gloss*uLightFlicker[i]*0.9;
    }

    // ---- micro contact occlusion from normal curvature (fake SSAO) ----
    float contact = clamp(0.5 + 0.5*geoN.y, 0.0, 1.0);

    vec3 color = (ambient*contact) + Lo + envSpec + albedo*uEmissive;

    // ---- atmospheric distance fog (exponential, height-graded) ----
    float dCam = length(uViewPos - fs.FragPos);
    float heightFactor = clamp(1.0 - (fs.FragPos.y-0.2)*0.05, 0.45, 1.0);
    float fog = 1.0 - exp(-uFogDensity*dCam*heightFactor);
    color = mix(color, uFogColor, clamp(fog,0.0,0.90));
    // add the volumetric in-scatter on top of fog (light shafts/glow haze)
    color += scatter*clamp(fog*1.2,0.0,1.0);

    color *= uExposureKey;

    FragColor = vec4(color, decalAlpha);
}
)GLSL";

// ---------- Shadow depth pass ----------
static const char* VS_DEPTH = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uLightSpace;
uniform mat4 uModel;
void main(){ gl_Position = uLightSpace*uModel*vec4(aPos,1.0); }
)GLSL";

static const char* FS_DEPTH = R"GLSL(
#version 330 core
void main(){}
)GLSL";

// ---------- Fullscreen quad VS ----------
static const char* VS_QUAD = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)GLSL";

// ---------- Bright-pass extraction ----------
static const char* FS_BRIGHT = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D uScene;
uniform float uThreshold;
void main(){
    vec3 c = texture(uScene,vUV).rgb;
    float b = dot(c, vec3(0.2126,0.7152,0.0722));
    // soft-knee threshold (smooth transition into bloom, no hard clip)
    float knee = 0.5;
    float soft = b - uThreshold + knee;
    soft = clamp(soft, 0.0, 2.0*knee);
    soft = soft*soft/(4.0*knee+1e-4);
    float contrib = max(soft, b-uThreshold)/max(b,1e-4);
    vec3 outc = c * clamp(contrib,0.0,1.0);
    FragColor=vec4(outc,1.0);
}
)GLSL";

// ---------- Gaussian blur (9-tap) ----------
static const char* FS_BLUR = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir;
void main(){
    vec2 ts = 1.0/vec2(textureSize(uTex,0));
    // wider 7-tap kernel with 2px stride -> softer, filmic bloom falloff
    float w[7]=float[](0.196482,0.174378,0.121632,0.066798,0.028532,0.009577,0.002400);
    vec3 r = texture(uTex,vUV).rgb*w[0];
    for(int i=1;i<7;i++){
        vec2 off = uDir*ts*(float(i)*1.6);
        r += texture(uTex,vUV+off).rgb*w[i];
        r += texture(uTex,vUV-off).rgb*w[i];
    }
    FragColor=vec4(r,1.0);
}
)GLSL";

// ---------- Final composite ----------
static const char* FS_COMPOSITE = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uTime;
uniform float uBloomStrength;
uniform int   uDebug;

// ACES filmic fit (Narkowicz) operating in linear space.
vec3 ACES(vec3 x){
    float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);
}
float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }

void main(){
    if(uDebug==1){ FragColor=vec4(pow(texture(uScene,vUV).rgb,vec3(1.0/2.2)),1.0); return; }

    vec2 q = vUV-0.5;
    float r2 = dot(q,q);
    // chromatic aberration that grows toward the edges (lens feel)
    float ca = 0.0020*r2;
    vec3 hdr;
    hdr.r = texture(uScene, vUV + q*ca).r;
    hdr.g = texture(uScene, vUV).g;
    hdr.b = texture(uScene, vUV - q*ca).b;

    vec3 bloom = texture(uBloom,vUV).rgb;
    hdr += bloom*uBloomStrength;

    vec3 mapped = ACES(hdr*uExposure);

    // ---- subtle cinematic colour grade toward the sickly Backrooms palette ----
    // lift shadows toward cool, push highlights toward warm mustard.
    float luma = dot(mapped, vec3(0.2126,0.7152,0.0722));
    vec3 shadowTint = vec3(0.92,0.95,1.04);   // cool shadows
    vec3 highTint   = vec3(1.06,1.02,0.86);   // warm highs
    vec3 grade = mix(shadowTint, highTint, smoothstep(0.0,0.7,luma));
    mapped *= grade;
    // gentle S-curve contrast
    mapped = mix(mapped, mapped*mapped*(3.0-2.0*mapped), 0.18);

    mapped = pow(clamp(mapped,0.0,1.0), vec3(1.0/2.2));

    // vignette (slightly stronger for oppressive framing)
    float vig = smoothstep(0.98,0.28,length(q));
    mapped *= mix(0.38,1.0,vig);

    // animated film grain (luminance-scaled so dark areas stay grainy)
    float gn = (hash(vUV*vec2(1920.0,1080.0)+fract(uTime))*2.0-1.0);
    mapped += gn*0.032*(1.0-0.5*luma);

    // ordered dithering to kill banding in dark gradients
    float dth = (hash(floor(vUV*vec2(640.0,360.0)))-0.5)/255.0;
    mapped += dth;

    FragColor=vec4(clamp(mapped,0.0,1.0),1.0);
}
)GLSL";

// =============================================================
//  UI / HUD shaders (orthographic 2D overlay, alpha blended).
//  A single flexible shader draws solid panels, gradient bars,
//  rounded rectangles and glyph atlases depending on uMode.
// =============================================================
static const char* VS_UI = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;   // pixel coords
layout(location=1) in vec2 aUV;
uniform vec2 uScreen;              // framebuffer size
out vec2 vUV;
out vec2 vLocal;
void main(){
    vUV = aUV;
    vLocal = aPos;
    vec2 ndc = vec2(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0);
    gl_Position = vec4(ndc,0.0,1.0);
}
)GLSL";

static const char* FS_UI = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 vUV;
in vec2 vLocal;
uniform int   uMode;       // 0 solid, 1 vertical gradient, 2 rounded panel, 3 glyph atlas, 4 vignette
uniform vec4  uColor;
uniform vec4  uColor2;
uniform vec4  uRect;       // x,y,w,h (px) for rounded panel
uniform float uRadius;
uniform sampler2D uTex;    // glyph atlas
uniform float uFill;       // 0..1 fill fraction for bars
uniform float uTime;

float sdRound(vec2 p, vec2 b, float r){
    vec2 d = abs(p)-b+r;
    return length(max(d,0.0)) + min(max(d.x,d.y),0.0) - r;
}

void main(){
    if(uMode==0){
        FragColor = uColor;
    } else if(uMode==1){
        // gradient + fill cutoff (bars)
        if(vUV.x > uFill){ FragColor = uColor2; }      // empty track color
        else {
            vec3 c = mix(uColor.rgb, uColor.rgb*1.4, vUV.y);
            // subtle animated sheen
            float sh = 0.10*sin(vUV.x*20.0 - uTime*4.0);
            FragColor = vec4(c + sh, uColor.a);
        }
    } else if(uMode==2){
        vec2 c = uRect.xy + uRect.zw*0.5;
        float d = sdRound(vLocal - c, uRect.zw*0.5, uRadius);
        float a = smoothstep(1.5, -1.5, d);
        // subtle inner border highlight (1.5px) for a crisp "glass" edge
        float border = smoothstep(2.5,0.5,abs(d+1.5));
        vec3 rgb = mix(uColor.rgb, uColor.rgb*1.9+vec3(0.04), border*0.6);
        FragColor = vec4(rgb, uColor.a*a);
    } else if(uMode==3){
        float g = texture(uTex, vUV).r;
        FragColor = vec4(uColor.rgb, uColor.a*g);
    } else { // vignette / fade overlay
        float v = length(vUV-0.5);
        FragColor = vec4(uColor.rgb, uColor.a*smoothstep(0.2,0.8,v));
    }
}
)GLSL";
