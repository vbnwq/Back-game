#pragma once
// =============================================================
//  Procedural 3D geometry library.
//  Every wall / floor / ceiling / pillar / fixture / prop is a
//  REAL solid mesh with thickness, correct outward normals, UVs
//  and tangents (for normal mapping). Nothing is a 2D billboard.
//
//  Meshes provided:
//    makeCube()        - unit solid cube (all 6 faces, outward normals)
//    makeBevelCube()   - cube with chamfered edges (no hard primitive look)
//    makePlane()       - single quad (used only for decals/signs, double sided)
//    makeCylinder()    - capped cylinder (pipes, barrels, light tubes)
//    makeSphere()      - UV sphere (bulbs, rounded props)
//    makeRoundedFixtureBezel() - bezel ring for ceiling light panels
//  All are indexed, share the Vertex format, and compute tangents.
// =============================================================
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nrm;
    glm::vec2 uv;
    glm::vec3 tan;
};

struct Mesh {
    GLuint vao=0, vbo=0, ebo=0;
    GLsizei count=0;
    void upload(const std::vector<Vertex>& v, const std::vector<unsigned>& idx){
        count=(GLsizei)idx.size();
        glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(Vertex),v.data(),GL_STATIC_DRAW);
        glGenBuffers(1,&ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(unsigned),idx.data(),GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,pos));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,nrm));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,uv));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,tan));
        glBindVertexArray(0);
    }
    void draw() const { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0); }
};

// helper: push a quad (4 corners CCW seen from outside) with a shared normal+tangent.
static inline void pushQuad(std::vector<Vertex>& v, std::vector<unsigned>& idx,
                            glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                            glm::vec3 n, glm::vec3 t,
                            glm::vec2 ua, glm::vec2 ub, glm::vec2 uc, glm::vec2 ud){
    unsigned base=(unsigned)v.size();
    v.push_back({a,n,ua,t});
    v.push_back({b,n,ub,t});
    v.push_back({c,n,uc,t});
    v.push_back({d,n,ud,t});
    idx.push_back(base); idx.push_back(base+1); idx.push_back(base+2);
    idx.push_back(base); idx.push_back(base+2); idx.push_back(base+3);
}

