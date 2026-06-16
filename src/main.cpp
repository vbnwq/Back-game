// =============================================================
//  BACKROOMS - First Person 3D Game  (C++ / OpenGL 3.3 Core)
//  ------------------------------------------------------------
//  - Large procedurally-generated Backrooms maze (rooms, halls,
//    dead ends, hidden alcoves, concrete service blocks, tile
//    maintenance rooms) following Level-0 design principles.
//  - All geometry is REAL solid 3D (beveled boxes, capped
//    cylinders, spheres) with thickness; nothing is a billboard
//    and nothing disappears at grazing angles.
//  - High-res procedural PBR materials with strong separation
//    between floor / wall / ceiling / props.
//  - Physically-based lighting: many flickering ceiling
//    fluorescents (inverse-square point lights), directional
//    fill with PCF soft shadows, camera flashlight spotlight,
//    hemispheric ambient, atmospheric fog.
//  - HDR -> bloom -> ACES tonemap -> vignette / CA / grain.
//  - Realistic FPS controller: genuine Shift sprint (stamina),
//    jump, head-bob, breathing, landing dip, wall-sliding.
//  - Environmental storytelling via procedural signs / labels.
//  100% self-contained: no external model/texture files.
// =============================================================
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#include "shaders.h"
#include "textures.h"
#include "geometry.h"
#include "map.h"
#include "player.h"
#include "audio.h"
#include "ui.h"

// ---------------- game state ----------------
enum GameState { GS_PLAY, GS_PAUSE_MAIN, GS_PAUSE_GRAPHICS, GS_PAUSE_AUDIO, GS_PAUSE_CONTROLS };

struct Settings {
    float masterVol   = 0.85f;
    bool  muted       = false;
    float mouseSens   = 0.08f;
    int   quality     = 2;     // 0 low,1 med,2 high,3 ultra
    bool  vsync       = true;
    float brightness  = 1.0f;
    bool  bloom       = true;
    bool  filmGrain   = true;
};

// ---------------- globals ----------------
static int   WIN_W = 1280, WIN_H = 720;
static int   FB_W  = 1280, FB_H  = 720;
static Player gPlayer;
static bool  gFirstMouse = true;
static double gLastX=0, gLastY=0;
static bool  gCaptured = true;
static bool  gFlashOn  = false;
static GameState gState = GS_PLAY;
static Settings  gSet;
static bool  gQuit = false;
static int   gMenuSel = 0;      // keyboard menu selection
static audio::Engine gAudio;