// -------------------------------------------------------------
// Solid unit cube centered at origin (size 1). Per-face UVs so
// non-uniform scaling stretches the texture; uUVScale tiles it.
// Outward-facing winding so GL_CULL_FACE(BACK) keeps every face.
// -------------------------------------------------------------
inline Mesh makeCube(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    float h=0.5f;
    // +X
    pushQuad(v,idx, {h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}, {1,0,0},{0,0,-1}, {0,0},{1,0},{1,1},{0,1});
    // -X
    pushQuad(v,idx, {-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}, {-1,0,0},{0,0,1}, {0,0},{1,0},{1,1},{0,1});
    // +Y (top)
    pushQuad(v,idx, {-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}, {0,1,0},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // -Y (bottom)
    pushQuad(v,idx, {-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}, {0,-1,0},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // +Z
    pushQuad(v,idx, {-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}, {0,0,1},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // -Z
    pushQuad(v,idx, {h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}, {0,0,-1},{-1,0,0}, {0,0},{1,0},{1,1},{0,1});
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
// Beveled / chamfered cube. Slightly inset corner faces remove
// the "obvious primitive" hard-edge look and catch light nicely.
// b = bevel fraction of half-size.
// -------------------------------------------------------------
inline Mesh makeBevelCube(float b=0.06f){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    float h=0.5f, i=0.5f-b; // inner extent
    // 6 main faces (slightly inset on their plane edges -> use i for tangential, h for axis)
    // +X
    pushQuad(v,idx, {h,-i,i},{h,-i,-i},{h,i,-i},{h,i,i}, {1,0,0},{0,0,-1}, {0,0},{1,0},{1,1},{0,1});
    // -X
    pushQuad(v,idx, {-h,-i,-i},{-h,-i,i},{-h,i,i},{-h,i,-i}, {-1,0,0},{0,0,1}, {0,0},{1,0},{1,1},{0,1});
    // +Y
    pushQuad(v,idx, {-i,h,i},{i,h,i},{i,h,-i},{-i,h,-i}, {0,1,0},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // -Y
    pushQuad(v,idx, {-i,-h,-i},{i,-h,-i},{i,-h,i},{-i,-h,i}, {0,-1,0},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // +Z
    pushQuad(v,idx, {-i,-i,h},{i,-i,h},{i,i,h},{-i,i,h}, {0,0,1},{1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // -Z
    pushQuad(v,idx, {i,-i,-h},{-i,-i,-h},{-i,i,-h},{i,i,-h}, {0,0,-1},{-1,0,0}, {0,0},{1,0},{1,1},{0,1});
    // 12 edge bevels (each a thin angled quad). Normals point diagonally.
    auto edge=[&](glm::vec3 a,glm::vec3 bb,glm::vec3 c,glm::vec3 d,glm::vec3 n){
        pushQuad(v,idx,a,bb,c,d,glm::normalize(n),glm::normalize(glm::cross(n,glm::vec3(0,1,0))+glm::vec3(0.001f)),{0,0},{1,0},{1,1},{0,1});
    };
    // +X +Y edge
    edge({h,i,i},{h,i,-i},{i,h,-i},{i,h,i},{1,1,0});
    edge({-i,h,i},{-i,h,-i},{-h,i,-i},{-h,i,i},{-1,1,0});
    edge({h,-i,-i},{h,-i,i},{i,-h,i},{i,-h,-i},{1,-1,0});
    edge({-h,-i,i},{-h,-i,-i},{-i,-h,-i},{-i,-h,i},{-1,-1,0});
    // Z edges
    edge({i,i,h},{-i,i,h},{-i,h,i},{i,h,i},{0,1,1});
    edge({-i,i,-h},{i,i,-h},{i,h,-i},{-i,h,-i},{0,1,-1});
    edge({-i,-i,h},{i,-i,h},{i,-h,i},{-i,-h,i},{0,-1,1});
    edge({i,-i,-h},{-i,-i,-h},{-i,-h,-i},{i,-h,-i},{0,-1,-1});
    // X/Z corner verticals
    edge({h,-i,i},{h,i,i},{i,i,h},{i,-i,h},{1,0,1});
    edge({i,-i,-h},{i,i,-h},{h,i,-i},{h,-i,-i},{1,0,-1});
    edge({-i,-i,h},{-i,i,h},{-h,i,i},{-h,-i,i},{-1,0,1});
    edge({-h,-i,-i},{-h,i,-i},{-i,i,-h},{-i,-i,-h},{-1,0,-1});
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
// Single quad in the XZ-plane facing +Y by default? No: build it
// in XY plane facing +Z, double-sided so signs never vanish.
// -------------------------------------------------------------
inline Mesh makePlaneDouble(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    float h=0.5f;
    pushQuad(v,idx, {-h,-h,0},{h,-h,0},{h,h,0},{-h,h,0}, {0,0,1},{1,0,0}, {0,1},{1,1},{1,0},{0,0});
    pushQuad(v,idx, {h,-h,0},{-h,-h,0},{-h,h,0},{h,h,0}, {0,0,-1},{-1,0,0}, {0,1},{1,1},{1,0},{0,0});
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
// Capped solid cylinder along Y, radius .5, height 1, centered.
// -------------------------------------------------------------
inline Mesh makeCylinder(int seg=32){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    // side wall
    for(int i=0;i<=seg;i++){
        float a=(float)i/seg*6.2831853f;
        float x=cosf(a)*0.5f, z=sinf(a)*0.5f;
        glm::vec3 n=glm::normalize(glm::vec3(x,0,z));
        glm::vec3 t=glm::vec3(-sinf(a),0,cosf(a));
        v.push_back({{x,-0.5f,z},n,{(float)i/seg*4.0f,0},t});
        v.push_back({{x, 0.5f,z},n,{(float)i/seg*4.0f,1},t});
    }
    for(int i=0;i<seg;i++){
        unsigned b=i*2;
        idx.push_back(b); idx.push_back(b+1); idx.push_back(b+2);
        idx.push_back(b+1); idx.push_back(b+3); idx.push_back(b+2);
    }
    // caps
    unsigned topC=(unsigned)v.size(); v.push_back({{0,0.5f,0},{0,1,0},{0.5f,0.5f},{1,0,0}});
    unsigned botC=(unsigned)v.size(); v.push_back({{0,-0.5f,0},{0,-1,0},{0.5f,0.5f},{1,0,0}});
    unsigned ringTop=(unsigned)v.size();
    for(int i=0;i<=seg;i++){ float a=(float)i/seg*6.2831853f; float x=cosf(a)*0.5f,z=sinf(a)*0.5f;
        v.push_back({{x,0.5f,z},{0,1,0},{x+0.5f,z+0.5f},{1,0,0}}); }
    unsigned ringBot=(unsigned)v.size();
    for(int i=0;i<=seg;i++){ float a=(float)i/seg*6.2831853f; float x=cosf(a)*0.5f,z=sinf(a)*0.5f;
        v.push_back({{x,-0.5f,z},{0,-1,0},{x+0.5f,z+0.5f},{1,0,0}}); }
    for(int i=0;i<seg;i++){
        idx.push_back(topC); idx.push_back(ringTop+i); idx.push_back(ringTop+i+1);
        idx.push_back(botC); idx.push_back(ringBot+i+1); idx.push_back(ringBot+i);
    }
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
// UV sphere (radius .5). Used for bulbs / rounded fittings.
// -------------------------------------------------------------
inline Mesh makeSphere(int seg=24, int rings=16){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    for(int y=0;y<=rings;y++){
        float vv=(float)y/rings, phi=vv*3.14159265f;
        for(int x=0;x<=seg;x++){
            float uu=(float)x/seg, theta=uu*6.2831853f;
            glm::vec3 p(sinf(phi)*cosf(theta),cosf(phi),sinf(phi)*sinf(theta));
            glm::vec3 t(-sinf(theta),0,cosf(theta));
            v.push_back({p*0.5f,p,{uu,vv},t});
        }
    }
    int stride=seg+1;
    for(int y=0;y<rings;y++) for(int x=0;x<seg;x++){
        unsigned a=y*stride+x, b=a+stride;
        idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
        idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
    }
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  Helper: append a transformed solid box into a vertex/idx pool
//  (used to assemble multi-part detailed props as one mesh).
// -------------------------------------------------------------
static inline void addBox(std::vector<Vertex>& v,std::vector<unsigned>& idx,
                          glm::vec3 c,glm::vec3 s,glm::vec2 uvScale=glm::vec2(1)){
    glm::vec3 h=s*0.5f;
    auto q=[&](glm::vec3 a,glm::vec3 b,glm::vec3 cc,glm::vec3 d,glm::vec3 n,glm::vec3 t){
        pushQuad(v,idx, c+a*h, c+b*h, c+cc*h, c+d*h, n,t,
                 {0,0},{uvScale.x,0},{uvScale.x,uvScale.y},{0,uvScale.y});
    };
    q({1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1},{1,0,0},{0,0,-1});
    q({-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1},{-1,0,0},{0,0,1});
    q({-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1},{0,1,0},{1,0,0});
    q({-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},{0,-1,0},{1,0,0});
    q({-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{0,0,1},{1,0,0});
    q({1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1},{0,0,-1},{-1,0,0});
}

// -------------------------------------------------------------
//  DETAILED METAL BARREL: ribbed body + raised top/bottom rims.
//  Centered, radius .5, height 1. Replaces the plain cylinder.
// -------------------------------------------------------------
inline Mesh makeBarrel(int seg=28){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    auto ring=[&](float y,float rad,float uy){
        for(int i=0;i<=seg;i++){
            float a=(float)i/seg*6.2831853f;
            float x=cosf(a)*rad, z=sinf(a)*rad;
            glm::vec3 n=glm::normalize(glm::vec3(cosf(a),0,sinf(a)));
            glm::vec3 t=glm::vec3(-sinf(a),0,cosf(a));
            v.push_back({{x,y,z},n,{(float)i/seg*3.0f,uy},t});
        }
    };
    // profile with two raised rings (rims) so the barrel reads as ribbed
    struct P{float y,r,uy;};
    P prof[]={{-0.5f,0.46f,0},{-0.42f,0.50f,0.1f},{-0.30f,0.48f,0.2f},
              {0.0f,0.50f,0.5f},{0.30f,0.48f,0.8f},{0.42f,0.50f,0.9f},{0.5f,0.46f,1.0f}};
    int NP=7;
    for(int p=0;p<NP;p++) ring(prof[p].y,prof[p].r,prof[p].uy);
    for(int p=0;p<NP-1;p++){
        unsigned a=p*(seg+1), b=(p+1)*(seg+1);
        for(int i=0;i<seg;i++){
            idx.push_back(a+i); idx.push_back(b+i); idx.push_back(a+i+1);
            idx.push_back(a+i+1); idx.push_back(b+i); idx.push_back(b+i+1);
        }
    }
    // caps
    unsigned topC=(unsigned)v.size(); v.push_back({{0,0.5f,0},{0,1,0},{0.5f,0.5f},{1,0,0}});
    unsigned botC=(unsigned)v.size(); v.push_back({{0,-0.5f,0},{0,-1,0},{0.5f,0.5f},{1,0,0}});
    unsigned rt=(unsigned)v.size();
    for(int i=0;i<=seg;i++){ float a=(float)i/seg*6.2831853f; float x=cosf(a)*0.46f,z=sinf(a)*0.46f;
        v.push_back({{x,0.5f,z},{0,1,0},{x+0.5f,z+0.5f},{1,0,0}}); }
    unsigned rb=(unsigned)v.size();
    for(int i=0;i<=seg;i++){ float a=(float)i/seg*6.2831853f; float x=cosf(a)*0.46f,z=sinf(a)*0.46f;
        v.push_back({{x,-0.5f,z},{0,-1,0},{x+0.5f,z+0.5f},{1,0,0}}); }
    for(int i=0;i<seg;i++){
        idx.push_back(topC); idx.push_back(rt+i); idx.push_back(rt+i+1);
        idx.push_back(botC); idx.push_back(rb+i+1); idx.push_back(rb+i);
    }
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  STORAGE LOCKER / CABINET (multi-part: body, recessed door,
//  handle, vent slits, feet). Unit footprint ~1 wide x 1 deep,
//  ~2 tall (origin at base center).
// -------------------------------------------------------------
inline Mesh makeLocker(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    // body
    addBox(v,idx, {0,1.0f,0}, {1.0f,2.0f,0.9f});
    // door (slightly proud, front +Z)
    addBox(v,idx, {0,1.05f,0.47f}, {0.86f,1.78f,0.04f});
    // handle
    addBox(v,idx, {0.30f,1.05f,0.51f}, {0.06f,0.30f,0.05f});
    // top vent slits
    for(int i=0;i<3;i++) addBox(v,idx, {0,1.78f-i*0.06f,0.47f}, {0.5f,0.02f,0.05f});
    // feet
    for(float sx=-1;sx<=1;sx+=2) for(float sz=-1;sz<=1;sz+=2)
        addBox(v,idx, {sx*0.42f,0.04f,sz*0.38f}, {0.1f,0.08f,0.1f});
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  CEILING VENT DUCT BOX (a recessed louvre housing with frame).
//  Unit ~1.2 x 1.2 footprint, shallow. Origin at center.
// -------------------------------------------------------------
inline Mesh makeVentBox(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    // frame ring (4 bars)
    addBox(v,idx, {0,0,0.55f}, {1.2f,0.22f,0.12f});
    addBox(v,idx, {0,0,-0.55f},{1.2f,0.22f,0.12f});
    addBox(v,idx, {0.55f,0,0}, {0.12f,0.22f,1.2f});
    addBox(v,idx, {-0.55f,0,0},{0.12f,0.22f,1.2f});
    // recessed back plate
    addBox(v,idx, {0,-0.08f,0}, {1.0f,0.04f,1.0f});
    // louvre blades
    for(int i=0;i<6;i++){
        float z=-0.42f+i*0.17f;
        addBox(v,idx, {0,-0.02f,z}, {0.96f,0.06f,0.05f});
    }
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  DOOR — a framed door slab with a raised casing/jamb and a
//  protruding handle. Unit: ~1 wide (X), ~1 tall (Y centred at
//  origin), thin in Z. Scaled to fit a cell opening at draw time.
// -------------------------------------------------------------
inline Mesh makeDoor(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    // door slab (occupies most of the unit volume)
    addBox(v,idx, {0,0,0}, {0.86f,0.96f,0.16f}, {1,1});
    // surrounding frame / casing (4 bars, slightly proud)
    addBox(v,idx, {-0.47f,0.0f,0.02f}, {0.10f,1.0f,0.24f});   // left jamb
    addBox(v,idx, { 0.47f,0.0f,0.02f}, {0.10f,1.0f,0.24f});   // right jamb
    addBox(v,idx, {0.0f, 0.49f,0.02f}, {1.04f,0.10f,0.24f});  // lintel
    addBox(v,idx, {0.0f,-0.49f,0.02f}, {1.04f,0.10f,0.24f});  // threshold
    // lever handle on the right
    addBox(v,idx, {0.30f,0.0f,0.11f}, {0.16f,0.05f,0.10f});
    addBox(v,idx, {0.36f,0.0f,0.16f}, {0.05f,0.05f,0.10f});
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  HUMANOID STATUE — a disturbing, faceless standing figure
//  assembled from boxes/spheres (torso, head, arms, legs). Origin
//  at the base centre, ~2.0 tall, faces +Z. Deliberately stiff /
//  uncanny proportions. Built once; oriented per-instance.
// -------------------------------------------------------------
inline Mesh makeStatue(){
    std::vector<Vertex> v; std::vector<unsigned> idx;
    // legs
    addBox(v,idx, {-0.16f,0.45f,0.0f}, {0.18f,0.92f,0.20f});
    addBox(v,idx, { 0.16f,0.45f,0.0f}, {0.18f,0.92f,0.20f});
    // pelvis / hips
    addBox(v,idx, {0.0f,0.95f,0.0f}, {0.46f,0.20f,0.26f});
    // torso (tapering: stacked boxes)
    addBox(v,idx, {0.0f,1.18f,0.0f}, {0.50f,0.34f,0.28f});
    addBox(v,idx, {0.0f,1.50f,0.0f}, {0.46f,0.34f,0.26f});
    // shoulders
    addBox(v,idx, {0.0f,1.66f,0.0f}, {0.62f,0.16f,0.24f});
    // arms hanging at sides (slightly forward, uncanny)
    addBox(v,idx, {-0.36f,1.30f,0.04f}, {0.14f,0.66f,0.16f});
    addBox(v,idx, { 0.36f,1.30f,0.04f}, {0.14f,0.66f,0.16f});
    // hands
    addBox(v,idx, {-0.36f,0.96f,0.06f}, {0.13f,0.16f,0.14f});
    addBox(v,idx, { 0.36f,0.96f,0.06f}, {0.13f,0.16f,0.14f});
    // neck
    addBox(v,idx, {0.0f,1.82f,0.0f}, {0.16f,0.14f,0.16f});
    // head: a smooth featureless ovoid (sphere, slightly squashed)
    {
        int seg=18, rings=12;
        unsigned baseV=(unsigned)v.size();
        for(int yy=0;yy<=rings;yy++){
            float vv=(float)yy/rings, phi=vv*3.14159265f;
            for(int xx=0;xx<=seg;xx++){
                float uu=(float)xx/seg, theta=uu*6.2831853f;
                glm::vec3 p(sinf(phi)*cosf(theta),cosf(phi),sinf(phi)*sinf(theta));
                glm::vec3 t(-sinf(theta),0,cosf(theta));
                glm::vec3 pos = p*glm::vec3(0.17f,0.21f,0.18f) + glm::vec3(0.0f,2.02f,0.0f);
                v.push_back({pos,p,{uu,vv},t});
            }
        }
        int stride=seg+1;
        for(int yy=0;yy<rings;yy++) for(int xx=0;xx<seg;xx++){
            unsigned a=baseV+yy*stride+xx, b=a+stride;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
        }
    }
    Mesh m; m.upload(v,idx); return m;
}

// -------------------------------------------------------------
//  SECURITY CAMERA — assembled at draw time from animated parts
//  (mount / arm / body / lens / indicator) reusing the cube and
//  cylinder meshes above, so the pan motion can be driven per-part.
// -------------------------------------------------------------