// ---------------- shader helpers ----------------
static GLuint compile(GLenum type, const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[2048]; glGetShaderInfoLog(s,2048,nullptr,log);
        fprintf(stderr,"Shader compile error:\n%s\n",log); }
    return s;
}
static GLuint program(const char* vs,const char* fs){
    GLuint v=compile(GL_VERTEX_SHADER,vs), f=compile(GL_FRAGMENT_SHADER,fs);
    GLuint p=glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
    int ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char log[2048]; glGetProgramInfoLog(p,2048,nullptr,log);
        fprintf(stderr,"Link error:\n%s\n",log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}
static void setM4(GLuint p,const char*n,const glm::mat4&m){ glUniformMatrix4fv(glGetUniformLocation(p,n),1,GL_FALSE,glm::value_ptr(m)); }
static void setV3(GLuint p,const char*n,const glm::vec3&v){ glUniform3fv(glGetUniformLocation(p,n),1,glm::value_ptr(v)); }
static void setV2(GLuint p,const char*n,const glm::vec2&v){ glUniform2fv(glGetUniformLocation(p,n),1,glm::value_ptr(v)); }
static void setF (GLuint p,const char*n,float v){ glUniform1f(glGetUniformLocation(p,n),v); }
static void setI (GLuint p,const char*n,int v){ glUniform1i(glGetUniformLocation(p,n),v); }

// ---------------- mouse position for menu hit-testing ----------------
static double gMouseX=0, gMouseY=0;
static bool   gClick=false;

// ---------------- callbacks ----------------
static void fbsize(GLFWwindow*,int w,int h){ FB_W=w; FB_H=h; }
static void mousecb(GLFWwindow*,double x,double y){
    gMouseX=x; gMouseY=y;
    if(gFirstMouse){ gLastX=x; gLastY=y; gFirstMouse=false; }
    float dx=(float)(x-gLastX), dy=(float)(y-gLastY);
    gLastX=x; gLastY=y;
    if(gCaptured && gState==GS_PLAY) gPlayer.mouse(dx,dy,gSet.mouseSens);
}
static void mousebtn(GLFWwindow*,int button,int action,int){
    if(button==GLFW_MOUSE_BUTTON_LEFT && action==GLFW_PRESS) gClick=true;
}
static void setPlay(GLFWwindow* w){
    gState=GS_PLAY; gCaptured=true;
    glfwSetInputMode(w,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
    gFirstMouse=true;
}
static void setPaused(GLFWwindow* w){
    gState=GS_PAUSE_MAIN; gCaptured=false; gMenuSel=0;
    glfwSetInputMode(w,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
}
static int gMenuCount=7; // updated per page before nav
static void keycb(GLFWwindow* w,int key,int,int action,int){
    if(action!=GLFW_PRESS) return;
    if(key==GLFW_KEY_ESCAPE){
        if(gState==GS_PLAY) setPaused(w);
        else if(gState==GS_PAUSE_MAIN) setPlay(w);
        else { gState=GS_PAUSE_MAIN; gMenuSel=0; }   // back from subpage
        return;
    }
    if(gState==GS_PLAY){
        if(key==GLFW_KEY_F) gFlashOn=!gFlashOn;
        if(key==GLFW_KEY_M){ gSet.muted=!gSet.muted; gAudio.setMuted(gSet.muted); }
        return;
    }
    // menu keyboard navigation
    if(key==GLFW_KEY_UP||key==GLFW_KEY_W)   gMenuSel=(gMenuSel-1+gMenuCount)%gMenuCount;
    if(key==GLFW_KEY_DOWN||key==GLFW_KEY_S) gMenuSel=(gMenuSel+1)%gMenuCount;
}

// ---------------- scene draw command ----------------
enum MeshKind { MK_CUBE=0, MK_BEVEL=1, MK_CYL=2, MK_SPHERE=3, MK_PLANE=4,
                MK_BARREL=5, MK_LOCKER=6, MK_VENTBOX=7, MK_DOOR=8, MK_STATUE=9 };
struct DrawCmd {
    glm::mat4 model;
    tex::Material mat;
    glm::vec2 uvScale;   // U,V tiling
    glm::vec3 tint;
    float emissive;
    int mesh;
    bool castShadow;
    bool decal=false;
    // cached world-space AABB centre + radius for frustum/distance culling
    glm::vec3 center=glm::vec3(0);
    float bound=2.0f;
};

// flickering ceiling lamp
struct Lamp {
    glm::vec3 pos;       // light source position
    glm::vec3 color;
    float radius;
    // flicker params
    float phase;
    float rate;
    bool  faulty;        // some lamps stutter / are nearly dead
    bool  dead=false;    // fully failed light (area goes dark)
    int   drawIndex;     // index of emissive panel DrawCmd (to update emissive)
    float curFlickerCache=1.0f; // per-frame brightness multiplier
    float zapTimer=0.0f; // for independent buzz/snap audio
};

// security camera (surveillance system)
struct SecCam {
    glm::vec3 base;       // ceiling mount position
    float baseYaw;        // resting facing
    float yaw;            // current pan angle
    float targetYaw;
    float sweepPhase;
    bool  tracking=false; // currently sees player
    float indicatorPulse=0.0f;
    // draw command indices for animated parts
    int   armIdx=-1, bodyIdx=-1, lensIdx=-1, ledIdx=-1, glassIdx=-1;
};

// interactive door (purple = teleport, brown = exit/end)
enum DoorKind { DOOR_TELEPORT=0, DOOR_EXIT=1 };
struct Door {
    glm::vec3 pos;       // world position (centre of the slab, at floor + h/2)
    DoorKind kind;
    int   row, col;      // grid cell it occupies
    float glow=0.0f;     // pulsing emissive aura near the player
    int   drawIdx=-1;    // door slab draw command
    int   auraIdx=-1;    // emissive frame aura draw command
    bool  used=false;    // teleport cooldown / consumed
};

// disturbing humanoid statue.
//  - STATIC statues: always frozen, purely atmospheric.
//  - CHASER statues: hunt the player, BUT freeze instantly while
//    the player is looking at them. They never approach closer
//    than minDist. The player escapes by looking at it and walking
//    backwards away; once far enough the chaser gives up.
struct Statue {
    glm::vec3 basePos;     // home position
    glm::vec3 pos;         // current position (chasers move)
    float yaw=0.0f;        // facing
    bool  chaser=false;    // does it hunt the player?
    bool  active=false;    // chaser currently engaged / pursuing
    bool  frozen=true;     // currently frozen (looked-at or static)
    float minDist=2.6f;    // never gets closer than this
    float giveUpDist=22.0f;// disengage beyond this
    float speed=2.3f;
    int   drawIdx=-1;      // body draw command
    int   eyeLIdx=-1, eyeRIdx=-1; // glowing eyes
    float turnGlance=0.0f; // for the "occasionally turns to look at you" static ones
    float glanceTimer=0.0f;
};

int main(){
    if(!glfwInit()){ fprintf(stderr,"glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4);

    // ---- launch fullscreen on the primary monitor (borderless native res) ----
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vmode = mon? glfwGetVideoMode(mon) : nullptr;
    GLFWwindow* win=nullptr;
#ifdef SHOT_TEST
    // offscreen-style fixed window for deterministic screenshots
    win=glfwCreateWindow(WIN_W,WIN_H,"BACKROOMS - Level 0",nullptr,nullptr);
#else
    if(mon && vmode){
        glfwWindowHint(GLFW_RED_BITS,   vmode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, vmode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS,  vmode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE,vmode->refreshRate);
        WIN_W=vmode->width; WIN_H=vmode->height;
        win=glfwCreateWindow(WIN_W,WIN_H,"BACKROOMS - Level 0",mon,nullptr);
    }
    if(!win){ // fallback to a large windowed mode if fullscreen fails
        WIN_W=1280; WIN_H=720;
        win=glfwCreateWindow(WIN_W,WIN_H,"BACKROOMS - Level 0",nullptr,nullptr);
    }
#endif
    if(!win){ fprintf(stderr,"window failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(win,fbsize);
    glfwSetCursorPosCallback(win,mousecb);
    glfwSetMouseButtonCallback(win,mousebtn);
    glfwSetKeyCallback(win,keycb);
    glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_DISABLED);

    if(!gladLoadGL((GLADloadfunc)glfwGetProcAddress)){
        fprintf(stderr,"glad load failed\n"); return 1; }

    printf("============================================\n");
    printf("  BACKROOMS - Level 0   (C++ / OpenGL)\n");
    printf("--------------------------------------------\n");
    printf("  W / A / S / D : Move\n");
    printf("  Mouse         : Look around\n");
    printf("  Left Shift    : Sprint (uses stamina)\n");
    printf("  Space         : Jump\n");
    printf("  F             : Toggle flashlight\n");
    printf("  ESC           : Release / capture mouse\n");
    printf("============================================\n");
    fflush(stdout);

    glfwGetFramebufferSize(win,&FB_W,&FB_H);
#ifdef SHOT_TEST
    if(getenv("SHOT_FLASH")) gFlashOn=true;
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    glEnable(GL_FRAMEBUFFER_SRGB);

    // ---- shaders ----
    GLuint progScene = program(VS_SCENE, FS_SCENE);
    GLuint progDepth = program(VS_DEPTH, FS_DEPTH);
    GLuint progBright= program(VS_QUAD, FS_BRIGHT);
    GLuint progBlur  = program(VS_QUAD, FS_BLUR);
    GLuint progComp  = program(VS_QUAD, FS_COMPOSITE);

    // ---- cache scene-shader uniform locations once (perf) ----
    // Querying glGetUniformLocation + snprintf for every light every frame
    // is a real per-frame cost; cache everything up front instead.
    struct SceneLoc {
        GLint uModel,uView,uProj,uLightSpace,uViewPos,uSunDir,uSunColor,
              uAmbient,uAmbientSky,uAmbientGround,uFogColor,uFogDensity,
              uFlashOn,uFlashPos,uFlashDir,uNumLights,uAlbedo,uNormalMap,
              uRoughMap,uShadowMap,uExposureKey,uUVScale,uUVScale2,uUseUV2,
              uTint,uEmissive,uHasNormal,uIsDecal;
        GLint uLightPos[32],uLightColor[32],uLightRadius[32],uLightFlicker[32];
    } SL;
    glUseProgram(progScene);
    #define GLC(f) SL.f=glGetUniformLocation(progScene,#f)
    GLC(uModel);GLC(uView);GLC(uProj);GLC(uLightSpace);GLC(uViewPos);GLC(uSunDir);
    GLC(uSunColor);GLC(uAmbient);GLC(uAmbientSky);GLC(uAmbientGround);GLC(uFogColor);
    GLC(uFogDensity);GLC(uFlashOn);GLC(uFlashPos);GLC(uFlashDir);GLC(uNumLights);
    GLC(uAlbedo);GLC(uNormalMap);GLC(uRoughMap);GLC(uShadowMap);GLC(uExposureKey);
    GLC(uUVScale);GLC(uUVScale2);GLC(uUseUV2);GLC(uTint);GLC(uEmissive);
    GLC(uHasNormal);GLC(uIsDecal);
    #undef GLC
    for(int i=0;i<32;i++){
        char b[64];
        snprintf(b,64,"uLightPos[%d]",i);     SL.uLightPos[i]    =glGetUniformLocation(progScene,b);
        snprintf(b,64,"uLightColor[%d]",i);    SL.uLightColor[i]  =glGetUniformLocation(progScene,b);
        snprintf(b,64,"uLightRadius[%d]",i);   SL.uLightRadius[i] =glGetUniformLocation(progScene,b);
        snprintf(b,64,"uLightFlicker[%d]",i);  SL.uLightFlicker[i]=glGetUniformLocation(progScene,b);
    }
    GLint depthModelLoc = glGetUniformLocation(progDepth,"uModel");
    GLint depthLSLoc    = glGetUniformLocation(progDepth,"uLightSpace");

    // ---- meshes ----
    Mesh cube  = makeCube();
    Mesh bevel = makeBevelCube(0.05f);
    Mesh cyl   = makeCylinder(32);
    Mesh sph   = makeSphere(20,12);
    Mesh planeM= makePlaneDouble();
    Mesh barrel= makeBarrel(28);
    Mesh locker= makeLocker();
    Mesh ventB = makeVentBox();
    Mesh doorM = makeDoor();
    Mesh statueM= makeStatue();
    auto meshOf=[&](int k)->const Mesh&{
        switch(k){ case MK_BEVEL:return bevel; case MK_CYL:return cyl;
                   case MK_SPHERE:return sph; case MK_PLANE:return planeM;
                   case MK_BARREL:return barrel; case MK_LOCKER:return locker;
                   case MK_VENTBOX:return ventB; case MK_DOOR:return doorM;
                   case MK_STATUE:return statueM;
                   default:return cube; }
    };

    // ---- materials ----
    tex::Material matWall     = tex::makeWallpaper();
    tex::Material matBase     = tex::makeBaseboard();
    tex::Material matCarpet   = tex::makeCarpet();
    tex::Material matCeiling  = tex::makeCeiling();
    tex::Material matConcrete = tex::makeConcrete();
    tex::Material matTile     = tex::makeTileFloor();
    tex::Material matLampPanel= tex::makeFlat(0.96f,0.96f,0.90f,0.25f,0.0f);
    tex::Material matMetal    = tex::makeMetal(0.50f,0.51f,0.55f,false);
    tex::Material matSteel    = tex::makeMetal(0.58f,0.59f,0.62f,false);
    tex::Material matWood     = tex::makeFlat(0.34f,0.23f,0.13f,0.65f,0.0f);
    tex::Material matCardboard= tex::makeCardboard();
    tex::Material matPlastic  = tex::makeFlat(0.14f,0.14f,0.16f,0.45f,0.0f);
    tex::Material matRust     = tex::makeMetal(0.45f,0.30f,0.20f,true);
    tex::Material matVent     = tex::makeVent();
    tex::Material matPanel     = tex::makePanel();
    tex::Material matCamBody  = tex::makeFlat(0.78f,0.78f,0.80f,0.35f,0.0f);
    tex::Material matCamDark   = tex::makeFlat(0.05f,0.05f,0.06f,0.30f,0.1f);
    tex::Material matLED       = tex::makeFlat(0.9f,0.1f,0.1f,0.4f,0.0f);
    // doors + statues (next-gen additions)
    tex::Material matDoorPurple = tex::makeDoor(0.42f,0.16f,0.62f); // teleport door
    tex::Material matDoorBrown  = tex::makeDoor(0.40f,0.26f,0.13f); // exit / end door
    tex::Material matStatue     = tex::makeStatue();
    tex::Material matCamGlass    = tex::makeFlat(0.02f,0.03f,0.05f,0.06f,0.2f); // dark lens glass
    tex::Material matEye         = tex::makeFlat(1.0f,0.2f,0.15f,0.4f,0.0f);    // glowing statue eyes

    // handwritten wall markings (transparent decals - real ink, no plates)
    // styles: 0 black marker, 1 red paint, 2 charcoal, 3 chalk
    tex::Material hwHelp   = tex::makeHandwriting("HELP\nME", 1, 101);
    tex::Material hwNoExit = tex::makeHandwriting("NO\nEXIT", 0, 202);
    tex::Material hwTurn   = tex::makeHandwriting("DONT\nLOOK\nBACK", 0, 303);
    tex::Material hwLevel  = tex::makeHandwriting("LEVEL\n0", 2, 404);
    tex::Material hwArrow  = tex::makeHandwriting("THIS\nWAY", 0, 505);
    tex::Material hwNum    = tex::makeHandwriting("RM\n3491", 2, 606);
    tex::Material hwWatch  = tex::makeHandwriting("IT\nSEES\nYOU", 1, 707);
    tex::Material hwDays   = tex::makeHandwriting("DAY\n47", 2, 808);
    tex::Material hwKeep   = tex::makeHandwriting("KEEP\nOUT", 1, 909);
    tex::Material hwSym    = tex::makeHandwriting("X X X", 0, 110);
    tex::Material hwSafe   = tex::makeHandwriting("NOT\nSAFE", 1, 121);
    tex::Material* signList[] = { &hwHelp,&hwNoExit,&hwTurn,&hwLevel,&hwArrow,
                                  &hwNum,&hwWatch,&hwDays,&hwKeep,&hwSym,&hwSafe };
    int signCount=11;

    // ---- generate world ----
    bmap::generate(20240607u);
    bmap::clearObstacles();
    int RW=bmap::rows(), CW=bmap::cols();
    float H = bmap::WALL_H;
    float W = bmap::worldW(), D = bmap::worldD();

    bmap::Rng rng(99173u);

    std::vector<DrawCmd> draws;
    std::vector<Lamp> lamps;

    auto pushBox=[&](glm::vec3 p,glm::vec3 s,tex::Material m,int mesh,glm::vec2 uv,glm::vec3 tint,float emis,bool shadow)->int{
        glm::mat4 mm=glm::translate(glm::mat4(1),p);
        mm=glm::scale(mm,s);
        DrawCmd d{mm,m,uv,tint,emis,mesh,shadow};
        d.decal=m.decal;
        d.center=p;
        d.bound=glm::length(s)*0.5f+0.5f;
        draws.push_back(d);
        return (int)draws.size()-1;
    };
    auto matForFloor=[&](int zone)->tex::Material{
        if(zone==1) return matConcrete; if(zone==2) return matTile; return matCarpet;
    };

    // -------- floor (per open cell) + ceiling (EVERY cell, closed) --------
    // The ceiling now covers walls too, so a jumping player can never
    // peek over a wall into the void (wall-height exploit fix).
    for(int r=0;r<RW;r++) for(int c=0;c<CW;c++){
        glm::vec3 ctr=bmap::cellCenter(r,c);
        bool open=!bmap::isWall(r,c);
        if(open){
            int zone=bmap::floorZone(r,c);
            // floor slab (thickness 0.2, top at y=0). Per-cell UV phase
            // variation breaks the obvious repetition.
            pushBox(glm::vec3(ctr.x,-0.1f,ctr.z), glm::vec3(bmap::CELL,0.2f,bmap::CELL),
                    matForFloor(zone), MK_CUBE, glm::vec2(2.0f,2.0f), glm::vec3(0.92f+0.16f*rng.f()), 0.0f, false);
        }
        // ceiling slab over ALL cells (closed roof). Vary tile tint subtly
        // and give it a faint self-emissive so the drop-ceiling never reads
        // as a pure-black void (it catches indirect bounce from the tubes).
        float ct=0.85f+0.18f*rng.f();
        pushBox(glm::vec3(ctr.x,H+0.1f,ctr.z), glm::vec3(bmap::CELL,0.2f,bmap::CELL),
                matCeiling, MK_CUBE, glm::vec2(1.0f,1.0f), glm::vec3(ct), 0.05f, false);
    }

    // -------- walls (solid beveled boxes, only where they bound open space) --------
    // Each wall cell becomes a full-height beveled box. We still draw all wall
    // cells that touch any open neighbor so corridors are fully enclosed.
    auto touchesOpen=[&](int r,int c){
        for(int dr=-1;dr<=1;dr++)for(int dc=-1;dc<=1;dc++){
            if(!bmap::isWall(r+dr,c+dc)) return true;
        }
        return false;
    };
    for(int r=0;r<RW;r++) for(int c=0;c<CW;c++){
        if(!bmap::isWall(r,c)) continue;
        if(!touchesOpen(r,c)) continue; // skip fully-buried walls (perf)
        glm::vec3 ctr=bmap::cellCenter(r,c);
        bool concrete=bmap::isConcreteWall(r,c);
        tex::Material wm = concrete?matConcrete:matWall;
        glm::vec3 tint = concrete? glm::vec3(0.95f) : glm::vec3(1.0f);
        // main wall body. UV tiles ~ once per 2m so wallpaper reads as paper,
        // not stretched planks; vertical scaled to keep aspect ~square.
        pushBox(glm::vec3(ctr.x,H*0.5f,ctr.z), glm::vec3(bmap::CELL,H,bmap::CELL),
                wm, MK_BEVEL, glm::vec2(2.0f, 1.7f), tint, 0.0f, true);
        // baseboard skirting around exposed faces (only for wallpaper walls)
        if(!concrete){
            float bh=0.28f, bt=bmap::CELL*0.5f+0.03f;
            // place 4 thin strips facing each open neighbor
            struct Dir{int dr,dc; glm::vec3 off; glm::vec3 sc;};
            float half=bmap::CELL*0.5f;
            Dir dirs[4]={
                {0,1, glm::vec3(half,0,0), glm::vec3(0.06f,bh,bmap::CELL)},
                {0,-1,glm::vec3(-half,0,0),glm::vec3(0.06f,bh,bmap::CELL)},
                {1,0, glm::vec3(0,0,half), glm::vec3(bmap::CELL,bh,0.06f)},
                {-1,0,glm::vec3(0,0,-half),glm::vec3(bmap::CELL,bh,0.06f)},
            };
            for(auto&dd:dirs){
                if(!bmap::isWall(r+dd.dr,c+dd.dc)){
                    pushBox(ctr+dd.off+glm::vec3(0,bh*0.5f,0), dd.sc,
                            matBase, MK_CUBE, glm::vec2(2.0f,1.0f), glm::vec3(1),0.0f,false);
                }
            }
        }
    }

    // -------- ceiling detail: lights, vents, maintenance, wiring --------
    // Lighting placement varies naturally: not every grid cell gets a
    // fixture, some are dead, brightness/colour drift per fixture, and
    // ceilings carry vents / access panels / conduit for believability.
    for(int r=1;r<RW-1;r++) for(int c=1;c<CW-1;c++){
        if(bmap::isWall(r,c)) continue;
        glm::vec3 ctr=bmap::cellCenter(r,c);

        // wiring conduit runs along some ceiling cells (thin pipes)
        if(rng.f()<0.16f){
            bool along = rng.f()<0.5f;
            glm::vec3 sc = along? glm::vec3(bmap::CELL*0.9f,0.06f,0.06f)
                                : glm::vec3(0.06f,0.06f,bmap::CELL*0.9f);
            pushBox(glm::vec3(ctr.x,H-0.10f,ctr.z), sc,
                    matSteel, MK_CYL, glm::vec2(1,1), glm::vec3(0.45f),0.0f,false);
        }

        // ventilation grilles scattered on the ceiling
        if(rng.f()<0.07f){
            float vs=0.9f+rng.f()*0.4f;
            pushBox(glm::vec3(ctr.x,H-0.02f,ctr.z), glm::vec3(vs,0.18f,vs),
                    matVent, MK_VENTBOX, glm::vec2(1,1), glm::vec3(0.8f),0.0f,false);
            continue; // vent occupies this cell instead of a light
        }
        // maintenance / access panels (some damaged, sagging)
        if(rng.f()<0.05f){
            pushBox(glm::vec3(ctr.x,H+0.02f,ctr.z), glm::vec3(bmap::CELL*0.7f,0.10f,bmap::CELL*0.7f),
                    matPanel, MK_BEVEL, glm::vec2(1,1), glm::vec3(0.8f),0.0f,false);
            continue;
        }

        // light fixtures on a varied sparse pattern (jittered, not uniform)
        bool slot = ((r*7+c*3)%5==0) && rng.f()<0.62f;
        if(!slot) continue;

        bool dead   = rng.f()<0.10f;   // completely failed
        bool faulty = !dead && rng.f()<0.26f;
        // small positional jitter so rows don't read as a perfect grid
        float jx=(rng.f()-0.5f)*0.6f, jz=(rng.f()-0.5f)*0.6f;
        glm::vec3 fp(ctr.x+jx, 0, ctr.z+jz);

        // dark metal bezel frame
        pushBox(glm::vec3(fp.x,H-0.04f,fp.z), glm::vec3(2.6f,0.10f,0.9f),
                matMetal, MK_BEVEL, glm::vec2(1,1), glm::vec3(0.55f),0.0f,false);
        // emissive light panel (brighter so it blooms like a real tube)
        float panelEmis = dead? 0.05f : 4.2f;
        glm::vec3 panelTint = dead? glm::vec3(0.10f,0.10f,0.11f) : glm::vec3(1.0f,0.97f,0.86f);
        int panelIdx = pushBox(glm::vec3(fp.x,H-0.10f,fp.z), glm::vec3(2.3f,0.05f,0.62f),
                matLampPanel, MK_CUBE, glm::vec2(1,1), panelTint, panelEmis, false);
        Lamp lp;
        lp.pos = glm::vec3(fp.x, H-0.25f, fp.z);
        // per-fixture colour temperature + intensity drift. Brighter, with
        // wider falloff radius so each fixture actually pools light on the
        // floor and walls instead of leaving the room near-black.
        float warm=0.9f+0.2f*rng.f();
        lp.color = (faulty? glm::vec3(3.4f,3.2f,2.6f) : glm::vec3(4.6f,4.4f,3.7f))*warm;
        lp.radius = faulty? 13.0f : (18.0f+rng.f()*6.0f);
        lp.phase = rng.f()*6.28f;
        lp.rate  = 6.0f + rng.f()*12.0f;
        lp.faulty= faulty;
        lp.dead  = dead;
        lp.drawIndex = panelIdx;
        lamps.push_back(lp);
    }

    // -------- handwritten wall markings (environmental storytelling) --------
    // Real ink decals attached FLUSH to wall faces (0.02m proud, no gap).
    // Context-aware: more frequent near junctions / dead ends.
    {
        int placed=0, target=48;
        for(int r=1;r<RW-1 && placed<target;r++) for(int c=1;c<CW-1 && placed<target;c++){
            if(!bmap::isWall(r,c)) continue;
            if(rng.f()>0.12f) continue;
            glm::vec3 ctr=bmap::cellCenter(r,c);
            float half=bmap::CELL*0.5f+0.02f; // flush against the wall surface
            struct F{int dr,dc; glm::vec3 off; float rotY;};
            F faces[4]={
                {0,1, glm::vec3(half,0,0), -90.0f},
                {0,-1,glm::vec3(-half,0,0), 90.0f},
                {1,0, glm::vec3(0,0,half), 180.0f},
                {-1,0,glm::vec3(0,0,-half), 0.0f},
            };
            for(int k=0;k<4 && placed<target;k++){
                F&f=faces[k];
                if(bmap::isWall(r+f.dr,c+f.dc)) continue;
                tex::Material* sg = signList[rng.range(signCount)];
                // human eye-level placement with natural variance
                float yh = 1.35f + (rng.f()-0.5f)*0.7f;
                float sz = 1.0f + rng.f()*0.6f;
                glm::mat4 mm=glm::translate(glm::mat4(1), ctr+f.off+glm::vec3(0,yh,0));
                mm=glm::rotate(mm, glm::radians(f.rotY), glm::vec3(0,1,0));
                // slight random tilt - rushed, not perfectly aligned
                mm=glm::rotate(mm, glm::radians((rng.f()-0.5f)*8.0f), glm::vec3(0,0,1));
                mm=glm::scale(mm, glm::vec3(sz, sz, 1.0f));
                DrawCmd d{mm,*sg, glm::vec2(1,1), glm::vec3(1), 0.0f, MK_PLANE, false};
                d.decal=true;
                d.center=ctr+f.off+glm::vec3(0,yh,0);
                d.bound=sz;
                draws.push_back(d);
                placed++;
                break;
            }
        }
    }

    // -------- props (placed ONLY in valid open cells, grounded) --------
    auto openCell=[&](int& rr,int& cc)->bool{
        for(int tries=0;tries<200;tries++){
            int r=2+rng.range(RW-4), c=2+rng.range(CW-4);
            if(!bmap::isWall(r,c)){ rr=r; cc=c; return true; }
        }
        return false;
    };
    auto cellFloorPos=[&](int r,int c){ glm::vec3 ctr=bmap::cellCenter(r,c); return glm::vec3(ctr.x,0,ctr.z); };

    // helper: solid prop -> draw + collision obstacle
    auto pushSolid=[&](glm::vec3 p,glm::vec3 s,tex::Material m,int mesh,glm::vec2 uv,
                       glm::vec3 tint,float halfX,float halfZ,float top){
        pushBox(p,s,m,mesh,uv,tint,0.0f,true);
        bmap::addObstacle(p.x,p.z,halfX,halfZ,top);
    };

    int numClusters=34;
    for(int k=0;k<numClusters;k++){
        int r,c; if(!openCell(r,c)) continue;
        glm::vec3 base=cellFloorPos(r,c);
        int type=rng.range(7);
        if(type==0){ // stacked cardboard boxes (collidable stack)
            int n=2+rng.range(3);
            float maxr=0;
            for(int i=0;i<n;i++){
                float s=0.7f+rng.f()*0.4f;
                glm::vec3 off((rng.f()-0.5f)*0.7f,0,(rng.f()-0.5f)*0.7f);
                pushBox(base+off+glm::vec3(0,s*0.5f+i*0.78f*0.0f,0), glm::vec3(s,s*0.8f,s*0.9f),
                        matCardboard, MK_BEVEL, glm::vec2(1,1), glm::vec3(0.9f+0.2f*rng.f()),0.0f,true);
                maxr=std::max(maxr,s*0.5f);
            }
            bmap::addObstacle(base.x,base.z,maxr+0.3f,maxr+0.3f,1.0f);
        } else if(type==1){ // detailed metal barrel(s)
            int n=1+rng.range(2);
            for(int i=0;i<n;i++){
                glm::vec3 off(i*0.9f-(n-1)*0.45f,0,0);
                bool rusty=rng.f()<0.5f;
                pushSolid(base+off+glm::vec3(0,0.55f,0), glm::vec3(0.9f,1.1f,0.9f),
                        rusty?matRust:matMetal, MK_BARREL, glm::vec2(1,1),
                        rusty?glm::vec3(0.85f):glm::vec3(0.6f,0.62f,0.66f), 0.48f,0.48f,1.1f);
            }
        } else if(type==2){ // wooden pallet + crate
            pushBox(base+glm::vec3(0,0.08f,0), glm::vec3(1.6f,0.16f,1.1f),
                    matWood, MK_CUBE, glm::vec2(2,1), glm::vec3(1),0.0f,true);
            pushSolid(base+glm::vec3(0.2f,0.5f,0.1f), glm::vec3(0.9f,0.8f,0.8f),
                    matWood, MK_BEVEL, glm::vec2(1,1), glm::vec3(0.95f), 0.5f,0.45f,0.9f);
        } else if(type==3){ // exposed vertical pipe against nearby wall
            glm::vec3 off(0,0,0);
            if(bmap::isWall(r,c+1)) off=glm::vec3(bmap::CELL*0.42f,0,0);
            else if(bmap::isWall(r,c-1)) off=glm::vec3(-bmap::CELL*0.42f,0,0);
            else if(bmap::isWall(r+1,c)) off=glm::vec3(0,0,bmap::CELL*0.42f);
            else off=glm::vec3(0,0,-bmap::CELL*0.42f);
            pushSolid(base+off+glm::vec3(0,H*0.5f,0), glm::vec3(0.24f,H,0.24f),
                    matSteel, MK_CYL, glm::vec2(1,1), glm::vec3(0.6f), 0.18f,0.18f,H);
        } else if(type==4){ // plastic chair (upright, collidable)
            glm::vec3 p=base; float rot=rng.f()*6.28f;
            glm::mat4 R=glm::rotate(glm::mat4(1),rot,glm::vec3(0,1,0));
            auto place=[&](glm::vec3 lp,glm::vec3 ls){
                glm::mat4 mm=glm::translate(glm::mat4(1),p)*R*glm::translate(glm::mat4(1),lp);
                mm=glm::scale(mm,ls);
                DrawCmd d{mm,matPlastic,glm::vec2(1,1),glm::vec3(0.2f,0.22f,0.26f),0.0f,MK_BEVEL,true};
                d.center=p+lp; d.bound=glm::length(ls)*0.5f+0.4f; draws.push_back(d);
            };
            place(glm::vec3(0,0.45f,0),glm::vec3(0.5f,0.06f,0.5f));
            place(glm::vec3(0,0.75f,-0.22f),glm::vec3(0.5f,0.5f,0.06f));
            for(float sx=-1;sx<=1;sx+=2)for(float sz=-1;sz<=1;sz+=2)
                place(glm::vec3(sx*0.2f,0.22f,sz*0.2f),glm::vec3(0.05f,0.45f,0.05f));
            bmap::addObstacle(base.x,base.z,0.35f,0.35f,0.9f);
        } else if(type==5){ // storage locker / cabinet (detailed multi-part)
            float rot=(float)(rng.range(4))*1.5708f;
            glm::mat4 mm=glm::translate(glm::mat4(1),base);
            mm=glm::rotate(mm,rot,glm::vec3(0,1,0));
            DrawCmd d{mm,matMetal,glm::vec2(1,1),glm::vec3(0.62f,0.63f,0.66f),0.0f,MK_LOCKER,true};
            d.center=base+glm::vec3(0,1,0); d.bound=2.4f; draws.push_back(d);
            bmap::addObstacle(base.x,base.z,0.55f,0.5f,2.0f);
        } else { // scattered single crate / debris
            float s=0.6f+rng.f()*0.5f;
            pushSolid(base+glm::vec3(0,s*0.5f,0), glm::vec3(s),
                    matCardboard, MK_BEVEL, glm::vec2(1,1), glm::vec3(0.85f+0.3f*rng.f()),
                    s*0.5f+0.1f,s*0.5f+0.1f,s);
        }
    }

    // -------- security camera (CCTV) system --------
    // A limited number of wall/ceiling-mounted cameras that pan to
    // track the player when in line of sight. Each has a mount, arm,
    // body, lens and a status LED, plus a servo motor sound.
    std::vector<SecCam> cams;
    {
        int target=8;            // limited, not excessive
        int tries=0;
        while((int)cams.size()<target && tries<2000){
            tries++;
            int r=2+rng.range(RW-4), c=2+rng.range(CW-4);
            if(bmap::isWall(r,c)) continue;
            // must have an adjacent wall to mount on (corner of a room)
            int wdr=0,wdc=0; bool found=false;
            if(bmap::isWall(r,c+1)){wdc=1;found=true;}
            else if(bmap::isWall(r,c-1)){wdc=-1;found=true;}
            else if(bmap::isWall(r+1,c)){wdr=1;found=true;}
            else if(bmap::isWall(r-1,c)){wdr=-1;found=true;}
            if(!found) continue;
            glm::vec3 ctr=bmap::cellCenter(r,c);
            glm::vec3 mount=ctr + glm::vec3(wdc*(bmap::CELL*0.45f),H-0.55f, wdr*(bmap::CELL*0.45f));
            // ensure not too close to another camera
            bool tooClose=false;
            for(auto&cc:cams){ if(glm::length(cc.base-mount)<bmap::CELL*4.0f){tooClose=true;break;} }
            if(tooClose) continue;

            SecCam cam;
            cam.base=mount;
            cam.baseYaw=atan2f(-wdr,-wdc); // face into the room
            cam.yaw=cam.baseYaw; cam.targetYaw=cam.baseYaw;
            cam.sweepPhase=rng.f()*6.28f;
            // mount bracket (static)
            pushBox(mount+glm::vec3(0,0.18f,0), glm::vec3(0.18f,0.36f,0.18f),
                    matCamDark, MK_CYL, glm::vec2(1,1), glm::vec3(0.5f),0.0f,false);
            // arm (animated)
            glm::mat4 arm=glm::mat4(1);
            cam.armIdx=(int)draws.size();
            { DrawCmd d{arm,matCamBody,glm::vec2(1,1),glm::vec3(0.8f),0.0f,MK_CUBE,false};
              d.center=mount; d.bound=1.0f; draws.push_back(d); }
            // body (animated)
            cam.bodyIdx=(int)draws.size();
            { DrawCmd d{arm,matCamBody,glm::vec2(1,1),glm::vec3(0.85f,0.85f,0.88f),0.0f,MK_BEVEL,false};
              d.center=mount; d.bound=1.0f; draws.push_back(d); }
            // lens housing (animated, dark cylinder barrel)
            cam.lensIdx=(int)draws.size();
            { DrawCmd d{arm,matCamDark,glm::vec2(1,1),glm::vec3(0.08f,0.09f,0.11f),0.0f,MK_CYL,false};
              d.center=mount; d.bound=1.0f; draws.push_back(d); }
            // glass lens element (animated, glossy dark glass with slight glint)
            cam.glassIdx=(int)draws.size();
            { DrawCmd d{arm,matCamGlass,glm::vec2(1,1),glm::vec3(0.04f,0.06f,0.12f),0.12f,MK_SPHERE,false};
              d.center=mount; d.bound=1.0f; draws.push_back(d); }
            // status LED (animated, emissive)
            cam.ledIdx=(int)draws.size();
            { DrawCmd d{arm,matLED,glm::vec2(1,1),glm::vec3(1.0f,0.1f,0.1f),2.0f,MK_SPHERE,false};
              d.center=mount; d.bound=1.0f; draws.push_back(d); }
            // small collision so the player can't walk through the mount
            bmap::addObstacle(mount.x,mount.z,0.2f,0.2f,H);
            cams.push_back(cam);
        }
    }

    // -------- DOORS (purple teleport + brown exit) --------
    // Doors are mounted flush into wall faces that border open cells.
    // Purple doors teleport the player to a random open cell (35%
    // discovery weighting). Brown doors are the level exit / end —
    // exactly 3 exist on the map (25% discovery weighting). Each door
    // gets a soft emissive aura so it is findable, plus small floor
    // markers (placed later) hint toward them.
    std::vector<Door> doors;
    {
        // collect candidate (open cell, adjacent wall face) sites
        struct Site{int r,c,wdr,wdc;};
        std::vector<Site> sites;
        for(int r=2;r<RW-2;r++) for(int c=2;c<CW-2;c++){
            if(bmap::isWall(r,c)) continue;
            if(bmap::isWall(r,c+1)) sites.push_back({r,c,0,1});
            else if(bmap::isWall(r,c-1)) sites.push_back({r,c,0,-1});
            else if(bmap::isWall(r+1,c)) sites.push_back({r,c,1,0});
            else if(bmap::isWall(r-1,c)) sites.push_back({r,c,-1,0});
        }
        // shuffle sites deterministically
        for(int i=(int)sites.size()-1;i>0;i--){ int j=rng.range(i+1); std::swap(sites[i],sites[j]); }

        auto placeDoor=[&](const Site& s, DoorKind kind)->bool{
            glm::vec3 ctr=bmap::cellCenter(s.r,s.c);
            // spacing: keep doors apart
            for(auto&d:doors) if(glm::length(glm::vec3(d.pos.x-ctr.x,0,d.pos.z-ctr.z))<bmap::CELL*3.0f) return false;
            float half=bmap::CELL*0.5f+0.02f;
            glm::vec3 off(s.wdc*half,0,s.wdr*half);
            float rotY; // door faces into the room (away from wall)
            if(s.wdc==1) rotY=-90.0f; else if(s.wdc==-1) rotY=90.0f;
            else if(s.wdr==1) rotY=180.0f; else rotY=0.0f;
            glm::vec3 dpos=ctr+off+glm::vec3(0,1.15f,0);
            glm::mat4 mm=glm::translate(glm::mat4(1),dpos);
            mm=glm::rotate(mm,glm::radians(rotY),glm::vec3(0,1,0));
            mm=glm::scale(mm,glm::vec3(2.4f,2.3f,2.4f));
            tex::Material& dm = (kind==DOOR_TELEPORT)?matDoorPurple:matDoorBrown;
            glm::vec3 tint = (kind==DOOR_TELEPORT)?glm::vec3(1.05f,0.95f,1.15f):glm::vec3(1.0f);
            DrawCmd d{mm,dm,glm::vec2(1,1),tint,0.0f,MK_DOOR,true};
            d.center=dpos; d.bound=2.6f;
            int di=(int)draws.size(); draws.push_back(d);
            // emissive aura plane just in front of the door (findability)
            glm::vec3 apos=ctr+glm::vec3(s.wdc*(half-0.18f),1.15f,s.wdr*(half-0.18f));
            glm::mat4 am=glm::translate(glm::mat4(1),apos);
            am=glm::rotate(am,glm::radians(rotY),glm::vec3(0,1,0));
            am=glm::scale(am,glm::vec3(2.0f,2.2f,1.0f));
            glm::vec3 aura=(kind==DOOR_TELEPORT)?glm::vec3(0.55f,0.20f,0.85f):glm::vec3(0.75f,0.45f,0.18f);
            DrawCmd ad{am,matLampPanel,glm::vec2(1,1),aura,0.6f,MK_PLANE,false};
            ad.decal=false; ad.center=apos; ad.bound=2.2f;
            int ai=(int)draws.size(); draws.push_back(ad);
            Door door; door.pos=dpos; door.kind=kind; door.row=s.r; door.col=s.c;
            door.drawIdx=di; door.auraIdx=ai;
            doors.push_back(door);
            return true;
        };

        // 3 brown EXIT doors, then a set of purple TELEPORT doors.
        // 25% / 35% discovery weighting -> realised as relative counts.
        int exitTarget=3, teleTarget=5;
        int si=0;
        // exits first (spread out)
        for(;si<(int)sites.size() && (int)0<exitTarget;){
            int placedExit=0;
            for(; si<(int)sites.size() && placedExit<exitTarget; si++){
                if(placeDoor(sites[si],DOOR_EXIT)) placedExit++;
            }
            break;
        }
        // teleport doors
        int placedTel=0;
        for(; si<(int)sites.size() && placedTel<teleTarget; si++){
            if(placeDoor(sites[si],DOOR_TELEPORT)) placedTel++;
        }
        // register collision footprints (doors are solid until used)
        for(auto&d:doors) bmap::addObstacle(d.pos.x,d.pos.z,0.6f,0.6f,H);

        // small floor markers (faint glowing chips) leading toward each
        // door so the player has subtle visual clues to find them.
        for(auto&d:doors){
            glm::vec3 base(d.pos.x,0.02f,d.pos.z);
            glm::vec3 toRoom = glm::normalize(glm::vec3(bmap::cellCenter(d.row,d.col).x-d.pos.x,0,
                                                        bmap::cellCenter(d.row,d.col).z-d.pos.z)+glm::vec3(0.001f));
            glm::vec3 mc=(d.kind==DOOR_TELEPORT)?glm::vec3(0.55f,0.20f,0.85f):glm::vec3(0.80f,0.45f,0.18f);
            for(int m=1;m<=3;m++){
                glm::vec3 mp=base+toRoom*(float)m*1.1f;
                glm::mat4 mm=glm::translate(glm::mat4(1),glm::vec3(mp.x,0.025f,mp.z));
                mm=glm::rotate(mm,glm::radians(90.0f),glm::vec3(1,0,0));
                mm=glm::scale(mm,glm::vec3(0.35f,0.35f,1.0f));
                DrawCmd md{mm,matLampPanel,glm::vec2(1,1),mc,0.5f,MK_PLANE,false};
                md.center=glm::vec3(mp.x,0.025f,mp.z); md.bound=0.5f;
                draws.push_back(md);
            }
        }
    }

    // open cells list (for teleport destinations + statue placement)
    std::vector<glm::ivec2> openCells;
    for(int r=2;r<RW-2;r++) for(int c=2;c<CW-2;c++)
        if(!bmap::isWall(r,c)) openCells.push_back({r,c});

    // -------- STATUES (disturbing humanoid figures) --------
    // Fixed (non-random) spawn positions chosen deterministically from
    // the open-cell list with even spacing. Most are static & creepy;
    // a few are chasers. Kept deliberately sparse (not too many).
    std::vector<Statue> statues;
    {
        int total = 7;            // sparse: a handful across the whole map
        int chasers = 3;          // only a few actually hunt
        std::vector<glm::ivec2> picks;
        // pick spaced-out cells deterministically
        int guard=0;
        while((int)picks.size()<total && guard<4000){
            guard++;
            if(openCells.empty()) break;
            glm::ivec2 cell=openCells[rng.range((int)openCells.size())];
            glm::vec3 ctr=bmap::cellCenter(cell.x,cell.y);
            bool ok=true;
            for(auto&p:picks){ glm::vec3 pc=bmap::cellCenter(p.x,p.y);
                if(glm::length(pc-ctr)<bmap::CELL*5.0f){ok=false;break;} }
            // keep away from doors and spawn
            for(auto&d:doors) if(glm::length(glm::vec3(d.pos.x-ctr.x,0,d.pos.z-ctr.z))<bmap::CELL*3.0f) ok=false;
            if(glm::length(glm::vec3(ctr.x-gPlayer.pos.x,0,ctr.z-gPlayer.pos.z))<bmap::CELL*5.0f) ok=false;
            if(ok) picks.push_back(cell);
        }
        for(int i=0;i<(int)picks.size();i++){
            glm::vec3 ctr=bmap::cellCenter(picks[i].x,picks[i].y);
            Statue st;
            st.basePos=glm::vec3(ctr.x,0,ctr.z);
            st.pos=st.basePos;
            st.chaser=(i<chasers);
            st.yaw=rng.f()*6.28f;
            st.glanceTimer=2.0f+rng.f()*6.0f;
            // body
            glm::mat4 mm=glm::translate(glm::mat4(1),st.pos);
            mm=glm::rotate(mm,st.yaw,glm::vec3(0,1,0));
            { DrawCmd d{mm,matStatue,glm::vec2(1,1),glm::vec3(0.9f),0.0f,MK_STATUE,true};
              d.center=st.pos+glm::vec3(0,1,0); d.bound=2.6f; st.drawIdx=(int)draws.size(); draws.push_back(d); }
            // glowing eyes (two small emissive spheres on the head)
            for(int e=0;e<2;e++){
                DrawCmd d{glm::mat4(1),matEye,glm::vec2(1,1),glm::vec3(1.0f,0.15f,0.12f),
                          st.chaser?2.2f:1.0f,MK_SPHERE,false};
                d.center=st.pos; d.bound=0.3f;
                if(e==0) st.eyeLIdx=(int)draws.size(); else st.eyeRIdx=(int)draws.size();
                draws.push_back(d);
            }
            // collision footprint
            bmap::addObstacle(st.pos.x,st.pos.z,0.45f,0.45f,2.0f);
            statues.push_back(st);
        }
    }

    fprintf(stderr,"[diag] draws=%zu lamps=%zu cams=%zu doors=%zu statues=%zu obstacles=%zu world=%.1fx%.1f grid=%dx%d\n",
            draws.size(), lamps.size(), cams.size(), doors.size(), statues.size(), bmap::obstacles().size(), W, D, CW, RW);

    // ---- shadow map FBO ----
    const int SHADOW=2048;
    GLuint depthFBO, depthTex;
    glGenFramebuffers(1,&depthFBO);
    glGenTextures(1,&depthTex);
    glBindTexture(GL_TEXTURE_2D,depthTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,SHADOW,SHADOW,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
    float border[4]={1,1,1,1}; glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
    glBindFramebuffer(GL_FRAMEBUFFER,depthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthTex,0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    // ---- HDR scene FBO ----
    GLuint hdrFBO=0, hdrColor=0, hdrDepth=0;
    auto buildHDR=[&](int w,int h){
        if(hdrFBO) glDeleteFramebuffers(1,&hdrFBO);
        if(hdrColor) glDeleteTextures(1,&hdrColor);
        if(hdrDepth) glDeleteRenderbuffers(1,&hdrDepth);
        glGenFramebuffers(1,&hdrFBO); glBindFramebuffer(GL_FRAMEBUFFER,hdrFBO);
        glGenTextures(1,&hdrColor); glBindTexture(GL_TEXTURE_2D,hdrColor);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hdrColor,0);
        glGenRenderbuffers(1,&hdrDepth); glBindRenderbuffer(GL_RENDERBUFFER,hdrDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,hdrDepth);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    };
    buildHDR(FB_W,FB_H);

    // ---- ping-pong bloom FBOs ----
    GLuint pingFBO[2], pingTex[2];
    auto buildPing=[&](int w,int h){
        glGenFramebuffers(2,pingFBO); glGenTextures(2,pingTex);
        for(int i=0;i<2;i++){
            glBindFramebuffer(GL_FRAMEBUFFER,pingFBO[i]);
            glBindTexture(GL_TEXTURE_2D,pingTex[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,pingTex[i],0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    };
    int curBloomW=FB_W/2, curBloomH=FB_H/2;
    buildPing(curBloomW,curBloomH);

    // ---- fullscreen quad ----
    float quad[]={ -1,-1,0,0,  1,-1,1,0,  1,1,1,1,  -1,-1,0,0,  1,1,1,1,  -1,1,0,1 };
    GLuint qvao,qvbo; glGenVertexArrays(1,&qvao); glBindVertexArray(qvao);
    glGenBuffers(1,&qvbo); glBindBuffer(GL_ARRAY_BUFFER,qvbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // ---- place player in an open corridor ----
    {
#ifdef SHOT_TEST
        // optional debug spawn override: SHOT_X SHOT_Z SHOT_YAW env vars
        const char* sx=getenv("SHOT_X"); const char* sz=getenv("SHOT_Z"); const char* syaw=getenv("SHOT_YAW");
        if(sx&&sz){ gPlayer.pos=glm::vec3(atof(sx),1.7f,atof(sz)); gPlayer.yaw=syaw?atof(syaw):0.0f; }
        else
#endif
        {
        bool placed=false;
        for(int r=1;r<RW-1 && !placed;r++)
            for(int c=1;c<CW-1 && !placed;c++)
                if(!bmap::isWall(r,c) && !bmap::isWall(r,c+1) && !bmap::isWall(r,c+2)){
                    glm::vec3 p=bmap::cellCenter(r,c);
                    gPlayer.pos=glm::vec3(p.x,1.7f,p.z);
                    gPlayer.yaw=0.0f;
                    placed=true;
                }
        }
    }

    // ---- audio + UI systems ----
    GLuint progUI = program(VS_UI, FS_UI);
    ui::Renderer gui; gui.init(progUI);
    if(gAudio.init()){
        gAudio.setMasterVolume(gSet.masterVol);
        gAudio.setMuted(gSet.muted);
        // persistent ambience beds (centred, non-positional)
        gAudio.setLoop(audio::LOOP_AMB,  gAudio.ambience(), 0.55f, 0.0f);
        gAudio.setLoop(audio::LOOP_VENT, gAudio.vent(),     0.22f, 0.0f);
        gAudio.setLoop(audio::LOOP_HUM,  gAudio.hum(),      0.0f,  0.0f); // modulated per-frame
        fprintf(stderr,"[audio] engine started\n");
    } else {
        fprintf(stderr,"[audio] engine unavailable (silent)\n");
    }

    // ---- lighting setup (atmospheric, oppressive but readable) ----
    // The directional "sun" is a very dim cool fill (this is an interior
    // with no sky); the fluorescents do the real lighting work.
    glm::vec3 sunDir = glm::normalize(glm::vec3(-0.32f,-1.0f,-0.22f));
    glm::vec3 sunColor = glm::vec3(0.16f,0.16f,0.15f); // dim neutral fill
    glm::vec3 ambient = glm::vec3(0.060f,0.058f,0.048f); // base ambient
    glm::vec3 ambSky  = glm::vec3(0.115f,0.110f,0.082f); // warm ceiling bounce
    glm::vec3 ambGround=glm::vec3(0.050f,0.047f,0.034f); // floor bounce
    glm::vec3 fogColor= glm::vec3(0.072f,0.068f,0.048f);
    float fogDensity  = 0.020f;
    float prevStep    = 0.0f;  // footstep accumulator tracking

    double prev=glfwGetTime();
    int curFbW=FB_W, curFbH=FB_H;
    double fpsTimer=prev; int fpsFrames=0;

    while(!glfwWindowShouldClose(win) && !gQuit){
        double now=glfwGetTime();
        float dt=(float)(now-prev); prev=now;
        if(dt>0.05f) dt=0.05f;
        glfwPollEvents();

        fpsFrames++;
        if(now-fpsTimer>=0.5){
            char title[128];
            snprintf(title,128,"BACKROOMS - Level 0   |   %.0f FPS   |   Stamina %.0f%%%s",
                     fpsFrames/(now-fpsTimer), gPlayer.stamina*100.0f,
                     gFlashOn?"   [Flashlight]":"");
            glfwSetWindowTitle(win,title);
            fpsTimer=now; fpsFrames=0;
        }

        if(FB_W!=curFbW || FB_H!=curFbH){
            curFbW=FB_W; curFbH=FB_H;
            buildHDR(FB_W,FB_H);
            glDeleteFramebuffers(2,pingFBO); glDeleteTextures(2,pingTex);
            curBloomW=FB_W/2; curBloomH=FB_H/2; buildPing(curBloomW,curBloomH);
        }

        bool paused = (gState!=GS_PLAY);

        // ---- Enter edge-detect for menu activation ----
        static bool enterPrev=false;
        bool enterNow = (glfwGetKey(win,GLFW_KEY_ENTER)==GLFW_PRESS) ||
                        (glfwGetKey(win,GLFW_KEY_KP_ENTER)==GLFW_PRESS);
        bool enterPressed = enterNow && !enterPrev;
        enterPrev = enterNow;

        // ---- input (only when playing) ----
        glm::vec3 wish(0);
        bool sprintKey=false, jump=false;
        if(!paused && gCaptured){
            if(glfwGetKey(win,GLFW_KEY_W)==GLFW_PRESS) wish += gPlayer.frontFlat();
            if(glfwGetKey(win,GLFW_KEY_S)==GLFW_PRESS) wish -= gPlayer.frontFlat();
            if(glfwGetKey(win,GLFW_KEY_D)==GLFW_PRESS) wish += gPlayer.right();
            if(glfwGetKey(win,GLFW_KEY_A)==GLFW_PRESS) wish -= gPlayer.right();
            sprintKey = (glfwGetKey(win,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS)||
                        (glfwGetKey(win,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS);
            jump   = glfwGetKey(win,GLFW_KEY_SPACE)==GLFW_PRESS;
        }
        bool sprinting=false;
        if(!paused){
            sprinting = gPlayer.update(dt, wish, sprintKey, jump);
            gPlayer.addXP(dt*2.0f); // passive exploration XP
        }

        glm::vec3 eye;
        glm::mat4 view=gPlayer.view(eye);
        float fov = 70.0f + gPlayer.sprintBlend*8.0f; // visual sprint bonus
        glm::mat4 proj=glm::perspective(glm::radians(fov),
                        (float)FB_W/(float)FB_H, 0.05f, 240.0f);
        glm::mat4 viewProj = proj*view;

        // ---- update lamp flicker (each light behaves independently) ----
        for(auto& lp: lamps){
            float f;
            if(lp.dead){
                f = 0.0f; // failed light -> area stays dark
            } else if(lp.faulty){
                // stuttering near-dead tube
                float s = sinf((float)now*lp.rate + lp.phase);
                float n = sinf((float)now*lp.rate*3.7f + lp.phase*2.3f);
                f = (s*0.5f+0.5f);
                f = (f>0.45f)?(0.85f+0.15f*n):0.10f; // mostly off, blinks on
                // NOTE: the sharp electrical "zap/snap" sound is intentionally
                // removed (sharp / annoying SFX disabled per design). The
                // visual flicker remains, but it is now silent.
            } else {
                // gentle 60Hz-ish hum + occasional dip
                float hum = 0.96f + 0.04f*sinf((float)now*90.0f + lp.phase);
                float dip = (sinf((float)now*0.7f + lp.phase)>0.985f)?0.5f:1.0f;
                f = hum*dip;
            }
            lp.curFlickerCache = f;
            draws[lp.drawIndex].emissive = (lp.dead?0.05f:4.2f)*f;
        }

        // ---- security cameras: detect + track player, animate parts ----
        bool anyServo=false; float servoPan=0.0f, servoGain=0.0f;
        for(auto& cam: cams){
            glm::vec3 toP = eye - cam.base;
            float dist = glm::length(toP);
            float desiredYaw;
            bool see=false;
            if(dist < 24.0f){
                glm::vec3 dir=toP/std::max(dist,0.001f);
                see=true;
                for(float t=0.6f;t<dist;t+=0.6f){
                    glm::vec3 sp=cam.base+dir*t;
                    int cc=(int)floorf(sp.x/bmap::CELL), rr=(int)floorf(sp.z/bmap::CELL);
                    if(bmap::isWall(rr,cc)){ see=false; break; }
                }
            }
            if(see){
                desiredYaw=atan2f(toP.z,toP.x);
                cam.tracking=true;
            } else {
                cam.sweepPhase += dt*0.4f;
                desiredYaw=cam.baseYaw + sinf(cam.sweepPhase)*0.7f;
                cam.tracking=false;
            }
            float dy=desiredYaw-cam.yaw;
            while(dy> 3.14159f) dy-=6.2831853f;
            while(dy<-3.14159f) dy+=6.2831853f;
            float turn=std::clamp(dy, -1.6f*dt, 1.6f*dt);
            if(!paused) cam.yaw += turn;
            bool moving=fabsf(turn)>0.0008f;

            glm::mat4 M=glm::translate(glm::mat4(1),cam.base);
            M=glm::rotate(M, -cam.yaw, glm::vec3(0,1,0));
            // slim articulated arm
            { glm::mat4 a=M*glm::translate(glm::mat4(1),glm::vec3(0.28f,-0.04f,0));
              a=glm::scale(a,glm::vec3(0.56f,0.10f,0.10f)); draws[cam.armIdx].model=a; }
            // tapered dome-ish body (beveled box)
            { glm::mat4 b=M*glm::translate(glm::mat4(1),glm::vec3(0.60f,-0.04f,0));
              b=glm::scale(b,glm::vec3(0.46f,0.32f,0.30f)); draws[cam.bodyIdx].model=b; }
            // protruding lens barrel (front of body, along the view axis)
            { glm::mat4 l=M*glm::translate(glm::mat4(1),glm::vec3(0.86f,-0.04f,0));
              l=glm::rotate(l,1.5708f,glm::vec3(0,0,1));
              l=glm::scale(l,glm::vec3(0.16f,0.22f,0.16f)); draws[cam.lensIdx].model=l; }
            // glass element at the very front of the barrel
            { glm::mat4 gm=M*glm::translate(glm::mat4(1),glm::vec3(0.98f,-0.04f,0));
              gm=glm::scale(gm,glm::vec3(0.15f,0.15f,0.15f)); draws[cam.glassIdx].model=gm; }
            { glm::mat4 e=M*glm::translate(glm::mat4(1),glm::vec3(0.60f,0.14f,0.10f));
              e=glm::scale(e,glm::vec3(0.055f)); draws[cam.ledIdx].model=e; }
            cam.indicatorPulse+=dt*(cam.tracking?6.0f:1.5f);
            float led=cam.tracking? (1.6f+0.6f*sinf(cam.indicatorPulse))
                                  : (0.4f+0.6f*(sinf(cam.indicatorPulse)>0?1.0f:0.0f));
            draws[cam.ledIdx].emissive=led;
            draws[cam.ledIdx].tint = cam.tracking? glm::vec3(1.0f,0.05f,0.05f):glm::vec3(0.8f,0.2f,0.05f);

            if(moving && dist<16.0f){
                float g=0.35f*(1.0f-dist/16.0f);
                if(g>servoGain){
                    servoGain=g; anyServo=true;
                    glm::vec3 d=glm::normalize(glm::vec3(toP.x,0,toP.z));
                    servoPan=std::clamp(glm::dot(d,gPlayer.right()),-1.0f,1.0f);
                }
            }
        }
        if(gAudio.ok()){
            if(anyServo) gAudio.setLoop(audio::LOOP_SERVO,gAudio.servo(),servoGain,servoPan);
            else gAudio.stopLoop(audio::LOOP_SERVO);
        }

        // =========================================================
        //  DOORS: pulse aura, detect proximity, teleport / win
        // =========================================================
        static int  gNearDoorKind=-1;   // -1 none, 0 teleport, 1 exit (for HUD)
        static float gNearDoorDist=1e9f;
        static bool  gWon=false;
        gNearDoorKind=-1; gNearDoorDist=1e9f;
        for(auto& dr: doors){
            float dx=dr.pos.x-eye.x, dz=dr.pos.z-eye.z;
            float dist=sqrtf(dx*dx+dz*dz);
            // pulsing emissive aura so doors are findable in the gloom
            float pulse=0.45f+0.35f*sinf((float)now*2.2f + (dr.kind==DOOR_TELEPORT?0.0f:1.5f));
            float prox=std::clamp(1.0f-dist/9.0f,0.0f,1.0f);
            draws[dr.auraIdx].emissive=(0.4f+1.4f*prox)*pulse;
            // door slab gets a faint rim glow too
            draws[dr.drawIdx].emissive=0.05f+0.25f*prox*pulse;
            if(dist<gNearDoorDist){ gNearDoorDist=dist; gNearDoorKind=(int)dr.kind; }
            // interaction trigger (walk into the doorway)
            if(!paused && dist<1.9f && !dr.used){
                if(dr.kind==DOOR_TELEPORT){
                    // teleport to a random open cell
                    if(!openCells.empty()){
                        glm::ivec2 cell=openCells[rng.range((int)openCells.size())];
                        glm::vec3 ctr=bmap::cellCenter(cell.x,cell.y);
                        gPlayer.pos=glm::vec3(ctr.x,gPlayer.baseEye,ctr.z);
                        gPlayer.vel=glm::vec3(0); gPlayer.eyeOffset=0.0f; gPlayer.vY=0.0f;
                        if(gAudio.ok()) gAudio.zap(0.4f,0.0f);
                        dr.used=true; dr.glow=1.0f;
                    }
                } else { // EXIT / END door -> win
                    gWon=true;
                    gState=GS_PAUSE_MAIN; gCaptured=false; gMenuSel=0;
                    glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
                }
            }
            // re-arm a used teleport door once the player walks away
            if(dr.used && dr.kind==DOOR_TELEPORT && dist>4.0f) dr.used=false;
        }

        // =========================================================
        //  STATUES: static dread + chasers that freeze when watched
        // =========================================================
        {
            glm::vec3 fwd=gPlayer.front();
            for(auto& st: statues){
                glm::vec3 toS=st.pos-eye;
                float dist=glm::length(glm::vec3(toS.x,0,toS.z));
                glm::vec3 dir=(dist>0.001f)?glm::normalize(glm::vec3(toS.x,0,toS.z)):glm::vec3(0,0,1);
                // is the player looking roughly at this statue (and LOS clear)?
                float facing=glm::dot(glm::normalize(glm::vec3(fwd.x,0,fwd.z)),dir);
                bool looked = facing>0.86f && dist<26.0f;
                if(looked){
                    // line-of-sight check
                    for(float t=0.6f;t<dist;t+=0.7f){
                        glm::vec3 sp=eye+dir*t;
                        int cc=(int)floorf(sp.x/bmap::CELL), rr=(int)floorf(sp.z/bmap::CELL);
                        if(bmap::isWall(rr,cc)){ looked=false; break; }
                    }
                }

                if(st.chaser){
                    // engage if player is reasonably near
                    if(dist<14.0f) st.active=true;
                    if(dist>st.giveUpDist) st.active=false;
                    // freeze while watched; otherwise creep toward the player
                    st.frozen = looked || !st.active;
                    if(!paused && st.active && !st.frozen && dist>st.minDist){
                        glm::vec3 step=dir*st.speed*dt;
                        glm::vec3 np=st.pos+step;
                        // simple wall check (don't walk into walls)
                        int cc=(int)floorf(np.x/bmap::CELL), rr=(int)floorf(np.z/bmap::CELL);
                        if(!bmap::isWall(rr,cc)) st.pos=np;
                        st.yaw=atan2f(dir.x,dir.z); // face the player
                    } else if(st.active){
                        // even when frozen, keep facing the player (uncanny)
                        st.yaw=atan2f(dir.x,dir.z);
                    }
                    // damage if it manages to be right on top of the player
                    if(!paused && st.active && dist<st.minDist+0.4f){
                        gPlayer.health-=dt*9.0f;
                        if(gPlayer.health<0) gPlayer.health=0;
                    }
                } else {
                    // STATIC statue: usually frozen facing its rest direction,
                    // but occasionally snaps to look at the player for a beat.
                    st.frozen=true;
                    if(!paused){
                        st.glanceTimer-=dt;
                        if(st.glanceTimer<=0.0f){
                            st.turnGlance=1.4f; // hold a glance for ~1.4s
                            st.glanceTimer=6.0f+12.0f*rng.f();
                        }
                        if(st.turnGlance>0.0f){
                            st.turnGlance-=dt;
                            st.yaw=atan2f(dir.x,dir.z); // turn to look at the player
                        }
                    }
                }

                // update transforms
                glm::mat4 mm=glm::translate(glm::mat4(1),st.pos);
                mm=glm::rotate(mm,st.yaw,glm::vec3(0,1,0));
                draws[st.drawIdx].model=mm;
                draws[st.drawIdx].center=st.pos+glm::vec3(0,1,0);
                // eyes: place on the head, glow brighter for active chasers
                glm::vec3 hf=glm::vec3(sinf(st.yaw),0,cosf(st.yaw)); // facing dir
                glm::vec3 hr=glm::vec3(hf.z,0,-hf.x);                // right
                glm::vec3 head=st.pos+glm::vec3(0,2.02f,0)+hf*0.16f;
                float eyeE=(st.chaser? (st.active? (st.frozen?2.8f:1.6f):1.2f) : (st.turnGlance>0?1.6f:0.7f));
                for(int e=0;e<2;e++){
                    glm::vec3 ep=head+hr*(e==0?-0.06f:0.06f);
                    glm::mat4 em=glm::translate(glm::mat4(1),ep);
                    em=glm::scale(em,glm::vec3(0.05f));
                    int idx=(e==0)?st.eyeLIdx:st.eyeRIdx;
                    draws[idx].model=em; draws[idx].center=ep; draws[idx].emissive=eyeE;
                }
            }
        }

        // ---- dynamic audio: soft natural ceiling-lamp hum only ----
        // NOTE (per design): footstep/sprint sounds are intentionally
        // DISABLED so walking on any surface is silent. Harsh / sharp
        // ambient effects (clanks, zaps) are also removed. The only
        // positional sound left is a gentle, natural electrical hum
        // from the nearest working ceiling lamp.
        (void)prevStep; (void)sprinting;
        if(!paused && gAudio.ok()){
            float bestD=1e9f; const Lamp* nearL=nullptr;
            for(const auto& lp:lamps){ if(lp.dead) continue;
                float dx=lp.pos.x-eye.x,dz=lp.pos.z-eye.z; float d=dx*dx+dz*dz;
                if(d<bestD){bestD=d;nearL=&lp;} }
            if(nearL){
                float dist=sqrtf(bestD);
                // softer, lower gain so it sits naturally under the ambience
                float g=std::clamp(0.16f*(1.0f-dist/18.0f),0.0f,0.16f)*nearL->curFlickerCache;
                glm::vec3 d=glm::normalize(glm::vec3(nearL->pos.x-eye.x,0,nearL->pos.z-eye.z));
                float pan=std::clamp(glm::dot(d,gPlayer.right()),-1.0f,1.0f);
                // keep pitch close to natural mains hum (very little drift)
                gAudio.setLoop(audio::LOOP_HUM,gAudio.hum(),g,pan,0.99f+0.02f*nearL->curFlickerCache);
            } else gAudio.stopLoop(audio::LOOP_HUM);
        }

        // ---- choose nearest lamps (partial sort; reused storage) ----
        const int MAXL=32;
        static std::vector<std::pair<float,int>> order;
        order.clear(); order.reserve(lamps.size());
        for(size_t i=0;i<lamps.size();i++){
            float dx=eye.x-lamps[i].pos.x, dz=eye.z-lamps[i].pos.z;
            order.push_back({dx*dx+dz*dz,(int)i});
        }
        int nL=(int)std::min((size_t)MAXL, lamps.size());
        if((int)order.size()>nL)
            std::partial_sort(order.begin(),order.begin()+nL,order.end());
        else
            std::sort(order.begin(),order.end());

        // ---- shadow pass ----
        glm::vec3 center=glm::vec3(eye.x, 1.5f, eye.z);
        glm::vec3 lightPos = center - sunDir*32.0f;
        glm::mat4 lightProj=glm::ortho(-26.0f,26.0f,-26.0f,26.0f,1.0f,75.0f);
        glm::mat4 lightView=glm::lookAt(lightPos,center,glm::vec3(0,1,0));
        glm::mat4 lightSpace=lightProj*lightView;

        // shadow map resolution scales with quality setting (perf)
        int shadowRes = (gSet.quality<=0)?1024:(gSet.quality==1?1536:(gSet.quality==2?2048:2560));
        glViewport(0,0,shadowRes,shadowRes);
        glBindFramebuffer(GL_FRAMEBUFFER,depthFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(progDepth);
        glUniformMatrix4fv(depthLSLoc,1,GL_FALSE,glm::value_ptr(lightSpace));
        glCullFace(GL_FRONT); // reduce peter-panning
        // only shadow-cast objects within the shadow frustum radius
        for(size_t i=0;i<draws.size();i++){
            if(!draws[i].castShadow) continue;
            float dx=draws[i].center.x-eye.x, dz=draws[i].center.z-eye.z;
            if(dx*dx+dz*dz > 30.0f*30.0f) continue;  // shadow cull distance
            glUniformMatrix4fv(depthModelLoc,1,GL_FALSE,glm::value_ptr(draws[i].model));
            meshOf(draws[i].mesh).draw();
        }
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER,0);

        // ---- main HDR pass ----
        glViewport(0,0,FB_W,FB_H);
        glBindFramebuffer(GL_FRAMEBUFFER,hdrFBO);
        glClearColor(fogColor.r,fogColor.g,fogColor.b,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glUseProgram(progScene);
        glUniformMatrix4fv(SL.uView,1,GL_FALSE,glm::value_ptr(view));
        glUniformMatrix4fv(SL.uProj,1,GL_FALSE,glm::value_ptr(proj));
        glUniformMatrix4fv(SL.uLightSpace,1,GL_FALSE,glm::value_ptr(lightSpace));
        glUniform3fv(SL.uViewPos,1,glm::value_ptr(eye));
        glUniform3fv(SL.uSunDir,1,glm::value_ptr(sunDir));
        glUniform3fv(SL.uSunColor,1,glm::value_ptr(sunColor));
        glUniform3fv(SL.uAmbient,1,glm::value_ptr(ambient));
        glUniform3fv(SL.uAmbientSky,1,glm::value_ptr(ambSky));
        glUniform3fv(SL.uAmbientGround,1,glm::value_ptr(ambGround));
        glUniform3fv(SL.uFogColor,1,glm::value_ptr(fogColor));
        glUniform1f(SL.uFogDensity,fogDensity);
        glUniform1i(SL.uFlashOn, gFlashOn?1:0);
        glUniform3fv(SL.uFlashPos,1,glm::value_ptr(eye));
        glm::vec3 pf=gPlayer.front();
        glUniform3fv(SL.uFlashDir,1,glm::value_ptr(pf));
        glUniform1i(SL.uNumLights,nL);
        for(int i=0;i<nL;i++){
            const Lamp& lp=lamps[order[i].second];
            glUniform3fv(SL.uLightPos[i],1,glm::value_ptr(lp.pos));
            glUniform3fv(SL.uLightColor[i],1,glm::value_ptr(lp.color));
            glUniform1f(SL.uLightRadius[i],lp.radius);
            glUniform1f(SL.uLightFlicker[i],lp.curFlickerCache);
        }
        glUniform1i(SL.uAlbedo,0);
        glUniform1i(SL.uNormalMap,1);
        glUniform1i(SL.uRoughMap,2);
        glUniform1i(SL.uShadowMap,3);
        glUniform1f(SL.uExposureKey, gSet.brightness);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,depthTex);

        // extract 6 frustum planes from viewProj (for culling)
        glm::vec4 pl[6];
        {
            const glm::mat4& m=viewProj;
            pl[0]=glm::vec4(m[0][3]+m[0][0], m[1][3]+m[1][0], m[2][3]+m[2][0], m[3][3]+m[3][0]); // left
            pl[1]=glm::vec4(m[0][3]-m[0][0], m[1][3]-m[1][0], m[2][3]-m[2][0], m[3][3]-m[3][0]); // right
            pl[2]=glm::vec4(m[0][3]+m[0][1], m[1][3]+m[1][1], m[2][3]+m[2][1], m[3][3]+m[3][1]); // bottom
            pl[3]=glm::vec4(m[0][3]-m[0][1], m[1][3]-m[1][1], m[2][3]-m[2][1], m[3][3]-m[3][1]); // top
            pl[4]=glm::vec4(m[0][3]+m[0][2], m[1][3]+m[1][2], m[2][3]+m[2][2], m[3][3]+m[3][2]); // near
            pl[5]=glm::vec4(m[0][3]-m[0][2], m[1][3]-m[1][2], m[2][3]-m[2][2], m[3][3]-m[3][2]); // far
            for(int k=0;k<6;k++){ float l=glm::length(glm::vec3(pl[k])); if(l>0) pl[k]/=l; }
        }
        auto visible=[&](const DrawCmd& d)->bool{
            for(int k=0;k<6;k++){
                float dist=pl[k].x*d.center.x+pl[k].y*d.center.y+pl[k].z*d.center.z+pl[k].w;
                if(dist < -d.bound) return false;
            }
            return true;
        };

        // far distance cull radius (skip very distant geometry beyond fog)
        const float farCull2 = 95.0f*95.0f;

        // opaque pass (decals deferred for alpha blending after).
        // Texture binds are deduplicated: most adjacent draws share a
        // material, so we only re-bind the 3 textures when it changes.
        glUniform1i(SL.uUseUV2,1);
        glUniform1i(SL.uIsDecal,0);
        int drawnOpaque=0;
        GLuint lastAlb=0;
        for(size_t i=0;i<draws.size();i++){
            const DrawCmd& d=draws[i];
            if(d.decal) continue;
            float dx=d.center.x-eye.x, dz=d.center.z-eye.z;
            if(dx*dx+dz*dz > farCull2) continue;
            if(!visible(d)) continue;
            drawnOpaque++;
            glUniformMatrix4fv(SL.uModel,1,GL_FALSE,glm::value_ptr(d.model));
            glUniform1f(SL.uUVScale,d.uvScale.x);
            glUniform2fv(SL.uUVScale2,1,glm::value_ptr(d.uvScale));
            glUniform3fv(SL.uTint,1,glm::value_ptr(d.tint));
            glUniform1f(SL.uEmissive,d.emissive);
            glUniform1i(SL.uHasNormal, d.mesh==MK_PLANE?0:1);
            if(d.mat.albedo!=lastAlb){
                lastAlb=d.mat.albedo;
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,d.mat.albedo);
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,d.mat.normal);
                glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,d.mat.mrt);
            }
            meshOf(d.mesh).draw();
        }

        // decal pass: handwriting on walls (alpha-tested, double-sided)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glPolygonOffset(-1.0f,-1.0f); glEnable(GL_POLYGON_OFFSET_FILL);
        glUniform1f(SL.uUVScale,1.0f);
        glm::vec2 uvOne(1.0f);
        glUniform2fv(SL.uUVScale2,1,glm::value_ptr(uvOne));
        glUniform1i(SL.uUseUV2,1);
        glUniform1i(SL.uHasNormal,0);
        glUniform1i(SL.uIsDecal,1);
        for(size_t i=0;i<draws.size();i++){
            const DrawCmd& d=draws[i];
            if(!d.decal) continue;
            float dx=d.center.x-eye.x, dz=d.center.z-eye.z;
            if(dx*dx+dz*dz > 40.0f*40.0f) continue;
            if(!visible(d)) continue;
            glUniformMatrix4fv(SL.uModel,1,GL_FALSE,glm::value_ptr(d.model));
            glUniform3fv(SL.uTint,1,glm::value_ptr(d.tint));
            glUniform1f(SL.uEmissive,d.emissive);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,d.mat.albedo);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,d.mat.normal);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,d.mat.mrt);
            meshOf(d.mesh).draw();
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER,0);

        // ---- post: no depth / no cull ----
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        // bright pass
        glViewport(0,0,curBloomW,curBloomH);
        glBindFramebuffer(GL_FRAMEBUFFER,pingFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(progBright);
        setI(progBright,"uScene",0); setF(progBright,"uThreshold",1.05f);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,hdrColor);
        glBindVertexArray(qvao); glDrawArrays(GL_TRIANGLES,0,6);

        // blur ping-pong (pass count scales with quality)
        glUseProgram(progBlur); setI(progBlur,"uTex",0);
        bool horiz=true; int passes = gSet.bloom? (gSet.quality<=1?6:10) : 0; int src=0;
        for(int i=0;i<passes;i++){
            int dst=1-src;
            glBindFramebuffer(GL_FRAMEBUFFER,pingFBO[dst]);
            glUniform2f(glGetUniformLocation(progBlur,"uDir"), horiz?1.0f:0.0f, horiz?0.0f:1.0f);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,pingTex[src]);
            glDrawArrays(GL_TRIANGLES,0,6);
            src=dst; horiz=!horiz;
        }

        // composite to screen
        glViewport(0,0,FB_W,FB_H);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glUseProgram(progComp);
        setI(progComp,"uScene",0); setI(progComp,"uBloom",1);
        setF(progComp,"uExposure",1.25f);
        setF(progComp,"uTime", gSet.filmGrain?(float)now:0.0f);
        setF(progComp,"uBloomStrength", gSet.bloom?0.6f:0.0f);
#ifdef DEBUG_PASSTHRU
        setI(progComp,"uDebug",1);
#else
        setI(progComp,"uDebug",0);
#endif
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,hdrColor);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,pingTex[src]);
        glBindVertexArray(qvao); glDrawArrays(GL_TRIANGLES,0,6);
        glEnable(GL_FRAMEBUFFER_SRGB);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        // =========================================================
        //  UI / HUD overlay
        // =========================================================
        gui.begin(FB_W,FB_H,(float)now);
        {
            float W=(float)FB_W, Hh=(float)FB_H;
            float scale = std::min(W/1280.0f, Hh/720.0f);
            // ---- subtle gameplay vignette (atmosphere) ----
            if(!paused){
                gui.vignette(0.0f,0.0f,0.0f,0.28f);
            }
            // ---- crosshair (thin cross + center dot, gapped) ----
            if(!paused){
                float cx=W*0.5f, cy=Hh*0.5f, s=6*scale, gap=3*scale, t=1.5f*scale;
                float cr=0.86f,cg=0.86f,cb=0.82f,ca=0.55f;
                gui.rect(cx+gap, cy-t*0.5f, s, t, cr,cg,cb,ca);
                gui.rect(cx-gap-s, cy-t*0.5f, s, t, cr,cg,cb,ca);
                gui.rect(cx-t*0.5f, cy+gap, t, s, cr,cg,cb,ca);
                gui.rect(cx-t*0.5f, cy-gap-s, t, s, cr,cg,cb,ca);
                gui.rect(cx-t*0.6f, cy-t*0.6f, t*1.2f, t*1.2f, cr,cg,cb,0.7f);
            }

            // ---- bottom-left status cluster (health / stamina) ----
            float pad=22*scale;
            float panelW=300*scale, panelH=96*scale;
            float px=pad, py=Hh-panelH-pad;
            gui.panel(px,py,panelW,panelH,10*scale, 0.04f,0.04f,0.05f,0.55f);
            float barX=px+16*scale, barW=panelW-32*scale, barH=14*scale;
            // health
            float hf=gPlayer.health/gPlayer.maxHealth;
            gui.text("HEALTH", barX, py+10*scale, 11*scale, 0.75f,0.75f,0.78f,0.9f);
            gui.bar(barX, py+26*scale, barW, barH, hf,
                    gPlayer.injured?0.75f:0.65f, gPlayer.injured?0.12f:0.16f, 0.12f,0.95f,
                    0.12f,0.12f,0.14f,0.7f);
            // stamina
            gui.text("STAMINA", barX, py+46*scale, 11*scale, 0.75f,0.75f,0.78f,0.9f);
            gui.bar(barX, py+62*scale, barW, barH, gPlayer.stamina,
                    gPlayer.winded?0.55f:0.25f, gPlayer.winded?0.40f:0.55f, gPlayer.winded?0.10f:0.70f,0.95f,
                    0.12f,0.12f,0.14f,0.7f);

            // ---- bottom-right level / XP cluster ----
            float lpW=240*scale, lpH=70*scale;
            float lx=W-lpW-pad, ly=Hh-lpH-pad;
            gui.panel(lx,ly,lpW,lpH,10*scale, 0.04f,0.04f,0.05f,0.55f);
            char lvbuf[32]; snprintf(lvbuf,32,"LEVEL %d",gPlayer.level);
            gui.text(lvbuf, lx+16*scale, ly+10*scale, 13*scale, 0.85f,0.80f,0.55f,0.95f);
            float xf=gPlayer.xp/gPlayer.xpToNext;
            gui.bar(lx+16*scale, ly+38*scale, lpW-32*scale, 12*scale, xf,
                    0.55f,0.45f,0.85f,0.95f, 0.12f,0.12f,0.14f,0.7f);

            // ---- status-effect chips (top-center) ----
            float chipY=pad; float chipX=W*0.5f;
            auto chip=[&](const char* lab,float r,float g,float b){
                float w=gui.textWidth(lab,12*scale)+24*scale;
                gui.panel(chipX-w*0.5f,chipY,w,26*scale,8*scale,r*0.3f,g*0.3f,b*0.3f,0.7f);
                gui.textCentered(lab,chipX,chipY+7*scale,12*scale,r,g,b,1.0f);
                chipY+=32*scale;
            };
            if(gPlayer.injured) chip("INJURED",0.9f,0.3f,0.3f);
            if(gPlayer.winded)  chip("WINDED",0.9f,0.7f,0.3f);
            if(sprinting)       chip("SPRINT",0.5f,0.8f,1.0f);
            if(gFlashOn)        chip("FLASHLIGHT",0.9f,0.9f,0.7f);

            // ---- nearby-door proximity prompt (objective hint) ----
            if(!paused && gNearDoorKind>=0 && gNearDoorDist<11.0f){
                const char* lab = (gNearDoorKind==0)?"PURPLE DOOR NEAR  -  TELEPORT"
                                                    :"BROWN DOOR NEAR  -  EXIT";
                float r=(gNearDoorKind==0)?0.7f:0.85f, g=(gNearDoorKind==0)?0.4f:0.55f, b=(gNearDoorKind==0)?0.95f:0.25f;
                float w=gui.textWidth(lab,14*scale)+34*scale;
                float bx=W*0.5f-w*0.5f, by=Hh*0.70f;
                float fade=std::clamp(1.0f-gNearDoorDist/11.0f,0.25f,1.0f);
                gui.panel(bx,by,w,32*scale,9*scale, r*0.25f,g*0.25f,b*0.25f,0.6f*fade);
                gui.textCentered(lab,W*0.5f,by+9*scale,14*scale,r,g,b,fade);
            }

            // =====================================================
            //  PAUSE MENU
            // =====================================================
            if(paused){
                gui.rect(0,0,W,Hh, 0.0f,0.0f,0.0f,0.55f); // dim
                gui.vignette(0.0f,0.0f,0.0f,0.6f);
                float cx=W*0.5f;
                // title
                if(gWon){
                    gui.textCentered("YOU ESCAPED", cx, Hh*0.12f, 40*scale, 0.55f,0.85f,0.45f,1.0f);
                    gui.textCentered("YOU FOUND THE EXIT DOOR", cx, Hh*0.12f+46*scale, 16*scale, 0.7f,0.8f,0.6f,0.95f);
                } else {
                    gui.textCentered("BACKROOMS", cx, Hh*0.12f, 40*scale, 0.85f,0.80f,0.45f,1.0f);
                    gui.textCentered("LEVEL 0", cx, Hh*0.12f+46*scale, 16*scale, 0.6f,0.58f,0.4f,0.9f);
                }

                // hovered item via mouse Y
                auto item=[&](const char* label,int index,int count,float topY,float gap)->bool{
                    float y=topY+index*gap;
                    float iw=420*scale, ih=46*scale, ix=cx-iw*0.5f;
                    bool hover = (gMouseX>=ix&&gMouseX<=ix+iw&&gMouseY>=y&&gMouseY<=y+ih);
                    if(hover) gMenuSel=index;
                    bool sel = (gMenuSel==index);
                    gui.panel(ix,y,iw,ih,8*scale, sel?0.18f:0.06f, sel?0.16f:0.06f, sel?0.10f:0.07f, sel?0.9f:0.5f);
                    gui.textCentered(label,cx,y+13*scale,18*scale,
                                     sel?0.95f:0.7f, sel?0.92f:0.7f, sel?0.6f:0.7f, 1.0f);
                    bool act = sel && (gClick || enterPressed);
                    return act && (hover || enterPressed);
                };

                if(gState==GS_PAUSE_MAIN){
                    const char* items[]={"RESUME","GRAPHICS","AUDIO","CONTROLS","EXIT TO MENU","QUIT GAME"};
                    gMenuCount=6;
                    float topY=Hh*0.30f, gap=58*scale;
                    for(int i=0;i<6;i++){
                        if(item(items[i],i,6,topY,gap)){
                            if(i==0) setPlay(win);
                            else if(i==1) gState=GS_PAUSE_GRAPHICS, gMenuSel=0;
                            else if(i==2) gState=GS_PAUSE_AUDIO, gMenuSel=0;
                            else if(i==3) gState=GS_PAUSE_CONTROLS, gMenuSel=0;
                            else if(i==4){ // restart / respawn to spawn
                                gPlayer.health=gPlayer.maxHealth; gPlayer.stamina=1.0f;
                                setPlay(win);
                            }
                            else if(i==5) gQuit=true;
                        }
                    }
                } else if(gState==GS_PAUSE_GRAPHICS){
                    gui.textCentered("GRAPHICS",cx,Hh*0.24f,24*scale,0.85f,0.82f,0.5f,1.0f);
                    const char* qn[]={"LOW","MEDIUM","HIGH","ULTRA"};
                    char b0[48]; snprintf(b0,48,"QUALITY: %s",qn[gSet.quality]);
                    char b1[48]; snprintf(b1,48,"BLOOM: %s",gSet.bloom?"ON":"OFF");
                    char b2[48]; snprintf(b2,48,"FILM GRAIN: %s",gSet.filmGrain?"ON":"OFF");
                    char b3[48]; snprintf(b3,48,"BRIGHTNESS: %d%%",(int)(gSet.brightness*100));
                    const char* items[]={b0,b1,b2,b3,"BACK"};
                    gMenuCount=5;
                    float topY=Hh*0.32f, gap=58*scale;
                    for(int i=0;i<5;i++){
                        if(item(items[i],i,5,topY,gap)){
                            if(i==0) gSet.quality=(gSet.quality+1)%4;
                            else if(i==1) gSet.bloom=!gSet.bloom;
                            else if(i==2) gSet.filmGrain=!gSet.filmGrain;
                            else if(i==3){ gSet.brightness+=0.1f; if(gSet.brightness>1.55f) gSet.brightness=0.6f; }
                            else if(i==4) gState=GS_PAUSE_MAIN, gMenuSel=0;
                        }
                    }
                } else if(gState==GS_PAUSE_AUDIO){
                    gui.textCentered("AUDIO",cx,Hh*0.24f,24*scale,0.85f,0.82f,0.5f,1.0f);
                    char b0[48]; snprintf(b0,48,"MASTER VOLUME: %d%%",(int)(gSet.masterVol*100));
                    char b1[48]; snprintf(b1,48,"MUTE: %s",gSet.muted?"ON":"OFF");
                    const char* items[]={b0,b1,"BACK"};
                    gMenuCount=3;
                    float topY=Hh*0.34f, gap=58*scale;
                    for(int i=0;i<3;i++){
                        if(item(items[i],i,3,topY,gap)){
                            if(i==0){ gSet.masterVol+=0.1f; if(gSet.masterVol>1.01f) gSet.masterVol=0.0f;
                                      gAudio.setMasterVolume(gSet.masterVol); }
                            else if(i==1){ gSet.muted=!gSet.muted; gAudio.setMuted(gSet.muted); }
                            else if(i==2) gState=GS_PAUSE_MAIN, gMenuSel=0;
                        }
                    }
                } else if(gState==GS_PAUSE_CONTROLS){
                    gui.textCentered("CONTROLS",cx,Hh*0.20f,24*scale,0.85f,0.82f,0.5f,1.0f);
                    const char* lines[]={
                        "W A S D - MOVE","MOUSE - LOOK","SHIFT - SPRINT","SPACE - JUMP",
                        "F - FLASHLIGHT","M - MUTE","ESC - PAUSE / BACK"};
                    float ly2=Hh*0.30f;
                    for(int i=0;i<7;i++){ gui.textCentered(lines[i],cx,ly2,16*scale,0.78f,0.78f,0.8f,0.95f); ly2+=34*scale; }
                    gMenuCount=1;
                    float topY=Hh*0.30f+7*34*scale+10*scale;
                    if(item("BACK",0,1,topY,58*scale)) gState=GS_PAUSE_MAIN, gMenuSel=0;
                }
            }
        }
        gui.end();
        gClick=false;

#ifdef SHOT_TEST
        static int frameN=0; frameN++;
        if(frameN==12){
            std::vector<unsigned char> px(FB_W*FB_H*3);
            glReadBuffer(GL_BACK);
            glReadPixels(0,0,FB_W,FB_H,GL_RGB,GL_UNSIGNED_BYTE,px.data());
            FILE* f=fopen("/tmp/frame.ppm","wb");
            fprintf(f,"P6\n%d %d\n255\n",FB_W,FB_H);
            for(int y=FB_H-1;y>=0;y--) fwrite(&px[y*FB_W*3],1,FB_W*3,f);
            fclose(f);
            glfwSwapBuffers(win);
            break;
        }
#endif
        glfwSwapBuffers(win);
    }

    gAudio.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
